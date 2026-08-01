#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <3ds.h>
#include <stdbool.h>

Result audioInit(void);
void audioExit(void);
Result audioPlay(const char* path);
Result audioPlayStream(const char* url);
void audioPause(void);
void audioStop(void);
bool audioIsPlaying(void);
bool audioIsPaused(void);

#endif
