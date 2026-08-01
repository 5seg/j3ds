#include "ui/ui_player.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <3ds.h>
#include <citro2d.h>

#include "app.h"
#include "audio/audio_player.h"
#include "net/jellyfin.h"
#include "sys/sd.h"
#include "ui/gui.h"
#include "ui/screens.h"
#include "ui/thumbnail.h"
#include "utils/debug.h"

#define PLAYER_THUMB_X      80
#define PLAYER_THUMB_Y      24
#define PLAYER_THUMB_SIZE   240

static CurrentTrack s_track;
static Thumbnail s_thumb;
static bool s_thumbReady = false;

static BrowserItem s_queue[BROWSER_MAX_ITEMS];
static int s_queueCount = 0;
static int s_queueIndex = 0;

#define PLAYER_STREAM_URL_MAX 2048

static void playerBuildAudioPath(const char* itemId, char* out, size_t outLen)
{
    char root[256];
    sdPath(root, sizeof(root), "");
    snprintf(out, outLen, "%s/cache/audio/%s.mp3", root, itemId ? itemId : "");
}

void playerInit(void)
{
    memset(&s_track, 0, sizeof(s_track));
    memset(&s_thumb, 0, sizeof(s_thumb));
    s_thumbReady = false;
    strncpy(s_track.title, "Test Track", sizeof(s_track.title) - 1);
    strncpy(s_track.artist, "Test Artist", sizeof(s_track.artist) - 1);
    strncpy(s_track.album, "Test Album", sizeof(s_track.album) - 1);
}

void playerSetTrack(const char* title, const char* artist, const char* album,
	const char* itemId, const char* artItemId)
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

    /* Songs usually have no image of their own; the cover lives on the
       album, so request the album's Primary image when we know it. */
    s_track.thumbnailUrl[0] = '\0';
    if (artItemId && artItemId[0] != '\0' && g_app.config.serverUrl[0] != '\0') {
        jellyfinGetThumbnailUrl(g_app.config.serverUrl, appAuthToken(), artItemId,
            s_track.thumbnailUrl, sizeof(s_track.thumbnailUrl));
    }

    s_thumbReady = false;
}

void playerSetQueue(const BrowserItem* items, int count, int index)
{
    char target[64] = "";
    if (items && index >= 0 && index < count)
        snprintf(target, sizeof(target), "%s", items[index].id);

    s_queueCount = 0;
    s_queueIndex = 0;

    if (!items || count <= 0)
        return;

    if (count > BROWSER_MAX_ITEMS)
        count = BROWSER_MAX_ITEMS;

    for (int i = 0; i < count; ++i) {
        if (items[i].type != ITEM_TYPE_SONG)
            continue;
        s_queue[s_queueCount] = items[i];
        if (target[0] && strcmp(s_queue[s_queueCount].id, target) == 0)
            s_queueIndex = s_queueCount;
        s_queueCount++;
    }
}

int playerQueueIndex(void)
{
    return s_queueIndex;
}

/* Fetch and upload art before opening the long-lived audio HTTP connection. */
static void playerLoadThumb(void)
{
    if (s_track.thumbnailUrl[0] == '\0')
        return;

    s_thumbReady = thumbnailLoad(s_track.thumbnailUrl, &s_thumb);
}

Result playerPlaySongAt(int index)
{
    if (index < 0 || index >= s_queueCount)
        return -1;

    const BrowserItem* song = &s_queue[index];

    char url[PLAYER_STREAM_URL_MAX];
    Result res = jellyfinGetStreamUrl(g_app.config.serverUrl, appAuthToken(),
        song->id, url, sizeof(url));
    if (R_FAILED(res))
        return res;

    s_queueIndex = index;
    playerSetTrack(song->name, song->artist, song->album, song->id,
        song->albumId[0] ? song->albumId : song->id);

    /* Stop any active stream first so the httpc service is idle, then fetch
       and upload the art, then reopen the stream. Never let the two overlap. */
    audioStop();
    playerLoadThumb();

    return audioPlayStream(url);
}

void playerUpdate(void)
{
	if (g_app.touchDown && g_app.touch.py >= 185) {
		if (g_app.touch.px < 105)
			g_app.kDown |= KEY_B;
		else if (g_app.touch.px < 215)
			g_app.kDown |= KEY_X;
		else
			g_app.kDown |= KEY_Y;
	}

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

    if (g_app.kDown & KEY_SELECT)
        debugToggle();

    if (s_queueCount > 1) {
        if (g_app.kDown & KEY_L)
            playerPlaySongAt((s_queueIndex + s_queueCount - 1) % s_queueCount);
        if (g_app.kDown & KEY_R)
            playerPlaySongAt((s_queueIndex + 1) % s_queueCount);
    }

    if (g_app.kDown & KEY_B) {
        audioStop();
        screenChange(g_app.previous == SCREEN_BROWSER ? SCREEN_BROWSER : SCREEN_HOME);
    }
}

void playerRenderTop(void)
{
    if (s_track.thumbnailUrl[0] == '\0')
        return;

    if (!s_thumbReady || !s_thumb.valid)
        return;

    float scale = (float)PLAYER_THUMB_SIZE / (float)s_thumb.width;
    float w = (float)s_thumb.width * scale;
    float h = (float)s_thumb.height * scale;
    float x = PLAYER_THUMB_X + (PLAYER_THUMB_SIZE - w) / 2.0f;
    float y = PLAYER_THUMB_Y + (PLAYER_THUMB_SIZE - h) / 2.0f;
    C2D_DrawImageAt(s_thumb.image, x, y, GUI_DEPTH, NULL, scale, scale);
}

void playerRender(void)
{
    /* Header. */
    guiRect(0, 0, GUI_BOT_W, 30, GUI_COL_HEADER);
    guiRect(0, 28, GUI_BOT_W, 2, GUI_COL_SELECT);
    guiText("NOW PLAYING", 10, 6, 0.6f, GUI_COL_TEXT);
    if (s_queueCount > 1) {
        char pos[32];
        snprintf(pos, sizeof(pos), "%d/%d", s_queueIndex + 1, s_queueCount);
        guiTextRight(pos, GUI_BOT_W - 10, 9, 0.45f, GUI_COL_MUTED);
    }

    /* Status pill. */
    const char* status = audioIsPaused() ? "PAUSED"
        : audioIsPlaying() ? "PLAYING" : "STOPPED";
    u32 statusCol = audioIsPlaying() ? GUI_COL_GOOD
        : audioIsPaused() ? GUI_COL_ACCENT : GUI_COL_DIM;
    guiPanel(10, 40, 100, 22);
    guiTextCentered(status, 60, 45, 0.45f, statusCol);

    /* Track info. */
    guiText(s_track.title, 12, 78, 0.6f, GUI_COL_TEXT);
    guiText(s_track.artist, 12, 108, 0.45f, GUI_COL_MUTED);
    guiText(s_track.album, 12, 128, 0.45f, GUI_COL_MUTED);

    /* Controls. */
    guiButton(8, 170, 96, 36, "BACK  [B]", false);
    guiButton(112, 170, 96, 36, "PLAY  [X]", true);
    guiButton(216, 170, 96, 36, "PAUSE [Y]", false);

    /* Hint bar. */
    guiRect(0, 210, GUI_BOT_W, 30, GUI_COL_HEADER);
    guiRect(0, 210, GUI_BOT_W, 1, GUI_COL_DIM);
    if (s_queueCount > 1)
        guiTextCentered("L/R: Prev/Next   B: Back   X: Play   Y: Pause",
            GUI_BOT_W / 2.0f, 218, 0.4f, GUI_COL_MUTED);
    else
        guiTextCentered("B: Back   X: Play   Y: Pause", GUI_BOT_W / 2.0f,
            218, 0.4f, GUI_COL_MUTED);
}
