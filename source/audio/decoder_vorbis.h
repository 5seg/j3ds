#ifndef AUDIO_DECODER_VORBIS_H
#define AUDIO_DECODER_VORBIS_H

#include <3ds.h>
#include <stddef.h>

typedef struct {
	void* handle;
} VorbisDecoder;

Result vorbisInit(VorbisDecoder* dec, const char* path);
void vorbisExit(VorbisDecoder* dec);
int vorbisDecode(VorbisDecoder* dec, void* buffer, size_t bufferSize);

#endif
