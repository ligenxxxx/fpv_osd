#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define FONT_WIDTH  24
#define FONT_HEIGHT 36
#define FONT_SIZE   (24 * 36)

void font_init(unsigned char *bmp_path);

extern uint32_t osd_font[384 * 1152];

#ifdef __cplusplus
}
#endif
