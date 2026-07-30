#include "ui/ui_player.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <3ds.h>

#include "app.h"
#include "audio/audio_player.h"
#include "net/jellyfin.h"
#include "sys/sd.h"
#include "ui/screens.h"
#include "ui/thumbnail.h"
#include "utils/image.h"

#define PLAYER_THUMB_X      0
#define PLAYER_THUMB_Y      80
#define PLAYER_THUMB_SIZE   240

static CurrentTrack s_track;

static void playerBuildAudioPath(const char* itemId, char* out, size_t outLen)
{
    char root[256];
    sdPath(root, sizeof(root), "");
    snprintf(out, outLen, "%s/cache/audio/%s.mp3", root, itemId ? itemId : "");
}

void playerInit(void)
{
    memset(&s_track, 0, sizeof(s_track));
    strncpy(s_track.title, "Test Track", sizeof(s_track.title) - 1);
    strncpy(s_track.artist, "Test Artist", sizeof(s_track.artist) - 1);
    strncpy(s_track.album, "Test Album", sizeof(s_track.album) - 1);
}

void playerSetTrack(const char* title, const char* artist, const char* album, const char* itemId)
{
    memset(&s_track, 0, sizeof(s_track));

    if (title)
        strncpy(s_track.title, title, sizeof(s_track.title) - 1);
    if (artist)
        strncpy(s_track.artist, artist, sizeof(s_track.artist) - 1);
    if (album)
        strncpy(s_track.album, album, sizeof(s_track.album) - 1);
    if (itemId)
        strncpy(s_track.itemId, itemId, sizeof(s_track.itemId) - 1);

    s_track.thumbnailUrl[0] = '\0';
    if (itemId && itemId[0] != '\0' && g_app.config.serverUrl[0] != '\0') {
        jellyfinGetThumbnailUrl(g_app.config.serverUrl, g_app.config.apiKey, itemId,
            s_track.thumbnailUrl, sizeof(s_track.thumbnailUrl));
    }
}

void playerUpdate(void)
{
    if (g_app.kDown & KEY_X) {
        if (s_track.itemId[0] == '\0') {
            printf("No track selected\n");
        } else {
            char path[512];
            playerBuildAudioPath(s_track.itemId, path, sizeof(path));
            if (access(path, F_OK) == 0) {
                Result res = audioPlay(path);
                if (R_FAILED(res))
                    printf("Play failed: %08lX\n", (unsigned long)res);
            } else {
                printf("Track not downloaded yet\n");
            }
        }
    }

    if (g_app.kDown & KEY_Y)
        audioPause();

    if (g_app.kDown & KEY_B) {
        audioStop();
        screenChange(SCREEN_HOME);
    }
}

void playerRenderTop(void)
{
    u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);

    /* Clear the top screen to black. */
    imageDrawRect(fb, 0, 0, 240, 400, 0, 0, 0);

    if (s_track.thumbnailUrl[0] != '\0') {
        thumbnailDraw(s_track.thumbnailUrl, PLAYER_THUMB_X, PLAYER_THUMB_Y,
            PLAYER_THUMB_SIZE, PLAYER_THUMB_SIZE);
    }
}

void playerRender(void)
{
    printf("Player\n\n");
    printf("Status: %s\n", audioIsPaused() ? "Paused"
        : audioIsPlaying() ? "Playing" : "Stopped");
    printf("Title:  %s\n", s_track.title);
    printf("Artist: %s\n", s_track.artist);
    printf("Album:  %s\n", s_track.album);
    printf("\nX: Play downloaded, Y: Pause, B: Back\n");
}
