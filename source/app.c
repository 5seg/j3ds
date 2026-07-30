#include "app.h"

#include <stdio.h>
#include <string.h>
#include <3ds.h>

#include "ui/screens.h"
#include "ui/browser.h"
#include "ui/ui_player.h"
#include "audio/audio_player.h"
#include "storage/config.h"
#include "net/http.h"

AppContext g_app;

void appInit(void)
{
	memset(&g_app, 0, sizeof(g_app));
	g_app.current = SCREEN_HOME;
	g_app.previous = SCREEN_HOME;
	g_app.running = true;

	configLoad(&g_app.config);

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

	if (g_app.kDown & KEY_START) {
		g_app.running = false;
		return;
	}

	if (g_app.current == SCREEN_HOME) {
		if (g_app.kDown & KEY_A) {
			screenChange(SCREEN_BROWSER);
			browserLoadRoot();
		}
		if (g_app.kDown & KEY_B)
			screenChange(SCREEN_PLAYER);
		if (g_app.kDown & KEY_SELECT)
			screenChange(SCREEN_SETTINGS);
	} else {
		screenUpdate();
	}
}

void appRender(void)
{
	consoleClear();

	if (g_app.current == SCREEN_PLAYER)
		playerRenderTop();

	printf("Jellyfin 3DS\n\n");
	screenRender();
}

void appExit(void)
{
	configSave(&g_app.config);
	httpGlobalExit();
	audioExit();
}
