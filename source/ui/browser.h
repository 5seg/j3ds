#ifndef UI_BROWSER_H
#define UI_BROWSER_H

#define BROWSER_MAX_ITEMS 256
#define BROWSER_MAX_NAME 128

typedef enum {
    ITEM_TYPE_UNKNOWN,
    ITEM_TYPE_FOLDER,
    ITEM_TYPE_ARTIST,
    ITEM_TYPE_ALBUM,
    ITEM_TYPE_SONG,
    ITEM_TYPE_VIEW
} ItemType;

typedef struct {
    char id[64];
    char name[BROWSER_MAX_NAME];
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

#endif
