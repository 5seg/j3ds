#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <3ds.h>

#include "storage/config.h"

typedef enum {
	SCREEN_HOME,
	SCREEN_SETTINGS,
	SCREEN_BROWSER,
	SCREEN_PLAYER
} AppState;

typedef struct {
	AppState current;
	AppState previous;
	bool running;
	u32 kDown;
	u32 kHeld;
	Config config;
} AppContext;

extern AppContext g_app;

void appInit(void);
void appUpdate(void);
void appRender(void);
void appExit(void);

#endif
