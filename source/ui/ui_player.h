#ifndef UI_PLAYER_H
#define UI_PLAYER_H

#include <3ds.h>
#include "ui/browser.h"

typedef struct {
    char title[128];
    char artist[128];
    char album[128];
    char itemId[64];
    char thumbnailUrl[512];
} CurrentTrack;

void playerInit(void);
void playerRender(void);
void playerUpdate(void);
void playerRenderTop(void);
void playerSetTrack(const char* title, const char* artist, const char* album, const char* itemId);

/* Set the play queue (the song list the current track belongs to).
   Copies the items, so the caller may reuse its list afterwards. */
void playerSetQueue(const BrowserItem* items, int count, int index);

/* Play the song at the given queue index. Sets track info, builds the
   stream URL and starts playback. */
Result playerPlaySongAt(int index);

#endif
