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

/* Start loading a thumbnail from a Jellyfin image URL in the background
   (download + JPEG decode). Texture upload must happen on the render
   thread, so it is deferred until thumbnailPollReady(). Returns false if
   a load is already in progress or the URL is empty. */
bool thumbnailLoadAsync(const char* url);

/* Called from the render thread each frame. Returns true when the load
   finished and *out is valid (or the load failed). Call repeatedly until
   it returns true. Textures are owned by an internal cache and stay valid
   until thumbnailReleaseCache(). */
bool thumbnailPollReady(Thumbnail* out);

/* Free all textures held by the internal cache. Call on app exit. */
void thumbnailReleaseCache(void);

#endif
