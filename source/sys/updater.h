#ifndef SYS_UPDATER_H
#define SYS_UPDATER_H

#include <3ds.h>
#include <stdbool.h>

typedef enum {
	UPD_IDLE = 0,
	UPD_CHECKING,
	UPD_CONFIRM,     /* newer version found, awaiting user choice */
	UPD_DOWNLOADING,
	UPD_INSTALLING,
	UPD_DONE,        /* installed, awaiting relaunch choice */
	UPD_MESSAGE      /* info/error, B dismisses */
} UpdaterState;

void updaterInit(void);
void updaterExit(void);

bool updaterActive(void);
void updaterCheck(void);
void updaterUpdate(void);
void updaterRenderBottom(void);

#endif
