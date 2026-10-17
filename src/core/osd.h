#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define OSD_TRANSPARENT 0x00000000
#define OSD_WHITE       0xFFFFFFFF
#define OSD_RED         0xFFFF0000

void osd_init();
void osd_clear();
void osd_write(uint8_t row, uint8_t col, uint8_t *buf, uint8_t len);
void osd_draw();

#ifdef __cplusplus
}
#endif
