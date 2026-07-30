#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>

#include "app.h"

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);

	appInit();

	while (aptMainLoop())
	{
		hidScanInput();

		appUpdate();
		appRender();

		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();

		if (g_app.kDown & KEY_START)
			break;
	}

	appExit();
	gfxExit();
	return 0;
}
