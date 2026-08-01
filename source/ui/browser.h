#ifndef UI_BROWSER_H
#define UI_BROWSER_H

#define BROWSER_MAX_ITEMS 256
#define BROWSER_MAX_NAME 128

typedef enum {
    ITEM_TYPE_UNKNOWN,
    ITEM_TYPE_PLAYLISTS,    /* root entry "Playlists" / playlist list */
    ITEM_TYPE_ALL,          /* root entry "All Musics" / full song list */
    ITEM_TYPE_PLAYLIST,     /* a single playlist */
    ITEM_TYPE_SONG          /* an audio track */
} ItemType;

typedef struct {
    char id[64];
    char name[BROWSER_MAX_NAME];
    char artist[BROWSER_MAX_NAME];
    char album[BROWSER_MAX_NAME];
    ItemType type;
    char parentId[64];
} BrowserItem;

typedef struct {
    BrowserItem items[BROWSER_MAX_ITEMS];
    int count;
    int selected;
    int scroll;
    ItemType currentType;
    char currentId[64];
} BrowserState;

void browserInit(void);
void browserLoadRoot(void);
void browserLoadItems(const char* parentId, ItemType type);
void browserUpdate(void);
void browserRender(void);
void browserRenderTop(void);

#endif
