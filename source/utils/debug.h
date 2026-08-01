#ifndef UTILS_DEBUG_H
#define UTILS_DEBUG_H

#include <stdbool.h>

/* On-screen debug overlay + SD-card log used to diagnose album-art noise /
   instability. Toggle the overlay from the player screen with SELECT. */

void debugInit(void);
void debugExit(void);

/* Append a timestamped line to cache/debug.log. Cheap; safe to call from the
   render thread. */
void debugLog(const char* fmt, ...);

/* Thumbnail diagnostics for the overlay. */
void debugSetThumbInfo(int httpStatus, int srcW, int srcH, int texW, int texH,
	float subLeft, float subTop, float subRight, float subBottom,
	const char* state);

void debugToggle(void);
bool debugEnabled(void);

/* Render the overlay on the top screen. Call after other top-screen drawing. */
void debugRenderOverlay(void);

#endif
