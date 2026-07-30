#include "audio/decoder_mp3.h"

#include <mpg123.h>
#include <stdio.h>
#include <string.h>

Result mp3Init(Mp3Decoder* dec, const char* path)
{
	if (!dec)
		return -1;

	memset(dec, 0, sizeof(*dec));

	mpg123_init();

	mpg123_handle* mh = mpg123_new(NULL, NULL);
	if (!mh) {
		printf("mpg123_new failed\n");
		mpg123_exit();
		return -2;
	}

	dec->handle = mh;

	/* Restrict output to 16-bit signed PCM. */
	mpg123_format_none(mh);
	mpg123_format(mh, 44100, 2, MPG123_ENC_SIGNED_16);
	mpg123_format(mh, 48000, 2, MPG123_ENC_SIGNED_16);
	mpg123_format(mh, 32000, 2, MPG123_ENC_SIGNED_16);
	mpg123_format(mh, 44100, 1, MPG123_ENC_SIGNED_16);
	mpg123_format(mh, 48000, 1, MPG123_ENC_SIGNED_16);
	mpg123_format(mh, 32000, 1, MPG123_ENC_SIGNED_16);

	if (mpg123_open(mh, path) != MPG123_OK) {
		printf("mpg123_open failed: %s\n", path);
		mpg123_delete(mh);
		mpg123_exit();
		dec->handle = NULL;
		return -3;
	}

	long rate = 0;
	int channels = 0;
	int encoding = 0;
	if (mpg123_getformat(mh, &rate, &channels, &encoding) != MPG123_OK) {
		printf("mpg123_getformat failed\n");
		mpg123_close(mh);
		mpg123_delete(mh);
		mpg123_exit();
		dec->handle = NULL;
		return -4;
	}

	dec->rate = rate;
	dec->channels = channels;

	return 0;
}

void mp3Exit(Mp3Decoder* dec)
{
	if (!dec)
		return;

	if (dec->handle) {
		mpg123_close((mpg123_handle*)dec->handle);
		mpg123_delete((mpg123_handle*)dec->handle);
		dec->handle = NULL;
	}

	mpg123_exit();
}

int mp3Decode(Mp3Decoder* dec, void* buffer, size_t bufferSize)
{
	if (!dec || !dec->handle || !buffer)
		return -1;

	size_t done = 0;
	int err = mpg123_read((mpg123_handle*)dec->handle, buffer, bufferSize, &done);

	if (err == MPG123_DONE)
		return 0;

	if (err != MPG123_OK)
		return -err;

	return (int)done;
}
