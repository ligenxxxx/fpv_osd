#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "driver/drm.h"
#include "msp_displayport.h"

int drm_test() {
    if (drm_open() != 0) {
        return -1;
    }

    int width, height;
    drm_get_resolution(&width, &height);

    int pos_x = 100;
    int speed = 5;

    printf("Start OSD render loop\n");

    for (int frame = 0; frame < 500; frame++) {
        drm_clear_back_buffer(MAKE_COLOR(128, 128, 128));

        pos_x += speed;
        if (pos_x > width - 50 || pos_x < 5)
            speed = -speed;

        for (int y = 200; y < 250; y++) {
            for (int x = pos_x; x < pos_x + 50; x++) {
                drm_draw_pixel(x, y, MAKE_COLOR(0, 128, 255));
            }
        }

        drm_swap_buffers();
    }

    drm_close();
    return 0;
}

int main(void) {
    pthread_t thread_msp;
    pthread_create(&thread_msp, NULL, pthread_msp_displayport, NULL);

    // drm_test();

    pthread_join(thread_msp, NULL);

    return 0;
}