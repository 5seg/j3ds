#include "ui/browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <3ds.h>
#include <citro2d.h>
#include <jansson.h>

#include "app.h"
#include "audio/audio_player.h"
#include "net/http.h"
#include "net/jellyfin.h"
#include "storage/config.h"
#include "sys/sd.h"
#include "ui/gui.h"
#include "ui/input.h"
#include "ui/ui_player.h"
#include "ui/screens.h"
#include "ui/thumbnail.h"
#include "utils/json.h"

#define BROWSER_VISIBLE     7
#define BROWSER_STACK_MAX   8
#define BROWSER_STATUS_MAX  128
#define BROWSER_STREAM_URL_MAX 2048

#define BROWSER_HEADER_H    30
#define BROWSER_LIST_TOP    48
#define BROWSER_ROW_H       22
#define BROWSER_HINT_Y      (GUI_BOT_H - 34)

typedef struct {
    char id[64];
    char name[BROWSER_MAX_NAME];
    ItemType type;
    int selected;
    int scroll;
} BrowserLevel;

static BrowserState s_state;
static BrowserLevel s_stack[BROWSER_STACK_MAX];
static int s_stackTop = 0;
static char s_status[BROWSER_STATUS_MAX] = "";
static bool s_downloading = false;

static Result browserEnsureAuthenticated(void)
{
    /* Password login is preferred. The stored API key is deliberately kept
       as a legacy fallback for existing installations. */
    if (g_app.config.username[0] != '\0' && g_app.config.password[0] != '\0') {
        char token[CONFIG_MAX_KEY];
        Result res = jellyfinAuthByPassword(g_app.config.serverUrl,
            g_app.config.username, g_app.config.password, token, sizeof(token),
            g_app.userId, sizeof(g_app.userId));
        if (R_FAILED(res))
            return res;
        strncpy(g_app.authToken, token, sizeof(g_app.authToken) - 1);
        g_app.authToken[sizeof(g_app.authToken) - 1] = '\0';
        memset(token, 0, sizeof(token));
        return 0;
    }

    if (g_app.config.apiKey[0] != '\0') {
        strncpy(g_app.authToken, g_app.config.apiKey, sizeof(g_app.authToken) - 1);
        g_app.authToken[sizeof(g_app.authToken) - 1] = '\0';
        return 0;
    }

    if (g_app.config.username[0] == '\0')
        return -1;

    char password[CONFIG_MAX_PASSWORD];
    if (!inputShowKeyboardPassword(password, sizeof(password), "Jellyfin password"))
        return -1;

    Result res = jellyfinAuthByPassword(g_app.config.serverUrl,
        g_app.config.username, password, g_app.authToken, sizeof(g_app.authToken),
        g_app.userId, sizeof(g_app.userId));
    memset(password, 0, sizeof(password));
    return res;
}

static void browserSetStatus(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, args);
    va_end(args);
    s_status[sizeof(s_status) - 1] = '\0';
}

static void browserResetItems(void)
{
    memset(s_state.items, 0, sizeof(s_state.items));
    s_state.count = 0;
    s_state.selected = 0;
    s_state.scroll = 0;
}

static void browserResetStack(void)
{
    s_stackTop = 0;
    memset(s_stack, 0, sizeof(s_stack));
}

static ItemType browserTypeFromString(const char* typeStr)
{
    if (!typeStr)
        return ITEM_TYPE_UNKNOWN;
    if (strcmp(typeStr, "MusicArtist") == 0)
        return ITEM_TYPE_ARTIST;
    if (strcmp(typeStr, "MusicAlbum") == 0)
        return ITEM_TYPE_ALBUM;
    if (strcmp(typeStr, "Audio") == 0)
        return ITEM_TYPE_SONG;
    if (strcmp(typeStr, "Folder") == 0 || strcmp(typeStr, "CollectionFolder") == 0)
        return ITEM_TYPE_FOLDER;
    return ITEM_TYPE_UNKNOWN;
}

static const char* browserTypeName(ItemType type)
{
    switch (type) {
    case ITEM_TYPE_VIEW:   return "views";
    case ITEM_TYPE_ARTIST: return "artists";
    case ITEM_TYPE_ALBUM:  return "albums";
    case ITEM_TYPE_SONG:   return "songs";
    default:               return "items";
    }
}

static bool browserEnsureAudioCacheDir(void)
{
    char cache[256];
    char audio[256];
    struct stat st;

    sdPath(cache, sizeof(cache), "cache");
    sdPath(audio, sizeof(audio), "cache/audio");

    if (stat(audio, &st) == 0)
        return true;

    if (stat(cache, &st) != 0)
        mkdir(cache, 0777);

    mkdir(audio, 0777);
    return stat(audio, &st) == 0;
}

static void browserBuildAudioPath(const char* itemId, char* out, size_t outLen)
{
    char root[256];
    sdPath(root, sizeof(root), "");
    snprintf(out, outLen, "%s/cache/audio/%s.mp3", root, itemId ? itemId : "");
}

static void browserDownloadProgress(size_t downloaded, size_t total)
{
    if (total > 0) {
        printf("Downloading... %zu / %zu bytes\n", downloaded, total);
        browserSetStatus("Downloading... %zu / %zu", downloaded, total);
    } else {
        printf("Downloading... %zu bytes\n", downloaded);
        browserSetStatus("Downloading... %zu", downloaded);
    }
}

static Result browserDownloadSong(const char* itemId, const char* path)
{
    char url[BROWSER_STREAM_URL_MAX];
    Result res = jellyfinGetStreamUrl(g_app.config.serverUrl, appAuthToken(),
        itemId, url, sizeof(url));
    if (R_FAILED(res))
        return res;

    if (!browserEnsureAudioCacheDir())
        return -1;

    s_downloading = true;
    browserSetStatus("Downloading...");
    res = httpDownloadFileWithProgress(url, path, browserDownloadProgress);
    s_downloading = false;
    return res;
}

static void browserPushLevel(void)
{
    if (s_stackTop >= BROWSER_STACK_MAX - 1 || s_state.count <= 0)
        return;

    BrowserLevel* lvl = &s_stack[s_stackTop++];
    snprintf(lvl->id, sizeof(lvl->id), "%s", s_state.currentId);
    snprintf(lvl->name, sizeof(lvl->name), "%s", s_state.items[s_state.selected].name);
    lvl->type = s_state.currentType;
    lvl->selected = s_state.selected;
    lvl->scroll = s_state.scroll;
}

static void browserUpdateScroll(void)
{
    if (s_state.selected < 0)
        s_state.selected = 0;
    if (s_state.count > 0 && s_state.selected >= s_state.count)
        s_state.selected = s_state.count - 1;

    if (s_state.scroll > s_state.selected)
        s_state.scroll = s_state.selected;
    if (s_state.scroll < s_state.selected - BROWSER_VISIBLE + 1)
        s_state.scroll = s_state.selected - BROWSER_VISIBLE + 1;
    if (s_state.scroll < 0)
        s_state.scroll = 0;
    if (s_state.count <= BROWSER_VISIBLE)
        s_state.scroll = 0;
}

static void browserPlaySong(const BrowserItem* song)
{
    char path[512];
    browserBuildAudioPath(song->id, path, sizeof(path));

    if (access(path, F_OK) != 0) {
        Result res = browserDownloadSong(song->id, path);
        if (R_FAILED(res)) {
            browserSetStatus("Download failed: %08lX", (unsigned long)res);
            return;
        }
    }

    const char* artist = "";
    const char* album = "";
    if (s_stackTop >= 2)
        artist = s_stack[s_stackTop - 2].name;
    if (s_stackTop >= 1)
        album = s_stack[s_stackTop - 1].name;

    playerSetTrack(song->name, artist, album, song->id);
    screenChange(SCREEN_PLAYER);

    Result res = audioPlay(path);
    if (R_FAILED(res))
        browserSetStatus("Play failed: %08lX", (unsigned long)res);
}

static void browserDownloadOnly(const BrowserItem* song)
{
    char path[512];
    browserBuildAudioPath(song->id, path, sizeof(path));

    if (access(path, F_OK) == 0) {
        browserSetStatus("Already cached");
        return;
    }

    Result res = browserDownloadSong(song->id, path);
    if (R_FAILED(res))
        browserSetStatus("Download failed: %08lX", (unsigned long)res);
    else
        browserSetStatus("Downloaded %s", song->name);
}

static void browserLoadRootItems(void)
{
    s_state.currentType = ITEM_TYPE_VIEW;
    s_state.currentId[0] = '\0';
    browserResetItems();

    char* json = NULL;
    size_t len = 0;
    Result res = jellyfinGetViews(g_app.config.serverUrl, appAuthToken(), &json, &len);
    if (R_FAILED(res)) {
        browserSetStatus("Views failed: %08lX", (unsigned long)res);
        return;
    }

    json_error_t err;
    json_t* root = json_loads(json, 0, &err);
    free(json);
    if (!root) {
        browserSetStatus("Views parse error");
        return;
    }

    json_t* items = json_object_get(root, "Items");
    if (!json_is_array(items)) {
        json_decref(root);
        browserSetStatus("No Items in views");
        return;
    }

    size_t i, n = json_array_size(items);
    if (n > BROWSER_MAX_ITEMS)
        n = BROWSER_MAX_ITEMS;

    for (i = 0; i < n; ++i) {
        json_t* it = json_array_get(items, i);
        if (!json_is_object(it))
            continue;
        const char* id = jsonGetString(it, "Id");
        const char* name = jsonGetString(it, "Name");
        if (!id || !name)
            continue;

        BrowserItem* b = &s_state.items[s_state.count++];
        strncpy(b->id, id, sizeof(b->id) - 1);
        b->id[sizeof(b->id) - 1] = '\0';
        strncpy(b->name, name, sizeof(b->name) - 1);
        b->name[sizeof(b->name) - 1] = '\0';
        b->type = ITEM_TYPE_VIEW;
        b->parentId[0] = '\0';
    }

    json_decref(root);
    if (s_state.count == 0)
        browserSetStatus("No libraries found");
    else
        browserSetStatus("Loaded %d views", s_state.count);
}

void browserInit(void)
{
    browserResetStack();
    memset(&s_state, 0, sizeof(s_state));
    s_status[0] = '\0';
    s_downloading = false;
}

void browserLoadRoot(void)
{
    browserResetStack();

    if (g_app.config.serverUrl[0] == '\0') {
        browserSetStatus("Set server URL in settings");
        return;
    }

    Result authRes = browserEnsureAuthenticated();
    if (R_FAILED(authRes)) {
        if (authRes == HTTP_ERR_STATUS)
            browserSetStatus("Login failed: HTTP %d", httpLastStatus());
        else
            browserSetStatus("Login failed: %08lX", (unsigned long)authRes);
        return;
    }

    browserLoadRootItems();
}

void browserLoadItems(const char* parentId, ItemType type)
{
    browserResetItems();
    s_state.currentType = type;
    if (parentId) {
        strncpy(s_state.currentId, parentId, sizeof(s_state.currentId) - 1);
        s_state.currentId[sizeof(s_state.currentId) - 1] = '\0';
    } else {
        s_state.currentId[0] = '\0';
    }

    if (g_app.config.serverUrl[0] == '\0' || appAuthToken()[0] == '\0') {
        browserSetStatus("Not authenticated");
        return;
    }

    const char* includeTypes = NULL;
    const char* artistId = NULL;
    const char* queryParentId = parentId;

    switch (type) {
    case ITEM_TYPE_ARTIST:
        includeTypes = "MusicArtist";
        break;
    case ITEM_TYPE_ALBUM:
        includeTypes = "MusicAlbum";
        artistId = parentId;
        queryParentId = NULL;
        break;
    case ITEM_TYPE_SONG:
        includeTypes = "Audio";
        break;
    default:
        includeTypes = "Audio,MusicAlbum,MusicArtist";
        break;
    }

    char* json = NULL;
    size_t len = 0;
    Result res = jellyfinGetItems(g_app.config.serverUrl, appAuthToken(),
        queryParentId, artistId, includeTypes, &json, &len);
    if (R_FAILED(res)) {
        browserSetStatus("Load failed: %08lX", (unsigned long)res);
        return;
    }

    json_error_t err;
    json_t* root = json_loads(json, 0, &err);
    free(json);
    if (!root) {
        browserSetStatus("Parse error");
        return;
    }

    json_t* items = json_object_get(root, "Items");
    if (!json_is_array(items)) {
        json_decref(root);
        browserSetStatus("No Items");
        return;
    }

    size_t i, n = json_array_size(items);
    if (n > BROWSER_MAX_ITEMS)
        n = BROWSER_MAX_ITEMS;

    for (i = 0; i < n; ++i) {
        json_t* it = json_array_get(items, i);
        if (!json_is_object(it))
            continue;
        const char* id = jsonGetString(it, "Id");
        const char* name = jsonGetString(it, "Name");
        const char* typeStr = jsonGetString(it, "Type");
        if (!id || !name)
            continue;

        BrowserItem* b = &s_state.items[s_state.count++];
        strncpy(b->id, id, sizeof(b->id) - 1);
        b->id[sizeof(b->id) - 1] = '\0';
        strncpy(b->name, name, sizeof(b->name) - 1);
        b->name[sizeof(b->name) - 1] = '\0';
        b->type = browserTypeFromString(typeStr);
        if (parentId) {
            strncpy(b->parentId, parentId, sizeof(b->parentId) - 1);
            b->parentId[sizeof(b->parentId) - 1] = '\0';
        } else {
            b->parentId[0] = '\0';
        }
    }

    json_decref(root);
    browserSetStatus("Loaded %d %s", s_state.count, browserTypeName(type));
}

void browserUpdate(void)
{
    if (s_downloading)
        return;

    if (g_app.touchDown) {
        if (g_app.touch.py >= BROWSER_HINT_Y) {
            if (g_app.touch.px < 110)
                g_app.kDown |= KEY_B;
            else if (g_app.touch.px >= 220)
                g_app.kDown |= KEY_X;
        } else if (g_app.touch.py >= BROWSER_LIST_TOP && s_state.count > 0) {
            int tapped = s_state.scroll +
                ((int)g_app.touch.py - BROWSER_LIST_TOP) / BROWSER_ROW_H;
            if (tapped >= 0 && tapped < s_state.count) {
                s_state.selected = tapped;
                g_app.kDown |= KEY_A;
            }
        }
    }

    if (g_app.kDown & KEY_UP) {
        s_state.selected--;
        if (s_state.selected < 0)
            s_state.selected = s_state.count - 1;
        browserUpdateScroll();
    }

    if (g_app.kDown & KEY_DOWN) {
        s_state.selected++;
        if (s_state.selected >= s_state.count)
            s_state.selected = 0;
        browserUpdateScroll();
    }

    if (g_app.kDown & KEY_L) {
        s_state.selected -= BROWSER_VISIBLE;
        if (s_state.selected < 0)
            s_state.selected = 0;
        browserUpdateScroll();
    }

    if (g_app.kDown & KEY_R) {
        s_state.selected += BROWSER_VISIBLE;
        if (s_state.selected >= s_state.count)
            s_state.selected = s_state.count - 1;
        browserUpdateScroll();
    }

    if (g_app.kDown & KEY_B) {
        if (s_stackTop <= 0) {
            screenChange(SCREEN_HOME);
        } else {
            BrowserLevel* lvl = &s_stack[--s_stackTop];
            int sel = lvl->selected;
            int scroll = lvl->scroll;
            if (lvl->type == ITEM_TYPE_VIEW)
                browserLoadRootItems();
            else
                browserLoadItems(lvl->id[0] ? lvl->id : NULL, lvl->type);
            s_state.selected = sel;
            s_state.scroll = scroll;
            browserUpdateScroll();
        }
        return;
    }

    if (s_state.count <= 0)
        return;

    if (g_app.kDown & KEY_A) {
        const BrowserItem* item = &s_state.items[s_state.selected];
        switch (item->type) {
        case ITEM_TYPE_VIEW:
            browserPushLevel();
            browserLoadItems(item->id, ITEM_TYPE_ARTIST);
            break;
        case ITEM_TYPE_ARTIST:
            browserPushLevel();
            browserLoadItems(item->id, ITEM_TYPE_ALBUM);
            break;
        case ITEM_TYPE_ALBUM:
            browserPushLevel();
            browserLoadItems(item->id, ITEM_TYPE_SONG);
            break;
        case ITEM_TYPE_SONG:
            browserPlaySong(item);
            break;
        default:
            break;
        }
    }

    if (g_app.kDown & KEY_X) {
        const BrowserItem* item = &s_state.items[s_state.selected];
        if (item->type == ITEM_TYPE_SONG)
            browserDownloadOnly(item);
    }
}

static const char* browserItemBadge(ItemType type)
{
    switch (type) {
    case ITEM_TYPE_VIEW:   return "V";
    case ITEM_TYPE_ARTIST: return "A";
    case ITEM_TYPE_ALBUM:  return "L";
    case ITEM_TYPE_SONG:   return "S";
    case ITEM_TYPE_FOLDER: return "F";
    default:               return "?";
    }
}

static u32 browserItemColor(ItemType type)
{
    switch (type) {
    case ITEM_TYPE_VIEW:   return GUI_COL_ACCENT;
    case ITEM_TYPE_ARTIST: return GUI_COL_GOOD;
    case ITEM_TYPE_ALBUM:  return GUI_COL_SELECT;
    case ITEM_TYPE_SONG:   return GUI_COL_TEXT;
    default:               return GUI_COL_DIM;
    }
}

static void browserBuildBreadcrumb(char* out, size_t outLen)
{
    out[0] = '\0';
    strncat(out, "Libraries", outLen - strlen(out) - 1);
    for (int i = 0; i < s_stackTop; ++i) {
        strncat(out, " / ", outLen - strlen(out) - 1);
        strncat(out, s_stack[i].name, outLen - strlen(out) - 1);
    }
}

void browserRender(void)
{
    /* Header. */
    guiRect(0, 0, GUI_BOT_W, BROWSER_HEADER_H, GUI_COL_HEADER);
    guiRect(0, BROWSER_HEADER_H - 2, GUI_BOT_W, 2, GUI_COL_SELECT);
    guiText("LIBRARY", 10, 6, 0.6f, GUI_COL_TEXT);

    /* Breadcrumb. */
    char path[256];
    browserBuildBreadcrumb(path, sizeof(path));
    guiText(path, 10, 32, 0.4f, GUI_COL_MUTED);

    /* List. */
    if (s_state.count <= 0) {
        guiText("No items.", 12, 64, 0.45f, GUI_COL_MUTED);
    } else {
        int end = s_state.scroll + BROWSER_VISIBLE;
        if (end > s_state.count)
            end = s_state.count;

        for (int i = s_state.scroll; i < end; ++i) {
            float y = BROWSER_LIST_TOP + (i - s_state.scroll) * BROWSER_ROW_H;
            const BrowserItem* item = &s_state.items[i];

            if (i == s_state.selected)
                guiPanelHighlight(4, y, GUI_BOT_W - 8, BROWSER_ROW_H - 2);
            else
                guiRect(4, y, GUI_BOT_W - 8, BROWSER_ROW_H - 2, GUI_COL_PANEL2);

            guiText(browserItemBadge(item->type), 12, y + 3, 0.45f,
                browserItemColor(item->type));
            guiText(item->name, 28, y + 3, 0.45f, GUI_COL_TEXT);
        }
    }

    /* Status line. */
    if (s_status[0])
        guiText(s_status, 12, BROWSER_HINT_Y - 16, 0.4f, GUI_COL_MUTED);

    /* Hint bar. */
    guiRect(0, BROWSER_HINT_Y, GUI_BOT_W, 34, GUI_COL_HEADER);
    guiRect(0, BROWSER_HINT_Y, GUI_BOT_W, 1, GUI_COL_DIM);
    guiText("B: Back  X: Cache", 8, BROWSER_HINT_Y + 8, 0.4f, GUI_COL_MUTED);
    guiTextRight("A: Open/Play", GUI_BOT_W - 8, BROWSER_HINT_Y + 8, 0.4f,
        GUI_COL_MUTED);
}

void browserRenderTop(void)
{
    if (s_state.count > 0 && s_state.selected < s_state.count) {
        const BrowserItem* item = &s_state.items[s_state.selected];
        guiTextCentered(item->name, GUI_TOP_W / 2.0f, 56, 0.8f, GUI_COL_TEXT);
    }

    char path[256];
    browserBuildBreadcrumb(path, sizeof(path));
    guiTextCentered(path, GUI_TOP_W / 2.0f, 92, 0.4f, GUI_COL_MUTED);

    if (s_state.count > 0 && s_state.selected < s_state.count) {
        const BrowserItem* item = &s_state.items[s_state.selected];
        guiTextCentered(browserItemBadge(item->type), GUI_TOP_W / 2.0f, 120, 0.6f,
            browserItemColor(item->type));
        guiTextCentered("UP/DOWN move   L/R page", GUI_TOP_W / 2.0f, 200, 0.4f,
            GUI_COL_DIM);
    }
}
