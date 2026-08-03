#ifndef UTILS_IMAGE_H
#define UTILS_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <3ds.h>
#include <citro2d.h>

/* --- PICA200 GPU_RGBA8 texel format ---------------------------------------

   A GPU_RGBA8 texel (the format tex3ds emits and the format C3D_TexUpload
   expects) is stored as the four bytes A, B, G, R in that memory order. The
   3DS is little-endian, so a u32 view of one texel is:

       u32 = (r << 24) | (g << 16) | (b << 8) | a       i.e. 0xRRGGBBAA

   This is NOT the same as C2D_Color32(), which packs r | g<<8 | b<<16 | a<<24
   (0xAABBGGRR, memory order R,G,B,A). C2D_Color32 is the *vertex colour*
   format used for UI primitives and tints; it is the exact byte-reversal of a
   texel. Feeding C2D_Color32 values to a texture makes the GPU read the red
   channel as alpha and swaps red with blue.

   Use these helpers for anything that ends up in a C3D_Tex; keep C2D_Color32
   for UI drawing colours. */

static inline u32 imageTexelPack(u8 r, u8 g, u8 b, u8 a)
{
	return ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | (u32)a;
}

static inline void imageTexelUnpack(u32 texel, u8* r, u8* g, u8* b, u8* a)
{
	*r = (u8)(texel >> 24);
	*g = (u8)(texel >> 16);
	*b = (u8)(texel >> 8);
	*a = (u8)texel;
}

/* Decode a JPEG in memory into a linear GPU_RGBA8 texel buffer (upper-left
   origin, see imageTexelPack above for the byte order).
   If maxDim > 0 the decode is scaled down so the larger edge is at least
   maxDim (never smaller) using libjpeg DCT scaling, which is far faster and
   lighter than decoding full-size. Caller must free() the returned buffer.
   Returns false on error. */
bool imageLoadJpegRgba(const void* data, size_t size, int maxDim,
	u32** outRgba, int* outW, int* outH);

/* Convert a linear RGBA8 buffer into the 3DS GPU tiled layout in-place.
   width/height must be powers of two. The buffer is swizzled in place. */
void imageSwizzleRgba8(u32* rgba, int width, int height);

/* Upload a linear RGBA8 buffer into a GPU texture. The buffer must be a
   power-of-two canvas of canvasW x canvasH pixels; the image occupies the
   top-left imgW x imgH region of that canvas. The subtexture is sized to the
   real image so no transparent fringe shows when drawn. *outSubtex is
   caller-owned. Returns false on error. */
bool imageUploadToTexture(C3D_Tex* tex, const u32* rgba,
	int canvasW, int canvasH, int imgW, int imgH,
	Tex3DS_SubTexture* outSubtex, C2D_Image* out);

/* Free a texture previously created by imageUploadToTexture. */
void imageFreeTexture(C3D_Tex* tex);

#endif
