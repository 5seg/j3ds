#include "audio/decoder_vorbis.h"

Result vorbisInit(VorbisDecoder* dec, const char* path)
{
	(void)path;
	if (dec)
		dec->handle = NULL;
	return 0;
}

void vorbisExit(VorbisDecoder* dec)
{
	if (dec)
		dec->handle = NULL;
}

int vorbisDecode(VorbisDecoder* dec, void* buffer, size_t bufferSize)
{
	(void)dec;
	(void)buffer;
	(void)bufferSize;
	return 0;
}
