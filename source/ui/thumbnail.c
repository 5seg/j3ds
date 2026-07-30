#include "ui/thumbnail.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <3ds.h>

#include "net/http.h"
#include "sys/sd.h"
#include "utils/image.h"

#define THUMB_CACHE_DIR "cache/thumbs"
#define THUMB_HASH_LEN  8

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

bool thumbnailDraw(const char* url, int x, int y, int w, int h)
{
	if (!url || url[0] == '\0' || w <= 0 || h <= 0)
		return false;

	if (!thumbnailEnsureCacheDir())
		return false;

	char root[512];
	sdPath(root, sizeof(root), "");

	char path[512];
	int n = snprintf(path, sizeof(path), "%s/cache/thumbs/", root);
	if (n < 0 || (size_t)n >= sizeof(path))
		return false;
	snprintf(path + n, sizeof(path) - n, "%08x.jpg", thumbnailHashUrl(url));

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

	u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
	bool ok = imageLoadJpeg(data, size, fb, x, y, w, h);

	free(data);
	return ok;
}
