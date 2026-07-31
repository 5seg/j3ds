#ifndef UI_THUMBNAIL_H
#define UI_THUMBNAIL_H

#include <stdbool.h>
#include <3ds.h>
#include <citro2d.h>

typedef struct {
	C2D_Image image;
	bool valid;
	int width;
	int height;
} Thumbnail;

/* Load a thumbnail from a Jellyfin image URL (with SD cache).
   Textures are owned by an internal cache: the returned C2D_Image stays
   valid until thumbnailReleaseCache() is called. Returns false on error. */
bool thumbnailLoad(const char* url, Thumbnail* out);

/* Free all textures held by the internal cache. Call on app exit. */
void thumbnailReleaseCache(void);

#endif
