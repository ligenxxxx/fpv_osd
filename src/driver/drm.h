#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MAKE_COLOR(r, g, b) (((r) << 16) | ((g) << 8) | (b))

int drm_open(void);
int drm_close(void);

uint32_t *drm_get_back_buffer(void);
void drm_clear_back_buffer(uint32_t color);
void drm_draw_pixel(int x, int y, uint32_t color);
int drm_swap_buffers(void);
void drm_get_resolution(int *width, int *height);

#ifdef __cplusplus
}
#endif
