#ifndef SYS_SD_H
#define SYS_SD_H

#include <stddef.h>

const char* sdGetRoot(void);
void sdPath(char* out, size_t outLen, const char* relative);

#endif
