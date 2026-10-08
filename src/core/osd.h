#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void osd_init();
void osd_clear();
void osd_write(uint8_t row, uint8_t col, uint8_t *buf, uint8_t len);
void osd_draw();

#ifdef __cplusplus
}
#endif
