#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "driver/camera.h"
#include "driver/drm.h"
#include "msp_displayport.h"

int main(void) {
    if (drm_open() < 0) {
        printf("drm open failed.\n");
        return -1;
    }
    if (camera_start() < 0) {
        return -2;

        printf("camera_start\n");
    }

    pthread_t thread_msp;
    int ret = pthread_create(&thread_msp, NULL, pthread_msp_displayport, NULL);
    if (ret != 0) {
        printf("pthread_create failed: %d\n", ret);
        return -1;
    }
    pthread_join(thread_msp, NULL);

    return 0;
}