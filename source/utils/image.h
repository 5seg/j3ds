#ifndef UTILS_IMAGE_H
#define UTILS_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <3ds.h>
#include <citro2d.h>

/* Decode a JPEG in memory into a linear RGBA8 buffer (upper-left origin).
   Caller must free() the returned buffer. Returns false on error. */
bool imageLoadJpegRgba(const void* data, size_t size,
	u32** outRgba, int* outW, int* outH);

/* Convert a linear RGBA8 buffer into the 3DS GPU tiled layout in-place.
   width/height must be powers of two. The buffer is swizzled in place. */
void imageSwizzleRgba8(u32* rgba, int width, int height);

/* Upload a linear RGBA8 buffer (power-of-two dims) into a GPU texture.
   The subtexture is stored in *outSubtex (caller-owned) so the returned
   C2D_Image remains valid while the texture lives. Returns false on error. */
bool imageUploadToTexture(C3D_Tex* tex, const u32* rgba, int width, int height,
	Tex3DS_SubTexture* outSubtex, C2D_Image* out);

/* Free a texture previously created by imageUploadToTexture. */
void imageFreeTexture(C3D_Tex* tex);

#endif
