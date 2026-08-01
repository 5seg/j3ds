#include "ui/gui.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

GuiState g_gui;

#define GUI_DYNBUF_GLYPHS 8192

bool guiInit(void)
{
	if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
		return false;
	if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
		C3D_Fini();
		return false;
	}
	C2D_Prepare();

	g_gui.top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	g_gui.bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	if (!g_gui.top || !g_gui.bottom) {
		C2D_Fini();
		C3D_Fini();
		return false;
	}

	g_gui.font = C2D_FontLoadSystem(CFG_REGION_USA);
	if (!g_gui.font) {
		C2D_Fini();
		C3D_Fini();
		return false;
	}

	g_gui.buf = C2D_TextBufNew(GUI_DYNBUF_GLYPHS);
	if (!g_gui.buf) {
		C2D_FontFree(g_gui.font);
		C2D_Fini();
		C3D_Fini();
		return false;
	}

	return true;
}

void guiExit(void)
{
	if (g_gui.buf)
		C2D_TextBufDelete(g_gui.buf);
	if (g_gui.font)
		C2D_FontFree(g_gui.font);
	C2D_Fini();
	C3D_Fini();
	memset(&g_gui, 0, sizeof(g_gui));
}

void guiBeginFrame(void)
{
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	C2D_TextBufClear(g_gui.buf);
}

void guiEndFrame(void)
{
	C3D_FrameEnd(0);
}

static void guiDrawTextFlags(const char* text, float x, float y, float scale, u32 color, u32 flags)
{
	if (!text || !text[0])
		return;

	C2D_Text c2dText;
	const char* end = C2D_TextFontParse(&c2dText, g_gui.font, g_gui.buf, text);
	if (!end)
		return;
	C2D_TextOptimize(&c2dText);
	C2D_DrawText(&c2dText, flags | C2D_WithColor, x, y, GUI_DEPTH, scale, scale, color);
}

void guiText(const char* text, float x, float y, float scale, u32 color)
{
	guiDrawTextFlags(text, x, y, scale, color, C2D_AlignLeft);
}

void guiTextCentered(const char* text, float x, float y, float scale, u32 color)
{
	guiDrawTextFlags(text, x, y, scale, color, C2D_AlignCenter);
}

void guiTextRight(const char* text, float x, float y, float scale, u32 color)
{
	guiDrawTextFlags(text, x, y, scale, color, C2D_AlignRight);
}

void guiTextSize(const char* text, float scale, float* outW, float* outH)
{
	if (outW)
		*outW = 0.0f;
	if (outH)
		*outH = 0.0f;
	if (!text || !text[0])
		return;

	C2D_Text c2dText;
	if (!C2D_TextFontParse(&c2dText, g_gui.font, g_gui.buf, text))
		return;
	C2D_TextGetDimensions(&c2dText, scale, scale, outW, outH);
}

void guiRect(float x, float y, float w, float h, u32 color)
{
	C2D_DrawRectSolid(x, y, GUI_DEPTH, w, h, color);
}

void guiRectGradient(float x, float y, float w, float h, u32 topColor, u32 bottomColor)
{
	C2D_DrawRectangle(x, y, GUI_DEPTH, w, h, topColor, topColor, bottomColor, bottomColor);
}

void guiPanel(float x, float y, float w, float h)
{
	guiRect(x, y, w, h, GUI_COL_PANEL);
	guiRect(x, y + h - 1, w, 1, GUI_COL_ACCENT);
}

void guiPanelHighlight(float x, float y, float w, float h)
{
	guiRect(x, y, w, h, GUI_COL_SELECT2);
	guiRect(x, y, 3, h, GUI_COL_SELECT);
}

void guiLine(float x0, float y0, float x1, float y1, u32 color)
{
	C2D_DrawLine(x0, y0, color, x1, y1, color, 1.0f, GUI_DEPTH);
}

void guiCircle(float cx, float cy, float r, u32 color)
{
	C2D_DrawCircleSolid(cx, cy, GUI_DEPTH, r, color);
}

bool guiButtonHit(const GuiRect* r, const touchPosition* touch, u32 kDown)
{
	if (!r || !touch)
		return false;
	if (!(kDown & KEY_TOUCH))
		return false;
	return (touch->px >= r->x && touch->px < r->x + r->w &&
		touch->py >= r->y && touch->py < r->y + r->h);
}

void guiButton(float x, float y, float w, float h, const char* label, bool selected)
{
	if (selected)
		guiPanelHighlight(x, y, w, h);
	else
		guiRect(x, y, w, h, GUI_COL_PANEL2);

	float tw, th;
	guiTextSize(label, 0.55f, &tw, &th);
	guiText(label, x + (w - tw) / 2.0f, y + (h - th) / 2.0f, 0.55f,
		selected ? GUI_COL_TEXT : GUI_COL_MUTED);
}
