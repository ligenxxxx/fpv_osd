#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <libcamera/libcamera.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "camera.h"

using namespace libcamera;

static Camera *g_camera = nullptr;
static std::atomic<bool> g_running{true};
static std::atomic<unsigned int> g_frame_count{0};

static std::mutex g_mutex;
static std::condition_variable g_condition;
static bool g_camera_ready = false;

static int drm_fd = -1;
static uint32_t drm_plane_id = 98;
static uint32_t drm_crtc_id = 109;

struct DrmBuffer {
    FrameBuffer *camera_buffer = nullptr;
    uint32_t fb_id = 0;
    uint32_t handle = 0;
    int dmabuf_fd = -1;
};

static std::vector<DrmBuffer> g_drm_buffers;

static void signal_handler(int) {
    g_running = false;
    g_condition.notify_all();
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
        std::cerr << "Unexpected NV12 plane count: "
                  << planes.size() << std::endl;
        return false;
    }

    int fd0 = planes[0].fd.get();
    int fd1 = planes[1].fd.get();

    std::cout << "Import camera buffer:"
              << " fd0=" << fd0
              << " fd1=" << fd1
              << " y_length=" << planes[0].length
              << " uv_offset=" << planes[1].offset
              << " uv_length=" << planes[1].length
              << std::endl;

    uint32_t handle = 0;

    if (drmPrimeFDToHandle(drm_fd, fd0, &handle) != 0) {
        std::cerr << "drmPrimeFDToHandle failed: "
                  << strerror(errno) << std::endl;
        return false;
    }

    uint32_t handles[4] = {};
    uint32_t pitches[4] = {};
    uint32_t offsets[4] = {};

    handles[0] = handle;
    handles[1] = handle;

    pitches[0] = 1280;
    pitches[1] = 1280;

    offsets[0] = planes[0].offset;
    offsets[1] = planes[1].offset;

    uint32_t fb_id = 0;

    int ret = drmModeAddFB2(
        drm_fd,
        1280,
        720,
        DRM_FORMAT_NV12,
        handles,
        pitches,
        offsets,
        &fb_id,
        0);

    if (ret != 0) {
        std::cerr << "drmModeAddFB2 failed: "
                  << strerror(errno) << std::endl;

        drm_gem_close close_arg = {};
        close_arg.handle = handle;
        ioctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &close_arg);

        return false;
    }

    out.camera_buffer = buffer;
    out.fb_id = fb_id;
    out.handle = handle;
    out.dmabuf_fd = fd0;

    std::cout << "DRM FB created: fb_id=" << fb_id
              << " handle=" << handle << std::endl;

    return true;
}

static void destroy_drm_buffers() {
    for (auto &item : g_drm_buffers) {
        if (item.fb_id) {
            drmModeRmFB(drm_fd, item.fb_id);
            item.fb_id = 0;
        }
    }

    g_drm_buffers.clear();
}

static bool show_drm_buffer(DrmBuffer *buffer) {
    if (!buffer || !buffer->fb_id)
        return false;

    int ret = drmModeSetPlane(
        drm_fd,
        drm_plane_id,
        drm_crtc_id,
        buffer->fb_id,
        0,
        0, 0,
        720, 480,
        0 << 16,
        0 << 16,
        1280 << 16,
        720 << 16);

    if (ret != 0) {
        std::cerr << "drmModeSetPlane failed: "
                  << strerror(errno) << std::endl;
        return false;
    }

    return true;
}

static void requestComplete(Request *request) {
    if (request->status() == Request::RequestCancelled)
        return;

    if (!g_running)
        return;

    for (auto const &item : request->buffers()) {
        FrameBuffer *buffer = item.second;

        DrmBuffer *drm_buffer = find_drm_buffer(buffer);

        if (!drm_buffer) {
            std::cerr << "No DRM buffer found for FrameBuffer "
                      << buffer << std::endl;
            continue;
        }

        unsigned int frame = g_frame_count.fetch_add(1);

        if (frame < 10 || frame % 60 == 0) {
            std::cout << "Frame " << frame
                      << " -> DRM FB " << drm_buffer->fb_id
                      << " fd " << drm_buffer->dmabuf_fd
                      << std::endl;
        }

        show_drm_buffer(drm_buffer);
    }

    request->reuse(Request::ReuseBuffers);

    if (g_running)
        g_camera->queueRequest(request);
}

int camera_init() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    CameraManager cm;

    if (cm.start()) {
        std::cerr << "CameraManager start failed" << std::endl;
        return 1;
    }

    auto cameras = cm.cameras();

    if (cameras.empty()) {
        std::cerr << "No camera found" << std::endl;
        cm.stop();
        return 1;
    }

    std::cout << "Found camera: "
              << cameras[0]->id() << std::endl;

    auto camera = cameras[0];
    g_camera = camera.get();

    if (camera->acquire()) {
        std::cerr << "Camera acquire failed" << std::endl;
        cm.stop();
        return 1;
    }

    std::unique_ptr<CameraConfiguration> config =
        camera->generateConfiguration(
            {StreamRole::Viewfinder});

    if (!config) {
        std::cerr << "Failed to generate configuration"
                  << std::endl;
        camera->release();
        cm.stop();
        return 1;
    }

    StreamConfiguration &streamConfig = config->at(0);

    streamConfig.size = Size(1280, 720);
    streamConfig.pixelFormat = formats::NV12;

    std::cout << "Camera configuration before validation:"
              << std::endl;
    std::cout << "  size="
              << streamConfig.size.width << "x"
              << streamConfig.size.height << std::endl;
    std::cout << "  format="
              << streamConfig.pixelFormat.toString()
              << std::endl;

    CameraConfiguration::Status validation = config->validate();

    if (validation == CameraConfiguration::Invalid) {
        std::cerr << "Camera configuration invalid"
                  << std::endl;
        camera->release();
        cm.stop();
        return 1;
    }

    std::cout << "Camera configuration after validation:"
              << std::endl;
    std::cout << "  size="
              << streamConfig.size.width << "x"
              << streamConfig.size.height << std::endl;
    std::cout << "  format="
              << streamConfig.pixelFormat.toString()
              << std::endl;
    std::cout << "  buffer count="
              << streamConfig.bufferCount << std::endl;

    if (camera->configure(config.get())) {
        std::cerr << "Camera configure failed"
                  << std::endl;
        camera->release();
        cm.stop();
        return 1;
    }

    Stream *stream = streamConfig.stream();

    FrameBufferAllocator allocator(camera);

    if (allocator.allocate(stream) < 0) {
        std::cerr << "Failed to allocate camera buffers"
                  << std::endl;
        camera->release();
        cm.stop();
        return 1;
    }

    const auto &buffers = allocator.buffers(stream);

    std::cout << "Allocated "
              << buffers.size()
              << " camera buffers" << std::endl;

    drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);

    if (drm_fd < 0) {
        std::cerr << "Failed to open DRM: "
                  << strerror(errno) << std::endl;

        camera->release();
        cm.stop();
        return 1;
    }

    for (const auto &buffer : buffers) {
        DrmBuffer drm_buffer;

        if (!create_drm_fb(buffer.get(), drm_buffer)) {
            std::cerr << "Failed to create DRM FB"
                      << std::endl;

            destroy_drm_buffers();
            close(drm_fd);
            drm_fd = -1;

            camera->release();
            cm.stop();
            return 1;
        }

        g_drm_buffers.push_back(drm_buffer);
    }

    std::cout << "Created "
              << g_drm_buffers.size()
              << " DRM framebuffers" << std::endl;

    std::vector<std::unique_ptr<Request>> requests;

    for (const auto &buffer : buffers) {
        std::unique_ptr<Request> request =
            camera->createRequest();

        if (!request) {
            std::cerr << "Failed to create request"
                      << std::endl;

            g_running = false;
            destroy_drm_buffers();
            close(drm_fd);
            drm_fd = -1;

            camera->release();
            cm.stop();
            return 1;
        }

        if (request->addBuffer(stream, buffer.get()) < 0) {
            std::cerr << "Failed to add buffer to request"
                      << std::endl;

            g_running = false;
            destroy_drm_buffers();
            close(drm_fd);
            drm_fd = -1;

            camera->release();
            cm.stop();
            return 1;
        }

        requests.push_back(std::move(request));
    }

    camera->requestCompleted.connect(requestComplete);

    if (camera->start()) {
        std::cerr << "Camera start failed"
                  << std::endl;

        camera->requestCompleted.disconnect(requestComplete);

        destroy_drm_buffers();
        close(drm_fd);
        drm_fd = -1;

        camera->release();
        cm.stop();
        return 1;
    }

    std::cout << "Camera started." << std::endl;

    for (auto &request : requests) {
        if (camera->queueRequest(request.get())) {
            std::cerr << "Failed to queue request"
                      << std::endl;

            g_running = false;
            break;
        }
    }

    while (g_running) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(100));
    }

    std::cout << "\nStopping..." << std::endl;

    camera->stop();

    camera->requestCompleted.disconnect(requestComplete);

    if (drm_fd >= 0) {
        drmModeSetPlane(
            drm_fd,
            drm_plane_id,
            drm_crtc_id,
            0,
            0,
            0, 0,
            0, 0,
            0, 0,
            0, 0);
    }

    destroy_drm_buffers();

    if (drm_fd >= 0) {
        close(drm_fd);
        drm_fd = -1;
    }

    allocator.free(stream);

    camera->release();

    g_camera = nullptr;

    std::cout << "Exit." << std::endl;

    return 0;
}
