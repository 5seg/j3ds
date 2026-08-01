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

typedef enum {
	THUMB_LOADING = 0, /* No load finished yet. */
	THUMB_READY = 1,   /* A load finished successfully; *out is valid. */
	THUMB_FAILED = 2   /* A load finished but failed; retrying is allowed. */
} ThumbnailStatus;

/* Start loading a thumbnail from a Jellyfin image URL in the background
   (download + JPEG decode). Texture upload must happen on the render
   thread, so it is deferred until thumbnailPollReady(). Returns false if
   the URL is empty or a load is already in progress (the caller may retry
   the request later). */
bool thumbnailLoadAsync(const char* url);

/* Called from the render thread each frame. Returns THUMB_LOADING while no
   result is ready, THUMB_READY on success (*out is valid), or THUMB_FAILED
   when the last load failed. The internal cache file for a failed load is
   removed so a retry re-downloads it. Textures are owned by an internal
   cache and stay valid until thumbnailReleaseCache(). */
ThumbnailStatus thumbnailPollReady(Thumbnail* out);

/* Free all textures held by the internal cache. Call on app exit. */
void thumbnailReleaseCache(void);

#endif
