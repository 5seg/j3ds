#include "ui/thumbnail.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "net/http.h"
#include "sys/sd.h"
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

	for (int y = 0; y < dstH; ++y) {
		int sy = (int)((float)y / scale);
		if (sy >= srcH) sy = srcH - 1;
		for (int x = 0; x < dstW; ++x) {
			int sx = (int)((float)x / scale);
			if (sx >= srcW) sx = srcW - 1;
			canvas[(size_t)y * texW + x] = rgba[(size_t)sy * srcW + sx];
		}
	}

	bool ok = imageUploadToTexture(&entry->tex, canvas, (int)texW, (int)texH,
		&entry->subtex, &entry->image);
	free(canvas);
	return ok;
}

void thumbnailReleaseCache(void)
{
	for (int i = 0; i < s_cacheCount; ++i)
		imageFreeTexture(&s_cache[i].tex);
	s_cacheCount = 0;
}

bool thumbnailLoad(const char* url, Thumbnail* out)
{
	if (!url || url[0] == '\0' || !out)
		return false;

	memset(out, 0, sizeof(*out));

	if (!thumbnailEnsureCacheDir())
		return false;

	unsigned int hash = thumbnailHashUrl(url);

	/* Reuse a texture already decoded for the same URL. */
	ThumbnailCacheEntry* cached = thumbnailFindCached(hash);
	if (cached) {
		out->image = cached->image;
		out->valid = true;
		return true;
	}

	/* Evict oldest entry if the cache is full. */
	if (s_cacheCount >= (int)(sizeof(s_cache) / sizeof(s_cache[0]))) {
		imageFreeTexture(&s_cache[0].tex);
		for (int i = 0; i < s_cacheCount - 1; ++i)
			s_cache[i] = s_cache[i + 1];
		s_cacheCount--;
	}

	char root[512];
	sdPath(root, sizeof(root), "");

	char path[512];
	int n = snprintf(path, sizeof(path), "%s/cache/thumbs/", root);
	if (n < 0 || (size_t)n >= sizeof(path))
		return false;
	snprintf(path + n, sizeof(path) - n, "%08x.jpg", hash);

	struct stat st;
	bool hasCache = (stat(path, &st) == 0);

	if (!hasCache) {
		Result res = httpDownloadFile(url, path);
		if (R_FAILED(res))
			return false;
	}

	void* data = NULL;
	size_t size = 0;
	if (!thumbnailReadFile(path, &data, &size))
		return false;

	u32* rgba = NULL;
	int srcW = 0, srcH = 0;
	bool ok = imageLoadJpegRgba(data, size, &rgba, &srcW, &srcH);
	free(data);

	if (!ok || !rgba) {
		free(rgba);
		return false;
	}

	ThumbnailCacheEntry* entry = &s_cache[s_cacheCount];
	memset(entry, 0, sizeof(*entry));
	entry->hash = hash;
	ok = thumbnailBuildTexture(rgba, srcW, srcH, entry);
	free(rgba);

	if (!ok) {
		imageFreeTexture(&entry->tex);
		return false;
	}

	entry->loaded = true;
	s_cacheCount++;

	out->image = entry->image;
	out->width = (int)entry->subtex.width;
	out->height = (int)entry->subtex.height;
	out->valid = true;
	return true;
}
