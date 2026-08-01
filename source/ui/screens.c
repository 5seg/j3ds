#include "ui/screens.h"

#include <stdio.h>
#include <string.h>
#include <citro2d.h>

#include "ui/gui.h"
#include "ui/browser.h"
#include "ui/ui_player.h"
#include "ui/input.h"
#include "storage/config.h"
#include "net/http.h"
#include "net/jellyfin.h"
#include "app.h"

static AppState s_stack[8];
static int s_top = 0;

typedef enum {
	SET_SERVER_URL,
	SET_USERNAME,
	SET_PASSWORD,
	SET_API_KEY,
	SET_DISABLE_SSL,
	SET_COUNT
} SettingItem;

static int s_settingSel = 0;
static char s_status[128] = "";

#define HEADER_H   30
#define HINT_H     34
#define ROW_H      26
#define ROW_GAP    4

void screenInit(void)
{
	memset(s_stack, 0, sizeof(s_stack));
	s_top = 0;
	s_stack[0] = SCREEN_HOME;
	s_settingSel = 0;
	s_status[0] = '\0';
}

void screenPush(AppState state)
{
	if (s_top < 7) {
		s_top++;
		s_stack[s_top] = state;
	}
	g_app.current = state;
}

void screenPop(void)
{
	if (s_top > 0)
		s_top--;
	g_app.current = s_stack[s_top];
}

void screenChange(AppState state)
{
	s_stack[s_top] = state;
	g_app.previous = g_app.current;
	g_app.current = state;
}

static void settingsTestConnection(void)
{
	if (g_app.config.serverUrl[0] == '\0') {
		strncpy(s_status, "No server URL", sizeof(s_status) - 1);
		s_status[sizeof(s_status) - 1] = '\0';
		return;
	}

	JellyfinServerInfo info;
	Result res = jellyfinServerInfo(g_app.config.serverUrl, &info);
	if (R_SUCCEEDED(res)) {
		snprintf(s_status, sizeof(s_status), "%s / %s",
			info.serverName[0] ? info.serverName : "Jellyfin",
			info.version[0] ? info.version : "?");
	} else if (res == HTTP_ERR_STATUS) {
		snprintf(s_status, sizeof(s_status), "Connection failed: HTTP %d", httpLastStatus());
	} else {
		snprintf(s_status, sizeof(s_status), "Connection failed: %08lX", (unsigned long)res);
	}
	s_status[sizeof(s_status) - 1] = '\0';
}

static void settingsEdit(void)
{
	char buf[256];
	switch (s_settingSel) {
	case SET_SERVER_URL:
		strncpy(buf, g_app.config.serverUrl, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		if (inputShowKeyboard(buf, sizeof(buf), "Server URL", buf)) {
			strncpy(g_app.config.serverUrl, buf, CONFIG_MAX_URL - 1);
			g_app.config.serverUrl[CONFIG_MAX_URL - 1] = '\0';
			s_status[0] = '\0';
		}
		break;
	case SET_USERNAME:
		strncpy(buf, g_app.config.username, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		if (inputShowKeyboard(buf, sizeof(buf), "Username", buf)) {
			strncpy(g_app.config.username, buf, CONFIG_MAX_USER - 1);
			g_app.config.username[CONFIG_MAX_USER - 1] = '\0';
			s_status[0] = '\0';
		}
		break;
	case SET_PASSWORD:
		strncpy(buf, g_app.config.password, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		if (inputShowKeyboardPassword(buf, sizeof(buf), "Password")) {
			strncpy(g_app.config.password, buf, CONFIG_MAX_PASSWORD - 1);
			g_app.config.password[CONFIG_MAX_PASSWORD - 1] = '\0';
			s_status[0] = '\0';
		}
		memset(buf, 0, sizeof(buf));
		break;
	case SET_API_KEY:
		if (inputShowKeyboardPassword(buf, sizeof(buf), "API Key")) {
			strncpy(g_app.config.apiKey, buf, CONFIG_MAX_KEY - 1);
			g_app.config.apiKey[CONFIG_MAX_KEY - 1] = '\0';
			s_status[0] = '\0';
		}
		break;
	case SET_DISABLE_SSL:
		g_app.config.disableSslVerify = !g_app.config.disableSslVerify;
		s_status[0] = '\0';
		break;
	default:
		break;
	}
}

void screenUpdate(void)
{
	switch (g_app.current) {
	case SCREEN_SETTINGS:
		if (g_app.kDown & KEY_UP) {
			s_settingSel--;
			if (s_settingSel < 0)
				s_settingSel = SET_COUNT - 1;
		}

		if (g_app.kDown & KEY_DOWN) {
			s_settingSel++;
			if (s_settingSel >= SET_COUNT)
				s_settingSel = 0;
		}

		if (g_app.kDown & KEY_A)
			settingsEdit();

		if (g_app.touchDown) {
			float rowY = HEADER_H + 6;
			for (int i = 0; i < SET_COUNT; ++i) {
				float ry = rowY + i * (ROW_H + ROW_GAP);
				if (g_app.touch.py >= ry && g_app.touch.py < ry + ROW_H) {
					s_settingSel = i;
					settingsEdit();
					break;
				}
			}
		}

		if (g_app.kDown & KEY_X)
			settingsTestConnection();

		if (g_app.kDown & KEY_START) {
			if (configSave(&g_app.config))
				strncpy(s_status, "Saved.", sizeof(s_status) - 1);
			else
				strncpy(s_status, "Save failed!", sizeof(s_status) - 1);
			s_status[sizeof(s_status) - 1] = '\0';
		}

		if (g_app.kDown & KEY_B)
			screenChange(SCREEN_HOME);
		break;

	case SCREEN_BROWSER:
		browserUpdate();
		break;

	case SCREEN_PLAYER:
		playerUpdate();
		break;

	default:
		break;
	}
}

static void screenRenderHeader(const char* title, u32 accent)
{
	guiRect(0, 0, GUI_BOT_W, HEADER_H, GUI_COL_HEADER);
	guiRect(0, HEADER_H - 2, GUI_BOT_W, 2, accent);
	guiText(title, 10, 6, 0.6f, GUI_COL_TEXT);
}

static void screenRenderHints(const char* left, const char* mid, const char* right)
{
	float y = GUI_BOT_H - HINT_H;
	guiRect(0, y, GUI_BOT_W, HINT_H, GUI_COL_HEADER);
	guiRect(0, y, GUI_BOT_W, 1, GUI_COL_DIM);

	if (left)
		guiText(left, 8, y + 8, 0.45f, GUI_COL_MUTED);
	if (mid)
		guiTextCentered(mid, GUI_BOT_W / 2.0f, y + 8, 0.45f, GUI_COL_MUTED);
	if (right)
		guiTextRight(right, GUI_BOT_W - 8, y + 8, 0.45f, GUI_COL_MUTED);
}

static void screenRenderHome(void)
{
	screenRenderHeader("JELLYFIN 3DS", GUI_COL_SELECT);

	/* Library card. */
	float cardW = (GUI_BOT_W - 30) / 2.0f;
	guiPanel(10, HEADER_H + 24, cardW, 96);
	guiTextCentered("LIBRARY", 10 + cardW / 2.0f, HEADER_H + 40, 0.7f, GUI_COL_TEXT);
	guiTextCentered("Browse music", 10 + cardW / 2.0f, HEADER_H + 74, 0.45f, GUI_COL_MUTED);
	guiTextCentered("[A]", 10 + cardW / 2.0f, HEADER_H + 96, 0.45f, GUI_COL_DIM);

	/* Setup card. */
	float sx = 20 + cardW;
	guiPanel(sx, HEADER_H + 24, cardW, 96);
	guiTextCentered("SETUP", sx + cardW / 2.0f, HEADER_H + 40, 0.7f, GUI_COL_TEXT);
	guiTextCentered("Server & login", sx + cardW / 2.0f, HEADER_H + 74, 0.45f, GUI_COL_MUTED);
	guiTextCentered("[SELECT]", sx + cardW / 2.0f, HEADER_H + 96, 0.45f, GUI_COL_DIM);

	/* Player card. Leave a clear gap above the hint bar. */
	guiPanel(10, HEADER_H + 124, GUI_BOT_W - 20, 44);
	guiTextCentered("PLAYER", GUI_BOT_W / 2.0f, HEADER_H + 132, 0.6f, GUI_COL_TEXT);
	guiTextCentered("Now playing  [B]", GUI_BOT_W / 2.0f, HEADER_H + 152, 0.45f, GUI_COL_MUTED);

	screenRenderHints("A: Library", "SELECT: Setup", "START: Quit");
}

static void screenRenderSettings(void)
{
	screenRenderHeader("SETUP & LOGIN", GUI_COL_ACCENT);

	const char* labels[SET_COUNT] = {
		"Server URL", "Username", "Password", "API key", "SSL verify"
	};

	const char* values[SET_COUNT] = {
		g_app.config.serverUrl,
		g_app.config.username,
		g_app.config.password[0] ? "********" : "(not set)",
		g_app.config.apiKey[0] ? "******** (legacy)" : "(empty)",
		g_app.config.disableSslVerify ? "disabled" : "enabled"
	};

	float rowY = HEADER_H + 6;
	for (int i = 0; i < SET_COUNT; ++i) {
		float ry = rowY + i * (ROW_H + ROW_GAP);
		if (i == s_settingSel)
			guiPanelHighlight(8, ry, GUI_BOT_W - 16, ROW_H);
		else
			guiRect(8, ry, GUI_BOT_W - 16, ROW_H, GUI_COL_PANEL);

		guiText(labels[i], 16, ry + 5, 0.45f, GUI_COL_MUTED);

		/* Value on the right, truncated. */
		char tmp[40];
		strncpy(tmp, values[i], sizeof(tmp) - 1);
		tmp[sizeof(tmp) - 1] = '\0';
		guiTextRight(tmp, GUI_BOT_W - 16, ry + 5, 0.45f, GUI_COL_TEXT);
	}

	if (s_status[0])
		guiText(s_status, 10, rowY + SET_COUNT * (ROW_H + ROW_GAP) + 2, 0.45f,
			GUI_COL_TEXT);

	screenRenderHints("X: Test", "START: Save  B: Back", "Touch row to edit");
}

void screenRender(void)
{
	switch (g_app.current) {
	case SCREEN_HOME:
		screenRenderHome();
		break;
	case SCREEN_SETTINGS:
		screenRenderSettings();
		break;
	case SCREEN_BROWSER:
		browserRender();
		break;
	case SCREEN_PLAYER:
		playerRender();
		break;
	default:
		break;
	}
}

/* A simple branded backdrop for the top screen. */
void screenRenderTop(void)
{
	if (g_app.current == SCREEN_BROWSER) {
		browserRenderTop();
		return;
	}

	guiCircle(GUI_TOP_W / 2.0f, 88, 54, C2D_Color32(0x2A, 0x2E, 0x3A, 0xFF));
	guiCircle(GUI_TOP_W / 2.0f, 88, 46, C2D_Color32(0xAA, 0x5C, 0xC3, 0xFF));
	guiCircle(GUI_TOP_W / 2.0f, 88, 32, C2D_Color32(0x1A, 0x1E, 0x28, 0xFF));
	guiTextCentered("Jellyfin 3DS", GUI_TOP_W / 2.0f, 168, 0.9f, GUI_COL_TEXT);
	guiTextCentered("Your music, on your 3DS", GUI_TOP_W / 2.0f, 198, 0.5f,
		GUI_COL_MUTED);
	guiTextRight("v" APP_VERSION, GUI_TOP_W - 8, GUI_TOP_H - 14, 0.4f, GUI_COL_DIM);
}
