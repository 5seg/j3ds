#ifndef UTILS_IMAGE_H
#define UTILS_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <3ds.h>

bool imageLoadJpeg(const void* data, size_t size, void* framebuffer, int x, int y, int w, int h);
void imageDrawRect(void* framebuffer, int x, int y, int w, int h, u8 r, u8 g, u8 b);

#endif
