#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include <citro2d.h>

#include "app.h"
#include "ui/gui.h"

int main(int argc, char* argv[])
{
	(void)argc;
	(void)argv;

	gfxInitDefault();

	if (!guiInit()) {
		printf("Failed to initialize Citro2D\n");
		gfxExit();
		return 1;
	}

	appInit();

	while (aptMainLoop())
	{
		hidScanInput();

		appUpdate();
		if (!g_app.running)
			break;

		guiBeginFrame();
		appRender();
		guiEndFrame();
	}

	appExit();
	guiExit();
	gfxExit();
	return 0;
}
