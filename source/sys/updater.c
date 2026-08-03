#include "sys/updater.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <jansson.h>
#include <sys/statvfs.h>

#include "app.h"
#include "net/http.h"
#include "audio/audio_player.h"
#include "ui/gui.h"

#define UPD_OWNER "5seg"
#define UPD_REPO  "j3ds"
#define UPD_CIA_ASSET "j3ds.cia"
#define UPD_CIA_PATH "sdmc:/3ds/j3ds/update.cia"
#define UPD_CIA_URL_MAX 768

/* GitHub (api.github.com and objects.githubusercontent.com) now requires
   TLS 1.2, which the 3DS httpc/sslc stack cannot negotiate. The in-app
   self-update path is therefore disabled: updaterCheck() never spawns a
   worker thread and never touches httpc, it only shows a short notice.
   Set this to 1 to restore the original download/install path. */
#define UPDATER_GITHUB_ENABLED 0

#define UPD_MSG_UNAVAILABLE \
	"アプリ内更新は現在利用できません。\n" \
	"GitHub TLS 1.2非互換のため、\n" \
	"GitHub ReleasesからCIAを\n" \
	"手動インストールしてください。"

#define UPD_ERR_OUT_OF_SPACE ((Result)-0x5000)
#define UPD_ERR_NO_MEM       ((Result)-0x5001)

/* Private results reported by worker threads. */
enum {
	UPD_RES_OK_UPDATE = 0,
	UPD_RES_OK_UP_TO_DATE,
	UPD_RES_HTTP,
	UPD_RES_BAD_JSON,
	UPD_RES_NO_ASSET,
	UPD_RES_NO_MEM,
	UPD_RES_CANCELLED,
	UPD_RES_INSTALL
};

/* Thread phases while UPD_DOWNLOADING is active. */
enum {
	UPD_PHASE_DOWNLOAD = 1,
	UPD_PHASE_INSTALL
};

typedef struct {
	UpdaterState state;
	Result lastResult;
	int phase;
	char latestTag[32];
	char ciaUrl[UPD_CIA_URL_MAX];
	char message[256];
	u64 titleId;
	Thread thread;
	volatile bool threadDone;
	volatile bool cancelled;
	volatile int progressPct;
	int anim;
} Updater;

static Updater s_up;

#if UPDATER_GITHUB_ENABLED
/* Compare dotted numeric versions, ignoring a leading 'v'. Returns
   <0 if a<b, 0 if equal, >0 if a>b. */
static int versionCompare(const char* a, const char* b)
{
	if (*a == 'v' || *a == 'V') a++;
	if (*b == 'v' || *b == 'V') b++;

	while (*a || *b) {
		long na = 0, nb = 0;
		while (*a >= '0' && *a <= '9') { na = na * 10 + (*a - '0'); a++; }
		while (*b >= '0' && *b <= '9') { nb = nb * 10 + (*b - '0'); b++; }
		if (na != nb) return na < nb ? -1 : 1;
		if (*a == '.') a++;
		if (*b == '.') b++;
	}
	return 0;
}
#endif /* UPDATER_GITHUB_ENABLED */

static void updaterSetMessage(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s_up.message, sizeof(s_up.message), fmt, ap);
	va_end(ap);
	s_up.message[sizeof(s_up.message) - 1] = '\0';
}

#if UPDATER_GITHUB_ENABLED
/* ------------------------------------------------------------------ */
/* Check thread: hit the GitHub latest-release API, remember the tag   */
/* and the j3ds.cia asset URL.                                         */
/* ------------------------------------------------------------------ */

static void checkThreadFunc(void* arg)
{
	(void)arg;
	s_up.threadDone = false;
	s_up.lastResult = UPD_RES_HTTP;

	char* body = NULL;
	size_t bodyLen = 0;
	Result ret = httpGetNoVerify(
		"https://api.github.com/repos/" UPD_OWNER "/" UPD_REPO "/releases/latest",
		&body, &bodyLen);
	if (R_FAILED(ret)) {
		updaterSetMessage("Update check failed (v%s, ret=%08lX, HTTP %d)",
			APP_VERSION, (unsigned long)ret, httpLastStatus());
		s_up.threadDone = true;
		return;
	}

	json_error_t jerr;
	memset(&jerr, 0, sizeof(jerr));
	json_t* root = json_loads(body, 0, &jerr);
	free(body);

	if (!root) {
		s_up.lastResult = UPD_RES_BAD_JSON;
		updaterSetMessage("Update check: bad response");
		s_up.threadDone = true;
		return;
	}

	const char* tag = json_string_value(json_object_get(root, "tag_name"));
	s_up.latestTag[0] = '\0';
	s_up.ciaUrl[0] = '\0';
	if (tag)
		snprintf(s_up.latestTag, sizeof(s_up.latestTag), "%s", tag);

	json_t* assets = json_object_get(root, "assets");
	if (json_is_array(assets)) {
		size_t n = json_array_size(assets);
		for (size_t i = 0; i < n; i++) {
			json_t* a = json_array_get(assets, i);
			if (!json_is_object(a)) continue;
			const char* name = json_string_value(json_object_get(a, "name"));
			const char* url = json_string_value(json_object_get(a, "browser_download_url"));
			if (name && url && strcmp(name, UPD_CIA_ASSET) == 0) {
				snprintf(s_up.ciaUrl, sizeof(s_up.ciaUrl), "%s", url);
				break;
			}
		}
	}

	json_decref(root);

	if (s_up.latestTag[0] == '\0' || s_up.ciaUrl[0] == '\0') {
		s_up.lastResult = UPD_RES_NO_ASSET;
		updaterSetMessage("Update check: release not found");
		s_up.threadDone = true;
		return;
	}

	if (versionCompare(APP_VERSION, s_up.latestTag) >= 0) {
		s_up.lastResult = UPD_RES_OK_UP_TO_DATE;
		s_up.threadDone = true;
		return;
	}

	s_up.lastResult = UPD_RES_OK_UPDATE;
	s_up.threadDone = true;
}

/* ------------------------------------------------------------------ */
/* Install thread: download the CIA, then install it via the AM        */
/* service (Universal-Updater style).                                  */
/* ------------------------------------------------------------------ */

static void downloadProgress(size_t downloaded, size_t total)
{
	if (s_up.cancelled)
		httpSetCancel(true);
	if (total > 0) {
		int pct = (int)(downloaded * 100 / total);
		if (pct > 100) pct = 100;
		s_up.progressPct = pct;
	} else {
		s_up.progressPct = -1;
	}
}

static Result installCiaFromPath(const char* sdmcPath)
{
	const char* rel = sdmcPath;
	if (strncmp(rel, "sdmc:", 5) == 0)
		rel += 5;

	Handle fileHandle = 0;
	Handle ciaHandle = 0;
	Result ret = 0;

	ret = FSUSER_OpenFileDirectly(&fileHandle, ARCHIVE_SDMC,
		fsMakePath(PATH_EMPTY, ""), fsMakePath(PATH_ASCII, rel), FS_OPEN_READ, 0);
	if (R_FAILED(ret)) return ret;

	AM_TitleInfo info;
	memset(&info, 0, sizeof(info));
	if (R_FAILED(ret = AM_GetCiaFileInfo(MEDIATYPE_SD, &info, fileHandle)))
		goto close_file;

	u64 ciaSize = 0;
	if (R_FAILED(ret = FSFILE_GetSize(fileHandle, &ciaSize)))
		goto close_file;

	u64 required = 0;
	if (R_SUCCEEDED(AM_GetCiaRequiredSpace(&required, MEDIATYPE_SD, fileHandle))) {
		struct statvfs st;
		if (statvfs("sdmc:/", &st) == 0) {
			u64 freeBytes = (u64)st.f_bavail * (u64)st.f_frsize;
			if (freeBytes < required) {
				ret = UPD_ERR_OUT_OF_SPACE;
				goto close_file;
			}
		}
	}

	if (R_FAILED(ret = AM_StartCiaInstall(MEDIATYPE_SD, &ciaHandle)))
		goto close_file;

	const u32 chunkSize = 0x200000;
	u8* buf = (u8*)malloc(chunkSize);
	if (!buf) {
		ret = UPD_ERR_NO_MEM;
		goto cancel_install;
	}

	u32 bytesRead = 0, bytesWritten = 0;
	u64 offset = 0;
	do {
		if (s_up.cancelled) {
			free(buf);
			ret = HTTP_ERR_CANCELLED;
			goto cancel_install;
		}
		if (R_FAILED(ret = FSFILE_Read(fileHandle, &bytesRead, offset, buf, chunkSize))) {
			free(buf);
			goto cancel_install;
		}
		if (bytesRead == 0) break;
		if (R_FAILED(ret = FSFILE_Write(ciaHandle, &bytesWritten, offset, buf, bytesRead,
				FS_WRITE_FLUSH))) {
			free(buf);
			goto cancel_install;
		}
		offset += bytesRead;
	} while (offset < ciaSize);

	free(buf);

	if (R_FAILED(ret = AM_FinishCiaInstall(ciaHandle)))
		goto close_file;
	ciaHandle = 0;

	s_up.titleId = info.titleID;

close_file:
	FSFILE_Close(fileHandle);
	return ret;

cancel_install:
	if (ciaHandle) AM_CancelCIAInstall(ciaHandle);
	goto close_file;
}

static void installThreadFunc(void* arg)
{
	(void)arg;
	s_up.threadDone = false;
	s_up.phase = UPD_PHASE_DOWNLOAD;
	s_up.progressPct = 0;
	s_up.lastResult = UPD_RES_INSTALL;

	Result ret = httpDownloadFileWithProgressNoVerify(s_up.ciaUrl, UPD_CIA_PATH,
		downloadProgress);
	if (s_up.cancelled || ret == HTTP_ERR_CANCELLED) {
		s_up.lastResult = UPD_RES_CANCELLED;
		s_up.threadDone = true;
		return;
	}
	if (R_FAILED(ret)) {
		updaterSetMessage("Download failed (ret=%08lX, HTTP %d)",
			(unsigned long)ret, httpLastStatus());
		s_up.threadDone = true;
		return;
	}

	s_up.phase = UPD_PHASE_INSTALL;
	s_up.progressPct = 100;

	ret = installCiaFromPath(UPD_CIA_PATH);
	if (s_up.cancelled) {
		s_up.lastResult = UPD_RES_CANCELLED;
		s_up.threadDone = true;
		return;
	}
	if (R_FAILED(ret)) {
		updaterSetMessage("Install failed (ret=%08lX)", (unsigned long)ret);
		s_up.threadDone = true;
		return;
	}

	s_up.lastResult = UPD_RES_OK_UPDATE;
	s_up.threadDone = true;
}
#endif /* UPDATER_GITHUB_ENABLED */

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void updaterInit(void)
{
	memset(&s_up, 0, sizeof(s_up));
	amInit();
}

void updaterExit(void)
{
	if (s_up.thread) {
		if (!s_up.threadDone) {
			s_up.cancelled = true;
			httpSetCancel(true);
			threadJoin(s_up.thread, U64_MAX);
		}
		threadFree(s_up.thread);
		s_up.thread = 0;
	}
	amExit();
}

bool updaterActive(void)
{
	return s_up.state != UPD_IDLE;
}

void updaterCheck(void)
{
	if (s_up.state != UPD_IDLE)
		return;

	s_up.anim = 0;
	s_up.thread = 0;
	s_up.threadDone = false;
	s_up.cancelled = false;
	s_up.phase = 0;
	s_up.progressPct = 0;
	s_up.message[0] = '\0';
	s_up.latestTag[0] = '\0';
	s_up.ciaUrl[0] = '\0';

#if UPDATER_GITHUB_ENABLED
	s_up.state = UPD_CHECKING;

	s32 prio = 0;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	s_up.thread = threadCreate(checkThreadFunc, NULL, 64 * 1024, prio - 1, -2, false);
	if (!s_up.thread) {
		s_up.state = UPD_MESSAGE;
		updaterSetMessage("Could not start update check");
	}
#else
	/* No thread, no httpc, no network: just tell the user why. */
	s_up.lastResult = UPD_RES_HTTP;
	s_up.state = UPD_MESSAGE;
	updaterSetMessage("%s", UPD_MSG_UNAVAILABLE);
#endif
}

#if UPDATER_GITHUB_ENABLED
static void updaterStartInstall(void)
{
	audioStop();
	s_up.state = UPD_DOWNLOADING;
	s_up.anim = 0;
	s_up.threadDone = false;
	s_up.cancelled = false;
	s_up.phase = UPD_PHASE_DOWNLOAD;
	s_up.progressPct = 0;

	s32 prio = 0;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	s_up.thread = threadCreate(installThreadFunc, NULL, 64 * 1024, prio - 1, -2, false);
	if (!s_up.thread) {
		s_up.state = UPD_MESSAGE;
		updaterSetMessage("Could not start update");
	}
}

static void relaunchTitle(void)
{
	u8 param[0x300] = { 0 };
	u8 hmac[0x20] = { 0 };

	if (R_FAILED(APT_PrepareToDoApplicationJump(0, s_up.titleId, MEDIATYPE_SD)))
		return;

	configSave(&g_app.config);
	APT_DoApplicationJump(param, sizeof(param), hmac);
}
#endif /* UPDATER_GITHUB_ENABLED */

void updaterUpdate(void)
{
	s_up.anim++;

	/* Only a live worker thread can ever set threadDone; the thread handle
	   guard keeps this branch inert when the updater runs threadless. */
	if (s_up.threadDone && s_up.thread) {
		threadJoin(s_up.thread, U64_MAX);
		threadFree(s_up.thread);
		s_up.thread = 0;
		s_up.threadDone = false;

		UpdaterState prev = s_up.state;
		s_up.state = UPD_IDLE;

		if (s_up.cancelled) {
			s_up.cancelled = false;
			return;
		}

		switch (s_up.lastResult) {
		case UPD_RES_OK_UPDATE:
			if (prev == UPD_CHECKING)
				s_up.state = UPD_CONFIRM;
			else if (prev == UPD_DOWNLOADING)
				s_up.state = UPD_DONE;
			else
				s_up.state = UPD_MESSAGE;
			break;
		case UPD_RES_OK_UP_TO_DATE:
			s_up.state = UPD_MESSAGE;
			updaterSetMessage("You are up to date (v%s)", APP_VERSION);
			break;
		case UPD_RES_CANCELLED:
			s_up.state = UPD_IDLE;
			break;
		default:
			s_up.state = UPD_MESSAGE;
			break;
		}
		return;
	}

	u32 kDown = g_app.kDown;

	switch (s_up.state) {
#if UPDATER_GITHUB_ENABLED
	case UPD_CHECKING:
		if (kDown & KEY_B)
			s_up.cancelled = true;
		break;

	case UPD_CONFIRM:
		if (kDown & KEY_A)
			updaterStartInstall();
		else if (kDown & KEY_B)
			s_up.state = UPD_IDLE;
		break;

	case UPD_DOWNLOADING:
		if (kDown & KEY_B)
			s_up.cancelled = true;
		break;

	case UPD_DONE:
		if (kDown & KEY_A)
			relaunchTitle();
		else if (kDown & KEY_B)
			s_up.state = UPD_IDLE;
		break;
#endif

	case UPD_MESSAGE:
		if (kDown & KEY_B)
			s_up.state = UPD_IDLE;
		break;

	default:
#if !UPDATER_GITHUB_ENABLED
		/* The networked states are unreachable now and have no worker
		   thread behind them, so never let the modal get stuck there. */
		s_up.state = UPD_IDLE;
		s_up.cancelled = false;
		s_up.threadDone = false;
#endif
		break;
	}
}

/* ------------------------------------------------------------------ */
/* Rendering (bottom screen modal overlay)                             */
/* ------------------------------------------------------------------ */

#define UPD_LINE_H 18.0f

/* Draw a possibly multi-line ('\n'-separated) message, centred line by line. */
static void drawMessage(const char* text, float cx, float y, float scale, u32 color)
{
	char line[128];

	while (*text) {
		const char* nl = strchr(text, '\n');
		size_t len = nl ? (size_t)(nl - text) : strlen(text);
		if (len >= sizeof(line))
			len = sizeof(line) - 1;
		memcpy(line, text, len);
		line[len] = '\0';

		guiTextCentered(line, cx, y, scale, color);
		y += UPD_LINE_H;

		if (!nl)
			break;
		text = nl + 1;
	}
}

#if UPDATER_GITHUB_ENABLED
static const char* spinner(void)
{
	static const char frames[] = "-\\|/";
	return &frames[(s_up.anim / 2) % 4];
}

static void drawProgressBar(int pct)
{
	const float x = 46, y = 172, w = GUI_BOT_W - 92, h = 14;
	guiRect(x, y, w, h, GUI_COL_PANEL2);
	if (pct >= 0 && pct <= 100) {
		float fw = (w - 4) * pct / 100.0f;
		if (fw > 0)
			guiRect(x + 2, y + 2, fw, h - 4, GUI_COL_ACCENT);
	}
}
#endif /* UPDATER_GITHUB_ENABLED */

void updaterRenderBottom(void)
{
	guiPanel(30, 40, GUI_BOT_W - 60, 160);
	guiText("UPDATE", 46, 50, 0.6f, GUI_COL_ACCENT);

	switch (s_up.state) {
#if UPDATER_GITHUB_ENABLED
	case UPD_CHECKING:
		guiTextCentered("Checking for updates...", GUI_BOT_W / 2.0f, 104, 0.5f,
			GUI_COL_TEXT);
		guiTextCentered(spinner(), GUI_BOT_W / 2.0f, 132, 0.7f, GUI_COL_MUTED);
		guiTextCentered("B: Back", GUI_BOT_W / 2.0f, 168, 0.45f, GUI_COL_MUTED);
		break;

	case UPD_CONFIRM: {
		char line[96];
		snprintf(line, sizeof(line), "New version %s available", s_up.latestTag);
		guiTextCentered(line, GUI_BOT_W / 2.0f, 100, 0.5f, GUI_COL_GOOD);
		guiTextCentered("Download & install?", GUI_BOT_W / 2.0f, 126, 0.5f,
			GUI_COL_TEXT);
		guiTextCentered("A: Install   B: Cancel", GUI_BOT_W / 2.0f, 168, 0.45f,
			GUI_COL_MUTED);
		break;
	}

	case UPD_DOWNLOADING:
		if (s_up.phase == UPD_PHASE_INSTALL) {
			guiTextCentered("Installing...", GUI_BOT_W / 2.0f, 104, 0.55f,
				GUI_COL_TEXT);
		} else if (s_up.progressPct >= 0) {
			char line[48];
			snprintf(line, sizeof(line), "Downloading... %d%%", s_up.progressPct);
			guiTextCentered(line, GUI_BOT_W / 2.0f, 104, 0.55f, GUI_COL_TEXT);
		} else {
			guiTextCentered("Downloading...", GUI_BOT_W / 2.0f, 104, 0.55f,
				GUI_COL_TEXT);
		}
		drawProgressBar(s_up.progressPct);
		guiTextCentered("B: Cancel", GUI_BOT_W / 2.0f, 196, 0.45f, GUI_COL_MUTED);
		break;

	case UPD_DONE:
		guiTextCentered("Installed successfully!", GUI_BOT_W / 2.0f, 100, 0.55f,
			GUI_COL_GOOD);
		guiTextCentered("Relaunch j3ds?", GUI_BOT_W / 2.0f, 126, 0.5f, GUI_COL_TEXT);
		guiTextCentered("A: Relaunch   B: Back", GUI_BOT_W / 2.0f, 168, 0.45f,
			GUI_COL_MUTED);
		break;
#endif

	case UPD_MESSAGE: {
		/* Centre the block vertically so multi-line notices stay in panel. */
		int lines = 1;
		for (const char* p = s_up.message; *p; ++p) {
			if (*p == '\n')
				lines++;
		}
		float y = 110.0f - (lines - 1) * (UPD_LINE_H / 2.0f);
		if (y < 74.0f)
			y = 74.0f;
		drawMessage(s_up.message, GUI_BOT_W / 2.0f, y, 0.5f, GUI_COL_TEXT);
		guiTextCentered("B: OK", GUI_BOT_W / 2.0f, 168, 0.45f, GUI_COL_MUTED);
		break;
	}

	default:
		break;
	}
}
