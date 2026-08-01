#ifndef AUDIO_DECODER_MP3_H
#define AUDIO_DECODER_MP3_H

#include <3ds.h>
#include <stddef.h>

typedef struct {
	void* handle;
	long rate;
	int channels;
} Mp3Decoder;

/* Streaming byte source for mpg123. read() must return up to size bytes,
   blocking until data is available, and return 0 at end of stream. */
typedef struct {
	void* user;
	ssize_t (*read)(void* user, void* buf, size_t size);
} Mp3Source;

Result mp3Init(Mp3Decoder* dec, const char* path);
Result mp3OpenStream(Mp3Decoder* dec, Mp3Source* src);
void mp3Exit(Mp3Decoder* dec);
int mp3Decode(Mp3Decoder* dec, void* buffer, size_t bufferSize);

#endif
