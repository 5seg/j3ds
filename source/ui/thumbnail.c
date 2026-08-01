#include "ui/thumbnail.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "net/http.h"
#include "sys/sd.h"
#include "utils/debug.h"
#include "utils/image.h"

#define THUMB_CACHE_DIR "cache/thumbs"
#define THUMB_MAX_SIZE  256

typedef struct {
	unsigned int hash;
	C3D_Tex tex;
	Tex3DS_SubTexture subtex;
	C2D_Image image;
	bool loaded;
} ThumbnailCacheEntry;

static ThumbnailCacheEntry s_cache[8];
static int s_cacheCount = 0;

static unsigned int thumbnailHashUrl(const char* url)
{
	unsigned int hash = 5381;
	int c;

	while ((c = (unsigned char)*url++) != 0)
		hash = ((hash << 5) + hash) + (unsigned int)c;

	return hash;
}

static void thumbnailCachePath(unsigned int hash, char* out, size_t outLen)
{
	char root[512];
	sdPath(root, sizeof(root), "");
	snprintf(out, outLen, "%s/%s/%08x.jpg", root, THUMB_CACHE_DIR, hash);
}

/* Remove the disk cache file so a failed/corrupt download gets re-fetched. */
static void thumbnailDeleteCache(unsigned int hash)
{
	char path[512];
	thumbnailCachePath(hash, path, sizeof(path));
	unlink(path);
}

static bool thumbnailEnsureCacheDir(void)
{
	char root[512];
	sdPath(root, sizeof(root), "");

	char cache[512];
	sdPath(cache, sizeof(cache), "cache");

	char thumbs[512];
	sdPath(thumbs, sizeof(thumbs), THUMB_CACHE_DIR);

	struct stat st;
	if (stat(thumbs, &st) == 0)
		return true;

	if (stat(cache, &st) != 0)
		mkdir(cache, 0777);

	mkdir(thumbs, 0777);
	return stat(thumbs, &st) == 0;
}

static bool thumbnailReadFile(const char* path, void** outData, size_t* outSize)
{
	FILE* f = fopen(path, "rb");
	if (!f)
		return false;

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return false;
	}

	long size = ftell(f);
	if (size <= 0) {
		fclose(f);
		return false;
	}

	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return false;
	}

	u8* data = (u8*)malloc((size_t)size);
	if (!data) {
		fclose(f);
		return false;
	}

	if (fread(data, 1, (size_t)size, f) != (size_t)size) {
		free(data);
		fclose(f);
		return false;
	}

	fclose(f);
	*outData = data;
	*outSize = (size_t)size;
	return true;
}

static ThumbnailCacheEntry* thumbnailFindCached(unsigned int hash)
{
	for (int i = 0; i < s_cacheCount; ++i) {
		if (s_cache[i].hash == hash)
			return &s_cache[i];
	}
	return NULL;
}

/* Area-average (box) downscale srcW x srcH -> dstW x dstH into dst.
   Every source pixel covered by a destination pixel contributes equally,
   which keeps edges clean (no nearest-neighbour aliasing). Integer math.
   dstStride is the destination row stride in pixels; it may be larger than
   dstW so the resized image can be laid out inside a power-of-two canvas. */
static void thumbnailResizeBox(const u32* src, int srcW, int srcH,
	u32* dst, int dstW, int dstH, int dstStride)
{
	for (int y = 0; y < dstH; ++y) {
		int y0 = (int)(((long long)y * srcH) / dstH);
		int y1 = (int)(((long long)(y + 1) * srcH) / dstH);
		if (y1 <= y0)
			y1 = y0 + 1;
		if (y1 > srcH)
			y1 = srcH;

		for (int x = 0; x < dstW; ++x) {
			int x0 = (int)(((long long)x * srcW) / dstW);
			int x1 = (int)(((long long)(x + 1) * srcW) / dstW);
			if (x1 <= x0)
				x1 = x0 + 1;
			if (x1 > srcW)
				x1 = srcW;

			unsigned long long r = 0, g = 0, b = 0, a = 0;
			unsigned int count = 0;
			for (int sy = y0; sy < y1; ++sy) {
				const u32* row = src + (size_t)sy * srcW;
				for (int sx = x0; sx < x1; ++sx) {
					u32 p = row[sx];
					r += (p >> 24) & 0xFF;
					g += (p >> 16) & 0xFF;
					b += (p >> 8) & 0xFF;
					a += p & 0xFF;
					count++;
				}
			}

			u32 pr = (u32)(r / count);
			u32 pg = (u32)(g / count);
			u32 pb = (u32)(b / count);
			u32 pa = (u32)(a / count);
			dst[(size_t)y * dstStride + x] = (pr << 24) | (pg << 16) | (pb << 8) | pa;
		}
	}
}

/* Downscale a decoded RGBA8 image into a power-of-two texture canvas. */
static bool thumbnailBuildTexture(const u32* rgba, int srcW, int srcH,
	ThumbnailCacheEntry* entry)
{
	float scale = 1.0f;
	if (srcW > THUMB_MAX_SIZE || srcH > THUMB_MAX_SIZE)
		scale = ((float)THUMB_MAX_SIZE) /
			((srcW > srcH) ? (float)srcW : (float)srcH);

	int dstW = (int)((float)srcW * scale);
	int dstH = (int)((float)srcH * scale);
	if (dstW < 1) dstW = 1;
	if (dstH < 1) dstH = 1;

	u32 texW = 1;
	while (texW < (u32)dstW) texW <<= 1;
	if (texW < 8) texW = 8;
	u32 texH = 1;
	while (texH < (u32)dstH) texH <<= 1;
	if (texH < 8) texH = 8;

	size_t bytes = (size_t)texW * (size_t)texH * 4;
	u32* canvas = (u32*)malloc(bytes);
	if (!canvas)
		return false;
	memset(canvas, 0, bytes);

	thumbnailResizeBox(rgba, srcW, srcH, canvas, dstW, dstH, (int)texW);

	bool ok = imageUploadToTexture(&entry->tex, canvas, (int)texW, (int)texH,
		dstW, dstH, &entry->subtex, &entry->image);
	free(canvas);
	return ok;
}

void thumbnailReleaseCache(void)
{
	for (int i = 0; i < s_cacheCount; ++i)
		imageFreeTexture(&s_cache[i].tex);
	s_cacheCount = 0;
}

typedef struct {
	Thread thread;
	volatile bool done;
	bool ok;
	bool cached;
	unsigned int hash;
	char url[512];
	u32* rgba;
	int srcW;
	int srcH;
	Thumbnail result;
} AsyncThumb;

static AsyncThumb s_async;

static void thumbBgThread(void* arg)
{
	(void)arg;

	char path[512];
	thumbnailCachePath(s_async.hash, path, sizeof(path));

	struct stat st;
	if (stat(path, &st) != 0) {
		if (R_FAILED(httpDownloadFile(s_async.url, path))) {
			debugLog("thumb dl fail: http=%d", httpLastStatus());
			s_async.done = true;
			return;
		}
	}

	void* data = NULL;
	size_t size = 0;
	if (!thumbnailReadFile(path, &data, &size)) {
		debugLog("thumb read fail");
		s_async.done = true;
		return;
	}

	s_async.ok = imageLoadJpegRgba(data, size, THUMB_MAX_SIZE, &s_async.rgba,
		&s_async.srcW, &s_async.srcH);
	free(data);
	if (!s_async.ok)
		debugLog("thumb decode fail");
	else
		debugLog("thumb decoded %dx%d", s_async.srcW, s_async.srcH);
	s_async.done = true;
}

bool thumbnailLoadAsync(const char* url)
{
	if (!url || url[0] == '\0' || s_async.thread)
		return false;

	if (!thumbnailEnsureCacheDir())
		return false;

	memset(&s_async, 0, sizeof(s_async));
	strncpy(s_async.url, url, sizeof(s_async.url) - 1);
	s_async.url[sizeof(s_async.url) - 1] = '\0';

	s_async.hash = thumbnailHashUrl(url);

	ThumbnailCacheEntry* cached = thumbnailFindCached(s_async.hash);
	if (cached) {
		s_async.cached = true;
		s_async.result.image = cached->image;
		s_async.result.width = (int)cached->subtex.width;
		s_async.result.height = (int)cached->subtex.height;
		s_async.result.valid = true;
		s_async.done = true;
		return true;
	}

	s_async.thread = threadCreate(thumbBgThread, NULL, 32 * 1024, 0x31, -1, false);
	if (!s_async.thread)
		return false;

	return true;
}

ThumbnailStatus thumbnailPollReady(Thumbnail* out)
{
	if (!out || !s_async.done)
		return THUMB_LOADING;

	if (s_async.thread) {
		threadJoin(s_async.thread, UINT64_MAX);
		threadFree(s_async.thread);
		s_async.thread = NULL;
	}

	s_async.done = false;

	if (s_async.cached) {
		*out = s_async.result;
		s_async.cached = false;
		return out->valid ? THUMB_READY : THUMB_FAILED;
	}

	if (!s_async.ok || !s_async.rgba) {
		s_async.ok = false;
		thumbnailDeleteCache(s_async.hash);
		return THUMB_FAILED;
	}

	if (s_cacheCount >= (int)(sizeof(s_cache) / sizeof(s_cache[0]))) {
		imageFreeTexture(&s_cache[0].tex);
		for (int i = 0; i < s_cacheCount - 1; ++i)
			s_cache[i] = s_cache[i + 1];
		s_cacheCount--;
	}

	ThumbnailCacheEntry* entry = &s_cache[s_cacheCount];
	memset(entry, 0, sizeof(*entry));
	entry->hash = s_async.hash;
	bool ok = thumbnailBuildTexture(s_async.rgba, s_async.srcW, s_async.srcH, entry);
	free(s_async.rgba);
	s_async.rgba = NULL;
	s_async.ok = false;

	if (!ok) {
		imageFreeTexture(&entry->tex);
		thumbnailDeleteCache(s_async.hash);
		return THUMB_FAILED;
	}

	entry->loaded = true;
	s_cacheCount++;

	out->image = entry->image;
	out->width = (int)entry->subtex.width;
	out->height = (int)entry->subtex.height;
	out->valid = true;

	debugSetThumbInfo(0, (int)entry->subtex.width, (int)entry->subtex.height,
		(int)entry->tex.width, (int)entry->tex.height,
		entry->subtex.left, entry->subtex.top,
		entry->subtex.right, entry->subtex.bottom, "READY");
	debugLog("thumb texture %dx%d sub %.3f,%.3f,%.3f,%.3f",
		(int)entry->tex.width, (int)entry->tex.height,
		entry->subtex.left, entry->subtex.top,
		entry->subtex.right, entry->subtex.bottom);
	return THUMB_READY;
}
