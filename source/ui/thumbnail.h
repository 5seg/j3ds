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

/* Download, decode, and upload an image synchronously. Call this on the
   render thread before starting audio so no HTTP requests overlap the audio
   stream and no worker can return stale art for a previous track. */
bool thumbnailLoad(const char* url, Thumbnail* out);

/* Free all textures held by the internal cache. Call on app exit. */
void thumbnailReleaseCache(void);

#endif
