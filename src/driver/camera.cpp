#include <atomic>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include <drm_fourcc.h>
#include <libcamera/libcamera.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "camera.h"
#include "drm.h"

using namespace libcamera;
struct DrmBuffer {
    FrameBuffer *camera_buffer = nullptr;
    uint32_t fb_id = 0;
    uint32_t handles[2] = {};
    int dmabuf_fd = -1;
};

static std::unique_ptr<CameraManager> g_camera_manager;
static std::shared_ptr<Camera> g_camera_ref;
static Camera *g_camera = nullptr;
static std::unique_ptr<FrameBufferAllocator> g_allocator;
static std::vector<std::unique_ptr<Request>> g_requests;
static std::vector<DrmBuffer> g_drm_buffers;

static std::atomic<bool> g_started{false};
static std::atomic<unsigned int> g_frame_count{0};

static Stream *g_stream = nullptr;
static uint32_t g_width = 0;
static uint32_t g_height = 0;
static uint32_t g_stride = 0;

static void close_gem_handle(uint32_t handle) {
    if (!handle || drm_get_fd() < 0)
        return;

    drm_gem_close arg = {};
    arg.handle = handle;
    ioctl(drm_get_fd(), DRM_IOCTL_GEM_CLOSE, &arg);
}

static DrmBuffer *find_drm_buffer(FrameBuffer *buffer) {
    for (auto &item : g_drm_buffers) {
        if (item.camera_buffer == buffer)
            return &item;
    }
    return nullptr;
}

static bool create_drm_fb(FrameBuffer *buffer, DrmBuffer &out) {
    const auto &planes = buffer->planes();
    if (planes.size() != 2) {
        std::cerr << "Unexpected NV12 plane count: " << planes.size() << std::endl;
        return false;
    }

    const int fd0 = planes[0].fd.get();
    const int fd1 = planes[1].fd.get();
    uint32_t handle0 = 0;
    uint32_t handle1 = 0;

    if (drmPrimeFDToHandle(drm_get_fd(), fd0, &handle0) != 0) {
        std::cerr << "Import Y plane failed: " << strerror(errno) << std::endl;
        return false;
    }

    if (fd1 == fd0) {
        handle1 = handle0;
    } else if (drmPrimeFDToHandle(drm_get_fd(), fd1, &handle1) != 0) {
        std::cerr << "Import UV plane failed: " << strerror(errno) << std::endl;
        close_gem_handle(handle0);
        return false;
    }

    uint32_t handles[4] = {handle0, handle1, 0, 0};
    uint32_t pitches[4] = {g_stride, g_stride, 0, 0};
    uint32_t offsets[4] = {
        static_cast<uint32_t>(planes[0].offset),
        static_cast<uint32_t>(planes[1].offset), 0, 0};
    uint32_t fb_id = 0;

    if (drmModeAddFB2(drm_get_fd(), g_width, g_height, DRM_FORMAT_NV12,
                      handles, pitches, offsets, &fb_id, 0) != 0) {
        std::cerr << "drmModeAddFB2 failed: " << strerror(errno) << std::endl;
        if (handle1 != handle0)
            close_gem_handle(handle1);
        close_gem_handle(handle0);
        return false;
    }

    out.camera_buffer = buffer;
    out.fb_id = fb_id;
    out.handles[0] = handle0;
    out.handles[1] = handle1;
    out.dmabuf_fd = fd0;

    std::cout << "DRM FB created: fb_id=" << fb_id
              << " Y fd=" << fd0 << " UV fd=" << fd1
              << " stride=" << g_stride << std::endl;
    return true;
}

static void destroy_drm_buffers() {
    for (auto &item : g_drm_buffers) {
        if (item.fb_id) {
            drmModeRmFB(drm_get_fd(), item.fb_id);
            item.fb_id = 0;
        }
        close_gem_handle(item.handles[1] == item.handles[0] ? 0 : item.handles[1]);
        close_gem_handle(item.handles[0]);
        item.handles[0] = item.handles[1] = 0;
    }
    g_drm_buffers.clear();
}

static bool show_drm_buffer(DrmBuffer *buffer) {
    if (!buffer || !buffer->fb_id ||
        drm_get_camera_plane_id() == 0 || drm_get_crtc_id() == 0)
        return false;

    int tv_width = 0, tv_height = 0;
    drm_get_resolution(&tv_width, &tv_height);
    if (tv_width <= 0 || tv_height <= 0) {
        std::cerr << "Invalid TV-out resolution: "
                  << tv_width << "x" << tv_height << std::endl;
        return false;
    }

    if (drmModeSetPlane(drm_get_fd(),
                        drm_get_camera_plane_id(),
                        drm_get_crtc_id(),
                        buffer->fb_id, 0,
                        0, 0, tv_width, tv_height,
                        0, 0, g_width << 16, g_height << 16) != 0) {
        std::cerr << "drmModeSetPlane failed: "
                  << strerror(errno) << std::endl;
        return false;
    }

    return true;
}

static void requestComplete(Request *request) {
    if (!g_started || request->status() == Request::RequestCancelled)
        return;

    for (const auto &item : request->buffers()) {
        DrmBuffer *drm_buffer = find_drm_buffer(item.second);
        if (!drm_buffer) {
            std::cerr << "No DRM buffer found for FrameBuffer "
                      << item.second << std::endl;
            continue;
        }

        unsigned int frame = g_frame_count.fetch_add(1);
#if (0)
        if (frame < 10 || frame % 60 == 0) {
            std::cout << "Frame " << frame
                      << " -> DRM FB " << drm_buffer->fb_id
                      << " fd " << drm_buffer->dmabuf_fd << std::endl;
        }
#endif
        show_drm_buffer(drm_buffer);
    }

    if (!g_started)
        return;

    request->reuse(Request::ReuseBuffers);
    if (g_started && g_camera)
        g_camera->queueRequest(request);
}

void camera_stop(void) {
    if (!g_camera_manager && !g_camera && !g_allocator && g_drm_buffers.empty())
        return;

    // Prevent the completion callback from queueing any more requests.
    g_started = false;

    if (g_camera) {
        g_camera->requestCompleted.disconnect(requestComplete);
        g_camera->stop();
    }

    if (drm_get_fd() >= 0 && drm_get_crtc_id() && drm_get_camera_plane_id()) {
        drmModeSetPlane(drm_get_fd(), drm_get_camera_plane_id(), drm_get_crtc_id(), 0, 0,
                        0, 0, 0, 0, 0, 0, 0, 0);
    }

    // Camera has stopped before its DMA buffers / framebuffers are released.
    g_requests.clear();
    destroy_drm_buffers();

    if (g_allocator && g_stream)
        g_allocator->free(g_stream);
    g_allocator.reset();
    g_stream = nullptr;

    if (g_camera_ref) {
        g_camera_ref->release();
        g_camera_ref.reset();
    }
    g_camera = nullptr;

    if (g_camera_manager) {
        g_camera_manager->stop();
        g_camera_manager.reset();
    }

    g_width = g_height = g_stride = 0;
    std::cout << "Camera stopped." << std::endl;
}

int camera_start(void) {
    if (g_started) {
        std::cerr << "Camera is already started" << std::endl;
        return -1;
    }

    // Clean up any resources left by a previous partial/failed start.
    camera_stop();

    g_camera_manager = std::make_unique<CameraManager>();
    if (g_camera_manager->start()) {
        std::cerr << "CameraManager start failed" << std::endl;
        camera_stop();
        return -1;
    }

    auto cameras = g_camera_manager->cameras();
    if (cameras.empty()) {
        std::cerr << "No camera found" << std::endl;
        camera_stop();
        return -1;
    }

    g_camera_ref = cameras[0];
    g_camera = g_camera_ref.get();
    std::cout << "Found camera: " << g_camera->id() << std::endl;

    if (g_camera->acquire()) {
        std::cerr << "Camera acquire failed" << std::endl;
        camera_stop();
        return -1;
    }

    auto config = g_camera->generateConfiguration({StreamRole::Viewfinder});
    if (!config || config->empty()) {
        std::cerr << "Failed to generate camera configuration" << std::endl;
        camera_stop();
        return -1;
    }

    StreamConfiguration &stream_config = config->at(0);
    stream_config.size = Size(1280, 720);
    stream_config.pixelFormat = formats::NV12;

    CameraConfiguration::Status validation = config->validate();
    if (validation == CameraConfiguration::Invalid) {
        std::cerr << "Camera configuration invalid" << std::endl;
        camera_stop();
        return -1;
    }

    if (g_camera->configure(config.get())) {
        std::cerr << "Camera configure failed" << std::endl;
        camera_stop();
        return -1;
    }

    g_stream = stream_config.stream();
    g_width = stream_config.size.width;
    g_height = stream_config.size.height;
    g_stride = stream_config.stride;

    std::cout << "Camera configuration: " << g_width << "x" << g_height
              << " format=" << stream_config.pixelFormat.toString()
              << " stride=" << g_stride
              << " buffers=" << stream_config.bufferCount << std::endl;

    g_allocator = std::make_unique<FrameBufferAllocator>(g_camera_ref);
    if (g_allocator->allocate(g_stream) < 0) {
        std::cerr << "Failed to allocate camera buffers" << std::endl;
        camera_stop();
        return -1;
    }

    const auto &buffers = g_allocator->buffers(g_stream);
    std::cout << "Allocated " << buffers.size() << " camera buffers" << std::endl;

    if (drm_get_fd() < 0 || drm_get_crtc_id() == 0) {
        std::cerr << "DRM is not initialized" << std::endl;
        camera_stop();
        return -1;
    }
    if (drm_get_camera_plane_id() == 0) {
        std::cerr << "Camera DRM plane ID is not configured; set drm_get_camera_plane_id to the camera plane ID."
                  << std::endl;
        camera_stop();
        return -1;
    }

    for (const auto &buffer : buffers) {
        DrmBuffer drm_buffer;
        if (!create_drm_fb(buffer.get(), drm_buffer)) {
            std::cerr << "Failed to create DRM framebuffer" << std::endl;
            camera_stop();
            return -1;
        }
        g_drm_buffers.push_back(drm_buffer);
    }

    for (const auto &buffer : buffers) {
        auto request = g_camera->createRequest();
        if (!request) {
            std::cerr << "Failed to create camera request" << std::endl;
            camera_stop();
            return -1;
        }
        if (request->addBuffer(g_stream, buffer.get()) < 0) {
            std::cerr << "Failed to add buffer to request" << std::endl;
            camera_stop();
            return -1;
        }
        g_requests.push_back(std::move(request));
    }

    // Connect before start so no completion can be missed.
    g_camera->requestCompleted.connect(requestComplete);
    if (g_camera->start()) {
        std::cerr << "Camera start failed" << std::endl;
        g_camera->requestCompleted.disconnect(requestComplete);
        camera_stop();
        return -1;
    }

    g_started = true;
    g_frame_count = 0;

    for (auto &request : g_requests) {
        if (g_camera->queueRequest(request.get())) {
            std::cerr << "Failed to queue camera request" << std::endl;
            camera_stop();
            return -1;
        }
    }

    std::cout << "Camera started." << std::endl;
    return 0; // Non-blocking: return while frames continue via callback.
}