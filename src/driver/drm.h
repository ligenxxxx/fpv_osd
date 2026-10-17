#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define DRM_CRTC_ID      109
#define DRM_CAMERA_PLANE 110
#define DRM_OSD_PLANE    121

#define MAKE_COLOR(r, g, b) (((r) << 16) | ((g) << 8) | (b))

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t handle;
    uint32_t size;
    uint32_t fb_id;
    uint32_t *map;
} drm_buffer_t;

int drm_open(void);
int drm_close(void);

uint32_t *drm_get_back_buffer(void);
void drm_clear_back_buffer(uint32_t color);
void drm_draw_pixel(int x, int y, uint32_t color);
int drm_swap_buffers(void);
void drm_get_resolution(int *width, int *height);

int drm_get_fd(void);
uint32_t drm_get_crtc_id(void);
uint32_t drm_get_camera_plane_id(void);

void destroy_dumb_buffer(drm_buffer_t *buf);

#ifdef __cplusplus
}
#endif
