#include "utils/debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <3ds.h>
#include <citro2d.h>

#include "app.h"
#include "sys/sd.h"
#include "ui/gui.h"

#define DEBUG_LOG_PATH "cache/debug.log"

#define DEBUG_LINE_MAX  12
#define DEBUG_LOGBUF    256

static bool s_overlay = false;
static char s_lines[DEBUG_LINE_MAX][DEBUG_LOGBUF];
static int s_lineCount = 0;

typedef struct {
	int httpStatus;
	int srcW;
	int srcH;
	int texW;
	int texH;
	float subLeft;
	float subTop;
	float subRight;
	float subBottom;
	char state[32];
} ThumbDebug;

static ThumbDebug s_thumb;

static void debugEnsureDir(void)
{
	struct stat st;
	if (stat("sdmc:/cache", &st) != 0)
		mkdir("sdmc:/cache", 0777);
}

static void debugPushLine(const char* line)
{
	if (s_lineCount < DEBUG_LINE_MAX) {
		strncpy(s_lines[s_lineCount], line, DEBUG_LOGBUF - 1);
		s_lines[s_lineCount][DEBUG_LOGBUF - 1] = '\0';
		s_lineCount++;
	} else {
		for (int i = 1; i < DEBUG_LINE_MAX; ++i)
			strncpy(s_lines[i - 1], s_lines[i], DEBUG_LOGBUF);
		strncpy(s_lines[DEBUG_LINE_MAX - 1], line, DEBUG_LOGBUF - 1);
		s_lines[DEBUG_LINE_MAX - 1][DEBUG_LOGBUF - 1] = '\0';
	}
}

void debugInit(void)
{
	memset(&s_thumb, 0, sizeof(s_thumb));
	s_overlay = false;
	s_lineCount = 0;
	debugEnsureDir();
	debugLog("=== j3ds debug log v" APP_VERSION " ===");
}

void debugExit(void)
{
}

void debugLog(const char* fmt, ...)
{
	char line[DEBUG_LOGBUF];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);

	debugPushLine(line);

	FILE* f = fopen(DEBUG_LOG_PATH, "a");
	if (f) {
		fprintf(f, "%s\n", line);
		fclose(f);
	}
}

void debugSetThumbInfo(int httpStatus, int srcW, int srcH, int texW, int texH,
	float subLeft, float subTop, float subRight, float subBottom,
	const char* state)
{
	s_thumb.httpStatus = httpStatus;
	s_thumb.srcW = srcW;
	s_thumb.srcH = srcH;
	s_thumb.texW = texW;
	s_thumb.texH = texH;
	s_thumb.subLeft = subLeft;
	s_thumb.subTop = subTop;
	s_thumb.subRight = subRight;
	s_thumb.subBottom = subBottom;
	strncpy(s_thumb.state, state, sizeof(s_thumb.state) - 1);
	s_thumb.state[sizeof(s_thumb.state) - 1] = '\0';
}

void debugToggle(void)
{
	s_overlay = !s_overlay;
}

bool debugEnabled(void)
{
	return s_overlay;
}

void debugRenderOverlay(void)
{
	if (!s_overlay)
		return;

	guiRect(4, 4, GUI_TOP_W - 8, GUI_TOP_H - 8, C2D_Color32(0, 0, 0, 0xA0));

	char buf[128];
	snprintf(buf, sizeof(buf), "SRC %dx%d  TEX %dx%d  HTTP %d",
		s_thumb.srcW, s_thumb.srcH, s_thumb.texW, s_thumb.texH,
		s_thumb.httpStatus);
	guiText(buf, 10, 8, 0.5f, GUI_COL_ACCENT);

	snprintf(buf, sizeof(buf), "SUB L%.3f T%.3f R%.3f B%.3f  [%s]",
		s_thumb.subLeft, s_thumb.subTop, s_thumb.subRight, s_thumb.subBottom,
		s_thumb.state[0] ? s_thumb.state : "?");
	guiText(buf, 10, 22, 0.5f, GUI_COL_ACCENT);

	float y = 40;
	for (int i = 0; i < s_lineCount; ++i)
		guiText(s_lines[i], 10, y + i * 14, 0.45f, GUI_COL_TEXT);
}
