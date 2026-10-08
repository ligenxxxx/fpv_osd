#include <stdint.h>

#include "drm.h"
#include "font.h"
#include "osd.h"

void osd_init() {
    drm_open();
}

void osd_clear() {
    drm_clear_back_buffer(MAKE_COLOR(127, 127, 127));
}

void osd_write(uint8_t row, uint8_t col, uint8_t *buf, uint8_t len) {
    int i, j, k;
    for (k = 0; k < len; k++) {
        uint32_t *cur_font = &osd_font[FONT_SIZE * buf[k]];
        for (j = 0; j < FONT_HEIGHT; j++) {
            for (i = 0; i < FONT_WIDTH; i++) {
                uint32_t pixel = cur_font[j * FONT_WIDTH + i];
                int pos_x = (col + k) * FONT_WIDTH + i;
                int pos_y = row * FONT_HEIGHT + j;
                drm_draw_pixel(pos_x, pos_y, pixel);
            }
        }
    }
}

void osd_draw() {
    drm_swap_buffers();
}