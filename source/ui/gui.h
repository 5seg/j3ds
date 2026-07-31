#ifndef UI_GUI_H
#define UI_GUI_H

#include <stdbool.h>
#include <3ds.h>
#include <citro2d.h>

#define GUI_TOP_W   400
#define GUI_TOP_H   240
#define GUI_BOT_W   320
#define GUI_BOT_H   240

#define GUI_DEPTH   0.5f

/* Jellyfin-inspired palette */
#define GUI_COL_BG      C2D_Color32(0x10, 0x12, 0x18, 0xFF)
#define GUI_COL_PANEL   C2D_Color32(0x1E, 0x22, 0x2B, 0xFF)
#define GUI_COL_PANEL2  C2D_Color32(0x16, 0x1A, 0x22, 0xFF)
#define GUI_COL_HEADER  C2D_Color32(0x14, 0x18, 0x20, 0xFF)
#define GUI_COL_SELECT  C2D_Color32(0xAA, 0x5C, 0xC3, 0xFF)
#define GUI_COL_SELECT2 C2D_Color32(0x33, 0x3C, 0x4A, 0xFF)
#define GUI_COL_TEXT    C2D_Color32(0xE8, 0xE8, 0xEF, 0xFF)
#define GUI_COL_MUTED   C2D_Color32(0x9A, 0x9F, 0xAB, 0xFF)
#define GUI_COL_DIM     C2D_Color32(0x5C, 0x60, 0x6C, 0xFF)
#define GUI_COL_ACCENT  C2D_Color32(0x7C, 0x3A, 0xED, 0xFF)
#define GUI_COL_BAD     C2D_Color32(0xC8, 0x4B, 0x4B, 0xFF)
#define GUI_COL_GOOD    C2D_Color32(0x4B, 0xC8, 0x6B, 0xFF)

typedef struct {
	C2D_Font font;
	C2D_TextBuf buf;
	C3D_RenderTarget* top;
	C3D_RenderTarget* bottom;
} GuiState;

extern GuiState g_gui;

bool guiInit(void);
void guiExit(void);

void guiBeginFrame(void);
void guiEndFrame(void);

/* Text: text is parsed from the dynamic buffer every frame. */
void guiText(const char* text, float x, float y, float scale, u32 color);
void guiTextCentered(const char* text, float x, float y, float scale, u32 color);
void guiTextRight(const char* text, float x, float y, float scale, u32 color);
void guiTextSize(const char* text, float scale, float* outW, float* outH);

/* Primitives (must be called inside a C2D scene). */
void guiRect(float x, float y, float w, float h, u32 color);
void guiRectGradient(float x, float y, float w, float h, u32 topColor, u32 bottomColor);
void guiPanel(float x, float y, float w, float h);
void guiPanelHighlight(float x, float y, float w, float h);
void guiLine(float x0, float y0, float x1, float y1, u32 color);
void guiCircle(float cx, float cy, float r, u32 color);

/* Buttons: touch hit-testing helper (gui3dslib-style). */
typedef struct {
	float x;
	float y;
	float w;
	float h;
} GuiRect;

bool guiButtonHit(const GuiRect* r, const touchPosition* touch, u32 kDown);
void guiButton(float x, float y, float w, float h, const char* label, bool selected);

#endif
