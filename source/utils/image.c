#include "utils/image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>

struct image_jpeg_error_mgr {
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};

static void image_jpeg_error_exit(j_common_ptr cinfo)
{
	struct image_jpeg_error_mgr* err = (struct image_jpeg_error_mgr*)cinfo->err;
	(*cinfo->err->output_message)(cinfo);
	longjmp(err->setjmp_buffer, 1);
}

bool imageLoadJpegRgba(const void* data, size_t size, int maxDim,
	u32** outRgba, int* outW, int* outH)
{
	if (!data || size == 0 || !outRgba || !outW || !outH)
		return false;

	struct jpeg_decompress_struct cinfo;
	struct image_jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = image_jpeg_error_exit;

	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		return false;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, (const unsigned char*)data, size);

	if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
		jpeg_destroy_decompress(&cinfo);
		return false;
	}

	if (maxDim > 0) {
		/* Pick the largest supported DCT scale factor (1/2, 1/4, 1/8) that
		   keeps the larger edge at least maxDim, so the caller never
		   upscales and never wastes time on full-size decoding. */
		jpeg_calc_output_dimensions(&cinfo);
		unsigned int longEdge = cinfo.output_width > cinfo.output_height
			? cinfo.output_width : cinfo.output_height;
		int denom = 1;
		while (denom < 8 && (longEdge / (unsigned int)(denom * 2))
			>= (unsigned int)maxDim)
			denom *= 2;
		cinfo.scale_num = 1;
		cinfo.scale_denom = denom;
	}

	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&cinfo);

	int srcW = (int)cinfo.output_width;
	int srcH = (int)cinfo.output_height;
	if (srcW <= 0 || srcH <= 0) {
		jpeg_abort_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		return false;
	}

	int row_stride = srcW * 3;
	JSAMPARRAY buffer = (*cinfo.mem->alloc_sarray)
		((j_common_ptr)&cinfo, JPOOL_IMAGE, (unsigned int)row_stride, 1);

	u32* rgba = (u32*)malloc((size_t)srcW * (size_t)srcH * 4);
	if (!rgba) {
		jpeg_abort_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		return false;
	}

	int row = 0;
	while (cinfo.output_scanline < cinfo.output_height) {
		jpeg_read_scanlines(&cinfo, buffer, 1);
		u8* line = buffer[0];
		for (int x = 0; x < srcW; x++) {
			rgba[(size_t)row * srcW + x] = C2D_Color32(line[x * 3],
				line[x * 3 + 1], line[x * 3 + 2], 0xFF);
		}
		row++;
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);

	*outRgba = rgba;
	*outW = srcW;
	*outH = srcH;
	return true;
}

/* PICA200 tiled texture layout: the texture is divided into 8x8 pixel tiles.
   Tiles are stored in row-major order, but within each tile the pixels are
   stored in Morton (Z-order) interleaved order, not row-major. This matches
   what tex3ds produces and what the GPU expects when sampling a tiled
   texture. */
static inline u32 imageMorton3(u32 x, u32 y)
{
	u32 z = 0;
	for (int i = 0; i < 3; i++) {
		z |= ((x >> i) & 1) << (2 * i);
		z |= ((y >> i) & 1) << (2 * i + 1);
	}
	return z;
}

void imageSwizzleRgba8(u32* rgba, int width, int height)
{
	if (!rgba || width <= 0 || height <= 0)
		return;

	u32* tmp = (u32*)malloc((size_t)width * (size_t)height * 4);
	if (!tmp)
		return;

	int tilesX = width / 8;
	int tilesY = height / 8;

	for (int ty = 0; ty < tilesY; ty++) {
		for (int tx = 0; tx < tilesX; tx++) {
			for (int yy = 0; yy < 8; yy++) {
				for (int xx = 0; xx < 8; xx++) {
					u32 px = rgba[(size_t)(ty * 8 + yy) * width + tx * 8 + xx];
					int dstIndex = ((size_t)(ty * tilesX + tx) * 64) +
						(int)imageMorton3((u32)xx, (u32)yy);
					tmp[dstIndex] = px;
				}
			}
		}
	}

	memcpy(rgba, tmp, (size_t)width * (size_t)height * 4);
	free(tmp);
}

static inline u32 nextPowerOfTwo(u32 v)
{
	u32 p = 1;
	while (p < v)
		p <<= 1;
	return p;
}

bool imageUploadToTexture(C3D_Tex* tex, const u32* rgba,
	int canvasW, int canvasH, int imgW, int imgH,
	Tex3DS_SubTexture* outSubtex, C2D_Image* out)
{
	if (!tex || !rgba || !out || !outSubtex)
		return false;
	if (canvasW <= 0 || canvasH <= 0 || imgW <= 0 || imgH <= 0)
		return false;
	if (imgW > canvasW || imgH > canvasH)
		return false;
	if (canvasW > 1024 || canvasH > 1024)
		return false;

	if (!C3D_TexInit(tex, (u16)canvasW, (u16)canvasH, GPU_RGBA8))
		return false;

	C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

	/* Copy so we don't mutate the caller's buffer during swizzle. */
	size_t bytes = (size_t)canvasW * (size_t)canvasH * 4;
	u32* swizzled = (u32*)malloc(bytes);
	if (!swizzled) {
		C3D_TexDelete(tex);
		return false;
	}
	memcpy(swizzled, rgba, bytes);
	imageSwizzleRgba8(swizzled, canvasW, canvasH);

	C3D_TexUpload(tex, swizzled);
	free(swizzled);
	C3D_TexFlush(tex);

	/* The subtexture covers only the real image, not the padding. The
	   PICA200 samples textures V-flipped (v=0 at the bottom of memory), so
	   the top edge of the image sits at v=1.0 and `top` must stay larger
	   than `bottom` or the Tex3DS helpers treat it as rotated. The right and
	   bottom edges are inset by one texel so the linear filter never blends
	   into the transparent padding. */
	outSubtex->width = (u16)imgW;
	outSubtex->height = (u16)imgH;
	outSubtex->left = 0.0f;
	outSubtex->top = 1.0f;
	outSubtex->right = (float)(imgW - 1) / (float)canvasW;
	outSubtex->bottom = 1.0f - (float)(imgH - 1) / (float)canvasH;

	out->tex = tex;
	out->subtex = outSubtex;
	return true;
}

void imageFreeTexture(C3D_Tex* tex)
{
	if (tex)
		C3D_TexDelete(tex);
}
