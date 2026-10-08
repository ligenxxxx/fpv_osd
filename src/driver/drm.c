#include <fcntl.h>
#include <libdrm/drm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "drm.h"

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t handle;
    uint32_t size;
    uint32_t fb_id;
    uint32_t *map;
} drm_buffer_t;

static int fd = -1;
static drmModeRes *resources = NULL;
static drmModeConnector *connector = NULL;
static drmModeEncoder *encoder = NULL;
static drmModeCrtc *saved_crtc = NULL;
static drmModeModeInfo mode;

static uint32_t conn_id;
static uint32_t crtc_id;

static drm_buffer_t buffers[2];
static int front_buf_idx = 0;
static int page_flip_pending = 0;

static void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
                              unsigned int tv_usec, void *user_data) {
    (void)fd;
    (void)sequence;
    (void)tv_sec;
    (void)tv_usec;
    (void)user_data;

    page_flip_pending = 0;
}

static drmEventContext evctx = {
    .version = 2,
    .page_flip_handler = page_flip_handler,
};

static int create_dumb_buffer(drm_buffer_t *buf) {
    struct drm_mode_create_dumb create_req = {0};
    create_req.width = mode.hdisplay;
    create_req.height = mode.vdisplay;
    create_req.bpp = 32; // XRGB8888

    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_req) < 0) {
        perror("DRM_IOCTL_MODE_CREATE_DUMB failed");
        return -1;
    }

    buf->width = create_req.width;
    buf->height = create_req.height;
    buf->pitch = create_req.pitch;
    buf->handle = create_req.handle;
    buf->size = create_req.size;

    if (drmModeAddFB(fd, mode.hdisplay, mode.vdisplay, 24, 32,
                     buf->pitch, buf->handle, &buf->fb_id) < 0) {
        perror("drmModeAddFB failed");
        return -1;
    }

    struct drm_mode_map_dumb map_req = {0};
    map_req.handle = buf->handle;

    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) < 0) {
        perror("DRM_IOCTL_MODE_MAP_DUMB failed");
        return -1;
    }

    buf->map = mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map_req.offset);
    if (buf->map == MAP_FAILED) {
        perror("mmap failed");
        return -1;
    }

    // clear as black
    memset(buf->map, 0, buf->size);
    return 0;
}

// destroy Dumb Buffer
static void destroy_dumb_buffer(drm_buffer_t *buf) {
    if (buf->map && buf->map != MAP_FAILED) {
        munmap(buf->map, buf->size);
    }
    if (buf->fb_id) {
        drmModeRmFB(fd, buf->fb_id);
    }
    if (buf->handle) {
        struct drm_mode_destroy_dumb destroy_req = {.handle = buf->handle};
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_req);
    }
}

int drm_open(void) {
    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("Can't open /dev/dri/card0");
        return -1;
    }

    resources = drmModeGetResources(fd);
    if (!resources) {
        perror("Get DRM resource failed");
        close(fd);
        return -1;
    }

    for (int i = 0; i < resources->count_connectors; i++) {
        drmModeConnector *conn = drmModeGetConnector(fd, resources->connectors[i]);
        if (!conn)
            continue;

        if ((conn->connection == DRM_MODE_CONNECTED ||
             conn->connector_type == DRM_MODE_CONNECTOR_Composite) &&
            conn->count_modes > 0) {
            connector = conn;
            break;
        }
        drmModeFreeConnector(conn);
    }

    if (!connector) {
        fprintf(stderr, "No display device is found\n");
        drmModeFreeResources(resources);
        close(fd);
        return -1;
    }

    conn_id = connector->connector_id;
    mode = connector->modes[0];
    printf("[DRM] resolution: %dx%d @ %dHz\n", mode.hdisplay, mode.vdisplay, mode.vrefresh);

    if (connector->encoder_id) {
        encoder = drmModeGetEncoder(fd, connector->encoder_id);
    }
    if (encoder && encoder->crtc_id) {
        crtc_id = encoder->crtc_id;
    } else {
        crtc_id = resources->crtcs[0];
    }

    saved_crtc = drmModeGetCrtc(fd, crtc_id);

    if (create_dumb_buffer(&buffers[0]) < 0 || create_dumb_buffer(&buffers[1]) < 0) {
        drm_close();
        return -1;
    }

    front_buf_idx = 0;
    if (drmModeSetCrtc(fd, crtc_id, buffers[front_buf_idx].fb_id, 0, 0, &conn_id, 1, &mode) < 0) {
        perror("drmModeSetCrtc failed");
        drm_close();
        return -1;
    }

    return 0;
}

uint32_t *drm_get_back_buffer(void) {
    int back_idx = 1 - front_buf_idx;
    return buffers[back_idx].map;
}

void drm_clear_back_buffer(uint32_t color) {
    uint32_t *back_map = drm_get_back_buffer();
    uint32_t pixel_count = buffers[0].size / 4;
    for (uint32_t i = 0; i < pixel_count; i++) {
        back_map[i] = color;
    }
}

void drm_draw_pixel(int x, int y, uint32_t color) {
    if (x < 0 || x >= (int)mode.hdisplay || y < 0 || y >= (int)mode.vdisplay)
        return;
    uint32_t *back_map = drm_get_back_buffer();
    back_map[y * mode.hdisplay + x] = color;
}

int drm_swap_buffers(void) {
    int back_idx = 1 - front_buf_idx;

    if (drmModePageFlip(fd, crtc_id, buffers[back_idx].fb_id, DRM_MODE_PAGE_FLIP_EVENT, NULL) < 0) {
        perror("drmModePageFlip failed");
        return -1;
    }

    page_flip_pending = 1;

    while (page_flip_pending) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        int ret = select(fd + 1, &fds, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select error");
            break;
        }
        if (ret > 0 && FD_ISSET(fd, &fds)) {
            drmHandleEvent(fd, &evctx);
        }
    }

    front_buf_idx = back_idx;
    return 0;
}

void drm_get_resolution(int *width, int *height) {
    if (width)
        *width = mode.hdisplay;
    if (height)
        *height = mode.vdisplay;
}

int drm_close(void) {
    if (saved_crtc) {
        drmModeSetCrtc(fd, saved_crtc->crtc_id, saved_crtc->buffer_id,
                       saved_crtc->x, saved_crtc->y, &conn_id, 1, &saved_crtc->mode);
        drmModeFreeCrtc(saved_crtc);
    }

    destroy_dumb_buffer(&buffers[0]);
    destroy_dumb_buffer(&buffers[1]);

    if (encoder)
        drmModeFreeEncoder(encoder);
    if (connector)
        drmModeFreeConnector(connector);
    if (resources)
        drmModeFreeResources(resources);

    if (fd >= 0)
        close(fd);
    return 0;
}