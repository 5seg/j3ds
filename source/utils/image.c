#include "utils/image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>

/* 3DS top screen (GFX_TOP, GFX_LEFT) framebuffer layout used by this project.
   The buffer is treated as 240 pixels wide and 400 pixels tall in memory
   (the hardware rotates it for display).  Each pixel is 3 bytes BGR. */
#define FB_STRIDE 240
#define FB_BPP    3

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

void imageDrawRect(void* framebuffer, int x, int y, int w, int h, u8 r, u8 g, u8 b)
{
	if (!framebuffer || w <= 0 || h <= 0)
		return;

	u8* fb = (u8*)framebuffer;

	for (int dy = 0; dy < h; dy++) {
		int py = y + dy;
		if (py < 0)
			continue;

		for (int dx = 0; dx < w; dx++) {
			int px = x + dx;
			if (px < 0)
				continue;

			int idx = (py * FB_STRIDE + px) * FB_BPP;
			fb[idx + 0] = b;
			fb[idx + 1] = g;
			fb[idx + 2] = r;
		}
	}
}

bool imageLoadJpeg(const void* data, size_t size, void* framebuffer, int x, int y, int w, int h)
{
	if (!data || size == 0 || !framebuffer || w <= 0 || h <= 0)
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
		((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

	u8* rgb = (u8*)malloc((size_t)srcW * (size_t)srcH * 3);
	if (!rgb) {
		jpeg_abort_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		return false;
	}

	int row = 0;
	while (cinfo.output_scanline < cinfo.output_height) {
		jpeg_read_scanlines(&cinfo, buffer, 1);
		memcpy(rgb + (size_t)row * row_stride, buffer[0], row_stride);
		row++;
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);

	/* Aspect-fit inside the requested w x h box. */
	float scaleX = (float)w / (float)srcW;
	float scaleY = (float)h / (float)srcH;
	float scale = scaleX < scaleY ? scaleX : scaleY;

	int dstW = (int)((float)srcW * scale);
	int dstH = (int)((float)srcH * scale);
	if (dstW < 1) dstW = 1;
	if (dstH < 1) dstH = 1;

	int offX = x + (w - dstW) / 2;
	int offY = y + (h - dstH) / 2;

	u8* fb = (u8*)framebuffer;

	for (int dy = 0; dy < dstH; dy++) {
		int sy = dy * srcH / dstH;
		if (sy >= srcH) sy = srcH - 1;
		int py = offY + dy;

		for (int dx = 0; dx < dstW; dx++) {
			int sx = dx * srcW / dstW;
			if (sx >= srcW) sx = srcW - 1;
			int px = offX + dx;

			const u8* src = rgb + ((size_t)sy * (size_t)srcW + (size_t)sx) * 3;
			int idx = (py * FB_STRIDE + px) * FB_BPP;
			fb[idx + 0] = src[2]; /* B */
			fb[idx + 1] = src[1]; /* G */
			fb[idx + 2] = src[0]; /* R */
		}
	}

	free(rgb);
	return true;
}
