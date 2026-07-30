#ifndef UI_PLAYER_H
#define UI_PLAYER_H

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

#endif
