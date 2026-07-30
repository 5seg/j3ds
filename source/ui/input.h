#ifndef UI_INPUT_H
#define UI_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <3ds.h>

bool inputShowKeyboard(char* out, size_t outLen, const char* hint, const char* initial);
bool inputShowKeyboardPassword(char* out, size_t outLen, const char* hint);
u32 inputGetKeysDown(void);
u32 inputGetKeysHeld(void);

#endif
