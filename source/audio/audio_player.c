#include "audio/audio_player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>

#include "audio/decoder_mp3.h"
#include "sys/power.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define AUDIO_PATH_TEST "/3ds/j3ds/cache/audio/test.mp3"

typedef enum {
	PLAYER_STOPPED,
	PLAYER_PLAYING,
	PLAYER_PAUSED
} PlayerState;

static PlayerState s_state = PLAYER_STOPPED;
static Mp3Decoder s_dec;
static Thread s_thread = NULL;
static LightEvent s_event;
static volatile bool s_quit = false;
static ndspWaveBuf s_waveBufs[3];
static int16_t* s_audioBuffer = NULL;
static char s_currentPath[512] = AUDIO_PATH_TEST;

static const int THREAD_AFFINITY = -1;
static const int THREAD_STACK_SZ = 32 * 1024;

static void audioCallback(void* arg)
{
	(void)arg;

	if (s_quit)
		return;

	LightEvent_Signal(&s_event);
}

static bool fillBuffer(Mp3Decoder* dec, ndspWaveBuf* waveBuf)
{
	int totalBytes = 0;
	const int maxBytes = (int)waveBuf->nsamples * (int)sizeof(int16_t);

	while (totalBytes < maxBytes) {
		int16_t* buffer = waveBuf->data_pcm16 + (totalBytes / sizeof(int16_t));
		const size_t remaining = (size_t)(maxBytes - totalBytes);

		int bytesRead = mp3Decode(dec, buffer, remaining);
		if (bytesRead < 0) {
			printf("mp3Decode error %d\n", bytesRead);
			break;
		}
		if (bytesRead == 0)
			break;

		totalBytes += bytesRead;
	}

	if (totalBytes == 0) {
		printf("Playback complete\n");
		return false;
	}

	waveBuf->nsamples = (size_t)(totalBytes / sizeof(int16_t));
	ndspChnWaveBufAdd(0, waveBuf);
	DSP_FlushDataCache(waveBuf->data_pcm16, (u32)totalBytes);

	return true;
}

static void audioThread(void* arg)
{
	(void)arg;

	while (!s_quit) {
		if (s_state == PLAYER_PAUSED) {
			LightEvent_Wait(&s_event);
			if (s_quit)
				return;
			continue;
		}

		for (size_t i = 0; i < ARRAY_SIZE(s_waveBufs); ++i) {
			if (s_waveBufs[i].status != NDSP_WBUF_DONE)
				continue;

			if (!fillBuffer(&s_dec, &s_waveBufs[i])) {
				s_state = PLAYER_STOPPED;
				return;
			}
		}

		LightEvent_Wait(&s_event);
	}
}

Result audioInit(void)
{
	Result res = ndspInit();
	if (R_FAILED(res)) {
		printf("ndspInit failed: %08lX\n", (unsigned long)res);
		return res;
	}

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
	ndspChnSetRate(0, 44100);

	LightEvent_Init(&s_event, RESET_ONESHOT);

	return 0;
}

void audioExit(void)
{
	audioStop();
	ndspExit();
}

Result audioPlay(const char* path)
{
	if (path && path[0] != '\0') {
		strncpy(s_currentPath, path, sizeof(s_currentPath) - 1);
		s_currentPath[sizeof(s_currentPath) - 1] = '\0';
	} else {
		strncpy(s_currentPath, AUDIO_PATH_TEST, sizeof(s_currentPath) - 1);
		s_currentPath[sizeof(s_currentPath) - 1] = '\0';
	}

	audioStop();

	Result res = mp3Init(&s_dec, s_currentPath);
	if (R_FAILED(res)) {
		printf("mp3Init failed: %08lX\n", (unsigned long)res);
		return res;
	}

	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
	ndspChnSetFormat(0, s_dec.channels == 1
		? NDSP_FORMAT_MONO_PCM16
		: NDSP_FORMAT_STEREO_PCM16);
	ndspChnSetRate(0, (float)s_dec.rate);

	const size_t samplesPerBuf = (size_t)(s_dec.rate * 120 / 1000);
	const size_t channels = (size_t)s_dec.channels;
	const size_t waveBufSize = samplesPerBuf * channels * sizeof(int16_t);
	const size_t bufferSize = waveBufSize * ARRAY_SIZE(s_waveBufs);

	s_audioBuffer = (int16_t*)linearAlloc(bufferSize);
	if (!s_audioBuffer) {
		printf("Failed to allocate audio buffer\n");
		mp3Exit(&s_dec);
		return -1;
	}

	memset(&s_waveBufs, 0, sizeof(s_waveBufs));
	memset(s_audioBuffer, 0, bufferSize);

	int16_t* buffer = s_audioBuffer;
	for (size_t i = 0; i < ARRAY_SIZE(s_waveBufs); ++i) {
		s_waveBufs[i].data_vaddr = buffer;
		s_waveBufs[i].nsamples = waveBufSize / sizeof(int16_t);
		s_waveBufs[i].status = NDSP_WBUF_DONE;
		buffer += waveBufSize / sizeof(int16_t);
	}

	s_quit = false;
	s_state = PLAYER_PLAYING;

	ndspSetCallback(audioCallback, NULL);

	int32_t priority = 0x30;
	svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
	priority -= 1;
	priority = priority < 0x18 ? 0x18 : priority;
	priority = priority > 0x3F ? 0x3F : priority;

	s_thread = threadCreate(audioThread, NULL, THREAD_STACK_SZ, priority,
		THREAD_AFFINITY, false);
	if (!s_thread) {
		printf("Failed to create audio thread\n");
		linearFree(s_audioBuffer);
		s_audioBuffer = NULL;
		mp3Exit(&s_dec);
		s_state = PLAYER_STOPPED;
		return -2;
	}

	powerPreventSleep();
	return 0;
}

void audioPause(void)
{
	if (s_state == PLAYER_PLAYING) {
		ndspChnSetPaused(0, true);
		s_state = PLAYER_PAUSED;
		powerAllowSleep();
	} else if (s_state == PLAYER_PAUSED) {
		ndspChnSetPaused(0, false);
		s_state = PLAYER_PLAYING;
		powerPreventSleep();
		LightEvent_Signal(&s_event);
	}
}

void audioStop(void)
{
	if (s_state == PLAYER_STOPPED && !s_thread)
		return;

	s_quit = true;
	LightEvent_Signal(&s_event);

	if (s_thread) {
		threadJoin(s_thread, UINT64_MAX);
		threadFree(s_thread);
		s_thread = NULL;
	}

	ndspChnReset(0);
	mp3Exit(&s_dec);

	if (s_audioBuffer) {
		linearFree(s_audioBuffer);
		s_audioBuffer = NULL;
	}

	powerAllowSleep();
	s_state = PLAYER_STOPPED;
}

bool audioIsPlaying(void)
{
	return s_state == PLAYER_PLAYING || s_state == PLAYER_PAUSED;
}

bool audioIsPaused(void)
{
	return s_state == PLAYER_PAUSED;
}
