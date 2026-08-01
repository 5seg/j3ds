#include "audio/audio_player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>

#include "audio/decoder_mp3.h"
#include "net/http.h"
#include "sys/power.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define AUDIO_PATH_TEST "/3ds/j3ds/cache/audio/test.mp3"

#define STREAM_CAP       (256 * 1024)
#define STREAM_PREBUF    (96 * 1024)
#define STREAM_URL_MAX   2048

typedef enum {
	PLAYER_STOPPED,
	PLAYER_PLAYING,
	PLAYER_PAUSED
} PlayerState;

static PlayerState s_state = PLAYER_STOPPED;
static Mp3Decoder s_dec;
static Thread s_thread = NULL;
static Thread s_dlThread = NULL;
static LightEvent s_event;
static volatile bool s_quit = false;
static ndspWaveBuf s_waveBufs[3];
static int16_t* s_audioBuffer = NULL;
static char s_currentPath[512] = AUDIO_PATH_TEST;
static char s_streamUrl[STREAM_URL_MAX] = "";
static bool s_streaming = false;
static bool s_decActive = false;

static const int THREAD_AFFINITY = -1;
static const int THREAD_STACK_SZ = 32 * 1024;

typedef struct {
	LightLock lock;
	LightEvent event;
	u8* data;
	size_t cap;
	size_t head;
	size_t count;
	bool eof;
} StreamSource;

static StreamSource s_stream;

static void streamInit(StreamSource* s)
{
	LightLock_Init(&s->lock);
	LightEvent_Init(&s->event, RESET_STICKY);
	s->data = (u8*)linearAlloc(STREAM_CAP);
	s->cap = s->data ? STREAM_CAP : 0;
	s->head = 0;
	s->count = 0;
	s->eof = false;
}

static void streamFree(StreamSource* s)
{
	if (s->data) {
		linearFree(s->data);
		s->data = NULL;
	}
	s->cap = 0;
	s->head = 0;
	s->count = 0;
	s->eof = false;
}

static size_t streamPush(StreamSource* s, const u8* data, size_t size)
{
	LightLock_Lock(&s->lock);
	while (s->cap > 0 && size > s->cap - s->count && !s->eof) {
		LightEvent_Clear(&s->event);
		LightLock_Unlock(&s->lock);
		LightEvent_Wait(&s->event);
		LightLock_Lock(&s->lock);
	}
	if (s->eof || s->cap == 0) {
		LightLock_Unlock(&s->lock);
		return 0;
	}

	size_t n = size;
	if (n > s->cap - s->count)
		n = s->cap - s->count;

	for (size_t i = 0; i < n; ++i)
		s->data[(s->head + s->count + i) % s->cap] = data[i];
	s->count += n;

	LightEvent_Signal(&s->event);
	LightLock_Unlock(&s->lock);
	return n;
}

static size_t streamPop(StreamSource* s, u8* out, size_t size)
{
	LightLock_Lock(&s->lock);
	while (s->count == 0 && !s->eof) {
		LightEvent_Clear(&s->event);
		LightLock_Unlock(&s->lock);
		LightEvent_Wait(&s->event);
		LightLock_Lock(&s->lock);
	}
	if (s->count == 0) {
		LightLock_Unlock(&s->lock);
		return 0;
	}

	size_t n = size < s->count ? size : s->count;
	for (size_t i = 0; i < n; ++i)
		out[i] = s->data[(s->head + i) % s->cap];
	s->head = (s->head + n) % s->cap;
	s->count -= n;

	LightEvent_Signal(&s->event);
	LightLock_Unlock(&s->lock);
	return n;
}

static void streamMarkEof(StreamSource* s)
{
	LightLock_Lock(&s->lock);
	s->eof = true;
	LightEvent_Signal(&s->event);
	LightLock_Unlock(&s->lock);
}

static void streamWaitData(StreamSource* s, size_t minBytes)
{
	for (;;) {
		LightLock_Lock(&s->lock);
		if (s->eof || s->count >= minBytes) {
			LightLock_Unlock(&s->lock);
			return;
		}
		LightEvent_Clear(&s->event);
		LightLock_Unlock(&s->lock);
		LightEvent_Wait(&s->event);
	}
}

static ssize_t streamSourceRead(void* user, void* buf, size_t size)
{
	StreamSource* s = (StreamSource*)user;
	return (ssize_t)streamPop(s, (u8*)buf, size);
}

static bool streamChunkSink(const void* data, size_t size, void* user)
{
	(void)user;
	if (s_quit)
		return false;
	return streamPush(&s_stream, (const u8*)data, size) > 0;
}

static void downloadThread(void* arg)
{
	(void)arg;
	httpDownloadToSink(s_streamUrl, streamChunkSink, NULL);
	streamMarkEof(&s_stream);
}

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

/* Runs on the audio thread. s_dec must already be initialized so that
   rate/channels are known. */
static void audioRunPlayback(void)
{
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
		s_state = PLAYER_STOPPED;
		return;
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

	/* Let the stream fill a little before starting to avoid an underrun. */
	if (s_streaming)
		streamWaitData(&s_stream, STREAM_PREBUF);

	if (s_quit) {
		s_state = PLAYER_STOPPED;
		return;
	}

	s_state = PLAYER_PLAYING;
	ndspSetCallback(audioCallback, NULL);

	while (!s_quit) {
		if (s_state == PLAYER_PAUSED) {
			LightEvent_Wait(&s_event);
			if (s_quit)
				break;
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

	s_state = PLAYER_STOPPED;
}

static void audioThreadStream(void* arg)
{
	(void)arg;

	Mp3Source src = { &s_stream, streamSourceRead };
	if (R_FAILED(mp3OpenStream(&s_dec, &src))) {
		s_state = PLAYER_STOPPED;
		return;
	}
	s_decActive = true;
	audioRunPlayback();
}

static void audioThreadFile(void* arg)
{
	(void)arg;

	if (R_FAILED(mp3Init(&s_dec, s_currentPath))) {
		s_state = PLAYER_STOPPED;
		return;
	}
	s_decActive = true;
	audioRunPlayback();
}

static void audioStartThread(void (*entry)(void*))
{
	s_quit = false;

	int32_t priority = 0x30;
	svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
	priority -= 1;
	priority = priority < 0x18 ? 0x18 : priority;
	priority = priority > 0x3F ? 0x3F : priority;

	s_thread = threadCreate(entry, NULL, THREAD_STACK_SZ, priority,
		THREAD_AFFINITY, false);
	if (s_thread)
		powerPreventSleep();
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

Result audioPlayStream(const char* url)
{
	if (!url || url[0] == '\0')
		return -1;

	audioStop();

	strncpy(s_streamUrl, url, sizeof(s_streamUrl) - 1);
	s_streamUrl[sizeof(s_streamUrl) - 1] = '\0';

	streamInit(&s_stream);
	s_streaming = true;
	s_state = PLAYER_STOPPED;
	s_quit = false;

	s_dlThread = threadCreate(downloadThread, NULL, THREAD_STACK_SZ, 0x3F,
		THREAD_AFFINITY, false);
	if (!s_dlThread) {
		streamFree(&s_stream);
		s_streaming = false;
		return -2;
	}

	audioStartThread(audioThreadStream);
	if (!s_thread) {
		s_quit = true;
		streamMarkEof(&s_stream);
		threadJoin(s_dlThread, UINT64_MAX);
		threadFree(s_dlThread);
		s_dlThread = NULL;
		streamFree(&s_stream);
		s_streaming = false;
		return -3;
	}

	return 0;
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

	s_streaming = false;
	s_state = PLAYER_STOPPED;

	audioStartThread(audioThreadFile);
	if (!s_thread)
		return -2;

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
	if (s_state == PLAYER_STOPPED && !s_thread && !s_dlThread)
		return;

	s_quit = true;
	LightEvent_Signal(&s_event);
	streamMarkEof(&s_stream);

	if (s_thread) {
		threadJoin(s_thread, UINT64_MAX);
		threadFree(s_thread);
		s_thread = NULL;
	}

	if (s_dlThread) {
		threadJoin(s_dlThread, UINT64_MAX);
		threadFree(s_dlThread);
		s_dlThread = NULL;
	}

	ndspChnReset(0);

	if (s_decActive) {
		mp3Exit(&s_dec);
		s_decActive = false;
	}

	if (s_audioBuffer) {
		linearFree(s_audioBuffer);
		s_audioBuffer = NULL;
	}

	if (s_streaming) {
		streamFree(&s_stream);
		s_streaming = false;
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
