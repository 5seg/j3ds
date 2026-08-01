#include "app.h"

#include <stdio.h>
#include <string.h>
#include <3ds.h>
#include <citro2d.h>

#include "ui/gui.h"
#include "ui/screens.h"
#include "ui/browser.h"
#include "ui/ui_player.h"
#include "audio/audio_player.h"
#include "storage/config.h"
#include "net/http.h"
#include "ui/thumbnail.h"

AppContext g_app;

void appInit(void)
{
	memset(&g_app, 0, sizeof(g_app));
	g_app.current = SCREEN_HOME;
	g_app.previous = SCREEN_HOME;
	g_app.running = true;

	configLoad(&g_app.config);
	/* A saved API key remains a valid legacy login until password auth runs. */
	strncpy(g_app.authToken, g_app.config.apiKey, sizeof(g_app.authToken) - 1);
	g_app.authToken[sizeof(g_app.authToken) - 1] = '\0';

	httpGlobalInit();
	audioInit();

	browserInit();
	playerInit();
	screenInit();
}

void appUpdate(void)
{
	g_app.kDown = hidKeysDown();
	g_app.kHeld = hidKeysHeld();
	touchPosition touch;
	hidTouchRead(&touch);
	g_app.touch = touch;
	g_app.touchDown = (g_app.kDown & KEY_TOUCH) != 0;

	if (g_app.current == SCREEN_HOME) {
		if (g_app.kDown & KEY_START) {
			g_app.running = false;
			return;
		}
		if (g_app.kDown & KEY_A) {
			screenChange(SCREEN_BROWSER);
			browserLoadRoot();
		}
		if (g_app.kDown & KEY_B)
			screenChange(SCREEN_PLAYER);
		if (g_app.kDown & KEY_SELECT)
			screenChange(SCREEN_SETTINGS);
		if (g_app.touchDown) {
			if (touch.px < 160 && touch.py >= 35 && touch.py < 125) {
				screenChange(SCREEN_BROWSER);
				browserLoadRoot();
			} else if (touch.px >= 160 && touch.py >= 35 && touch.py < 125) {
				screenChange(SCREEN_SETTINGS);
			}
		}
	} else {
		screenUpdate();
	}
}

const char* appAuthToken(void)
{
	return g_app.authToken[0] ? g_app.authToken : g_app.config.apiKey;
}

void appRender(void)
{
	/* Top screen: branded backdrop / album art. */
	C2D_TargetClear(g_gui.top, GUI_COL_BG);
	C2D_SceneBegin(g_gui.top);

	guiRectGradient(0, 0, GUI_TOP_W, GUI_TOP_H, C2D_Color32(0x1A, 0x1E, 0x28, 0xFF),
		GUI_COL_BG);
	if (g_app.current == SCREEN_PLAYER)
		playerRenderTop();
	else
		screenRenderTop();

	/* Bottom screen: interactive UI. */
	C2D_TargetClear(g_gui.bottom, GUI_COL_BG);
	C2D_SceneBegin(g_gui.bottom);
	screenRender();
}

void appExit(void)
{
	configSave(&g_app.config);
	thumbnailReleaseCache();
	httpGlobalExit();
	audioExit();
}
