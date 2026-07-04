#include "phone_os/camera_service.h"

#include "rodakos_adapters/file_service.h"

#include "sdkconfig.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <inttypes.h>

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_jpeg_enc.h>
#include <esp_log.h>
#include <esp_log_level.h>
#include <esp_timer.h>

#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace rodakos {
namespace {
constexpr const char* TAG = "CameraService";
constexpr const char* kPhotoDir = "/photos";
constexpr int kBufferCount = 2;
constexpr uint32_t kPreviewTaskStackSize = 4096;
constexpr uint8_t kJpegQuality = 82;
constexpr int64_t kMinValidUnixTime = 1700000000;
constexpr int kMaxPhotoNameSuffix = 9999;
constexpr const char* kGpioLogTag = "gpio";

#if configSUPPORT_STATIC_ALLOCATION == 1
StaticTask_t g_preview_task_buffer;
StackType_t g_preview_task_stack[kPreviewTaskStackSize];
#endif

const char* ErrnoName() {
    return std::strerror(errno);
}

void LogPreviewTaskCreateFailure() {
    ESP_LOGW(TAG,
             "Failed to start camera preview task: internal_free=%u internal_largest=%u "
             "spiram_free=%u spiram_largest=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

std::string JoinPath(const char* dir, const char* name) {
    std::string path = dir != nullptr ? dir : "";
    if (!path.empty() && path.back() != '/') {
        path.push_back('/');
    }
    path += name != nullptr ? name : "";
    return path;
}

std::vector<uint8_t> PackRgb565Frame(const CameraFrame& frame) {
    const int packed_stride = frame.width * 2;
    const size_t packed_size = static_cast<size_t>(packed_stride) * frame.height;
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < packed_stride ||
        frame.rgb565.size() < static_cast<size_t>(frame.stride) * frame.height) {
        return {};
    }
    if (frame.stride == packed_stride) {
        return frame.rgb565;
    }

    std::vector<uint8_t> packed(packed_size);
    for (int y = 0; y < frame.height; ++y) {
        const auto* src = frame.rgb565.data() + static_cast<size_t>(y) * frame.stride;
        auto* dst = packed.data() + static_cast<size_t>(y) * packed_stride;
        std::memcpy(dst, src, packed_stride);
    }
    return packed;
}

bool CopyRgb565LeToRgb888(const std::vector<uint8_t>& rgb565, int width, int height,
                          uint8_t* rgb888, size_t rgb888_size) {
    if (width <= 0 || height <= 0 || rgb888 == nullptr) {
        return false;
    }

    const size_t pixel_count = static_cast<size_t>(width) * height;
    if (rgb565.size() < pixel_count * 2 || rgb888_size < pixel_count * 3) {
        return false;
    }

    for (size_t i = 0; i < pixel_count; ++i) {
        const uint16_t pixel = static_cast<uint16_t>(rgb565[i * 2]) |
                               (static_cast<uint16_t>(rgb565[i * 2 + 1]) << 8);
        const uint8_t r5 = static_cast<uint8_t>((pixel >> 11) & 0x1f);
        const uint8_t g6 = static_cast<uint8_t>((pixel >> 5) & 0x3f);
        const uint8_t b5 = static_cast<uint8_t>(pixel & 0x1f);

        rgb888[i * 3] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        rgb888[i * 3 + 1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
        rgb888[i * 3 + 2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    }
    return true;
}

void RotateRgb565Frame180(CameraFrame& frame) {
    const int row_bytes = frame.width * 2;
    const size_t frame_size = static_cast<size_t>(frame.stride) * frame.height;
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < row_bytes ||
        frame.rgb565.size() < frame_size) {
        return;
    }

    if (frame.stride == row_bytes) {
        const size_t pixel_count = static_cast<size_t>(frame.width) * frame.height;
        for (size_t i = 0, j = pixel_count - 1; i < j; ++i, --j) {
            std::swap(frame.rgb565[i * 2], frame.rgb565[j * 2]);
            std::swap(frame.rgb565[i * 2 + 1], frame.rgb565[j * 2 + 1]);
        }
        return;
    }

    std::vector<uint8_t> rotated(frame_size);
    for (int y = 0; y < frame.height; ++y) {
        const auto* src_row = frame.rgb565.data() + static_cast<size_t>(y) * frame.stride;
        auto* dst_row = rotated.data() + static_cast<size_t>(frame.height - 1 - y) * frame.stride;
        for (int x = 0; x < frame.width; ++x) {
            const auto* src = src_row + x * 2;
            auto* dst = dst_row + (frame.width - 1 - x) * 2;
            dst[0] = src[0];
            dst[1] = src[1];
        }
    }
    frame.rgb565 = std::move(rotated);
}

uint8_t* AllocAlignedJpegInput(size_t size) {
    auto* buffer = static_cast<uint8_t*>(
        heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        buffer = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, size, MALLOC_CAP_8BIT));
    }
    return buffer;
}

#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
int StreamOnSuppressingBenignGpioIsrLog(int fd, int* type) {
    // The DVP driver intentionally ignores gpio_install_isr_service() when another device installed it first.
    const esp_log_level_t previous_level = esp_log_level_get(kGpioLogTag);
    esp_log_level_set(kGpioLogTag, ESP_LOG_NONE);
    const int ret = ioctl(fd, VIDIOC_STREAMON, type);
    esp_log_level_set(kGpioLogTag, previous_level);
    return ret;
}

void CopyRgb565Frame(const uint8_t* src, size_t src_size, int stride, int height,
                     uint32_t pixelformat, std::vector<uint8_t>& dst) {
    const size_t frame_size = static_cast<size_t>(stride) * height;
    if (src == nullptr || src_size < frame_size || stride <= 0 || height <= 0) {
        dst.clear();
        return;
    }

    dst.resize(frame_size);
    if (pixelformat != V4L2_PIX_FMT_RGB565X) {
        std::memcpy(dst.data(), src, frame_size);
        return;
    }

    for (size_t i = 0; i + 1 < frame_size; i += 2) {
        dst[i] = src[i + 1];
        dst[i + 1] = src[i];
    }
}

const char* PixelFormatName(uint32_t pixelformat) {
    switch (pixelformat) {
        case V4L2_PIX_FMT_RGB565:
            return "RGB565";
        case V4L2_PIX_FMT_RGB565X:
            return "RGB565X";
        default:
            return "unknown";
    }
}
#endif

}  // namespace

CameraService::CameraService(FileService* file_service) : file_service_(file_service) {
    mutex_ = xSemaphoreCreateMutex();
}

CameraService::~CameraService() {
    StopPreview();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool CameraService::IsAvailable() const {
    return camera_device_.IsConfigured();
}

bool CameraService::StartPreview(int width, int height) {
    if (!IsAvailable()) {
        SetError("Camera device is not configured");
        return false;
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (preview_running_) {
            xSemaphoreGive(mutex_);
            return true;
        }
        stop_requested_ = false;
        has_frame_ = false;
        frame_count_ = 0;
        latest_frame_ = {};
        xSemaphoreGive(mutex_);
    }

    if (!OpenStream(width, height)) {
        CloseStream();
        return false;
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        preview_running_ = true;
        xSemaphoreGive(mutex_);
    }

    TaskHandle_t task_handle = nullptr;
#if configSUPPORT_STATIC_ALLOCATION == 1
    task_handle = xTaskCreateStaticPinnedToCore(
        PreviewTaskEntry, "camera_preview", kPreviewTaskStackSize, this, 3,
        g_preview_task_stack, &g_preview_task_buffer,
#if CONFIG_SOC_CPU_CORES_NUM > 1
        0
#else
        tskNO_AFFINITY
#endif
    );
    preview_task_ = task_handle;
#else
    const BaseType_t task_ret =
#if CONFIG_SOC_CPU_CORES_NUM > 1
        xTaskCreatePinnedToCore(PreviewTaskEntry, "camera_preview", kPreviewTaskStackSize,
                                this, 3, &preview_task_, 0);
#else
        xTaskCreate(PreviewTaskEntry, "camera_preview", kPreviewTaskStackSize,
                    this, 3, &preview_task_);
#endif
    if (task_ret == pdPASS) {
        task_handle = preview_task_;
    }
#endif
    if (task_handle == nullptr) {
        LogPreviewTaskCreateFailure();
        SetError("Failed to start camera preview task");
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            preview_running_ = false;
            xSemaphoreGive(mutex_);
        }
        CloseStream();
        return false;
    }

    ESP_LOGI(TAG, "Camera preview started: %dx%d stride=%d format=%s",
             active_width_, active_height_, active_stride_,
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
             PixelFormatName(active_pixelformat_)
#else
             "n/a"
#endif
    );
    return true;
}

void CameraService::StopPreview() {
    bool should_wait = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        should_wait = preview_task_ != nullptr;
        stop_requested_ = true;
        xSemaphoreGive(mutex_);
    }

    if (should_wait) {
        for (int waited = 0; waited < 2000; waited += 20) {
            vTaskDelay(pdMS_TO_TICKS(20));
            if (mutex_ != nullptr) {
                xSemaphoreTake(mutex_, portMAX_DELAY);
                should_wait = preview_task_ != nullptr;
                xSemaphoreGive(mutex_);
            }
            if (!should_wait) {
                break;
            }
        }
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool task_still_alive = preview_task_ != nullptr;
        xSemaphoreGive(mutex_);
        if (task_still_alive) {
            ESP_LOGW(TAG, "Camera preview task did not stop in time");
        }
    }
}

bool CameraService::GetLatestFrame(CameraFrame& frame) {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool ok = has_frame_;
    if (ok) {
        frame = latest_frame_;
    }
    xSemaphoreGive(mutex_);
    return ok;
}

bool CameraService::CapturePhoto(std::string& saved_path) {
    CameraFrame frame;
    if (!GetLatestFrame(frame)) {
        SetError("No camera frame is ready yet");
        return false;
    }

    auto packed = PackRgb565Frame(frame);
    if (packed.empty()) {
        SetError("Camera frame is incomplete");
        return false;
    }

    const size_t rgb888_size = static_cast<size_t>(frame.width) * frame.height * 3;
    auto* aligned_input = AllocAlignedJpegInput(rgb888_size);
    if (aligned_input == nullptr) {
        SetError("Not enough memory for JPEG input");
        return false;
    }
    if (!CopyRgb565LeToRgb888(packed, frame.width, frame.height, aligned_input, rgb888_size)) {
        heap_caps_free(aligned_input);
        SetError("Failed to convert camera frame");
        return false;
    }

    jpeg_enc_config_t jpeg_cfg = DEFAULT_JPEG_ENC_CONFIG();
    jpeg_cfg.width = frame.width;
    jpeg_cfg.height = frame.height;
    jpeg_cfg.src_type = JPEG_PIXEL_FORMAT_RGB888;
    jpeg_cfg.subsampling = JPEG_SUBSAMPLE_420;
    jpeg_cfg.quality = kJpegQuality;
    jpeg_cfg.task_enable = false;

    jpeg_enc_handle_t encoder = nullptr;
    if (jpeg_enc_open(&jpeg_cfg, &encoder) != JPEG_ERR_OK || encoder == nullptr) {
        heap_caps_free(aligned_input);
        SetError("Failed to open JPEG encoder");
        return false;
    }

    const size_t out_capacity = std::max<size_t>(64 * 1024, rgb888_size);
    std::vector<uint8_t> encoded(out_capacity);
    int out_len = 0;
    const jpeg_error_t enc_ret = jpeg_enc_process(encoder, aligned_input,
                                                  static_cast<int>(rgb888_size),
                                                  encoded.data(),
                                                  static_cast<int>(encoded.size()),
                                                  &out_len);
    jpeg_enc_close(encoder);
    heap_caps_free(aligned_input);
    if (enc_ret != JPEG_ERR_OK || out_len <= 0 || static_cast<size_t>(out_len) > encoded.size()) {
        SetError("JPEG encode failed");
        return false;
    }
    encoded.resize(static_cast<size_t>(out_len));

    if (file_service_ == nullptr) {
        SetError("File service is not available");
        return false;
    }
    if (!file_service_->IsMounted() && !file_service_->Init()) {
        SetError("SD card is not available");
        return false;
    }
    if (!file_service_->Exists(kPhotoDir) && !file_service_->CreateDirectory(kPhotoDir)) {
        SetError("Failed to create /photos on SD card");
        return false;
    }

    saved_path = BuildPhotoPath();
    if (saved_path.empty()) {
        SetError("Failed to choose a unique photo path");
        return false;
    }
    if (!file_service_->WriteFile(saved_path, encoded, false)) {
        SetError("Failed to save photo");
        return false;
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        last_saved_path_ = saved_path;
        last_error_.clear();
        xSemaphoreGive(mutex_);
    }
    ESP_LOGI(TAG, "Saved photo: %s (%u bytes)", saved_path.c_str(), static_cast<unsigned>(encoded.size()));
    return true;
}

CameraState CameraService::GetState() const {
    CameraState state;
    state.available = IsAvailable();
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state.preview_running = preview_running_;
        state.has_frame = has_frame_;
        state.width = active_width_;
        state.height = active_height_;
        state.frame_count = frame_count_;
        state.last_saved_path = last_saved_path_;
        state.last_error = last_error_;
        xSemaphoreGive(mutex_);
    }
    return state;
}

void CameraService::PreviewTaskEntry(void* arg) {
    auto* service = static_cast<CameraService*>(arg);
    if (service != nullptr) {
        service->PreviewTask();
    }
    vTaskDelete(nullptr);
}

void CameraService::PreviewTask() {
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    const size_t frame_size = static_cast<size_t>(active_stride_) * active_height_;

    while (!ShouldStopPreview()) {
        v4l2_buffer buf = {};
        buf.type = type;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd_, VIDIOC_DQBUF, &buf) != 0) {
            if (errno != EAGAIN && errno != EINTR) {
                ESP_LOGW(TAG, "Camera dequeue failed: %s", ErrnoName());
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if ((buf.flags & V4L2_BUF_FLAG_DONE) != 0 &&
            buf.index < buffers_.size() &&
            buffers_[buf.index].data != nullptr &&
            buffers_[buf.index].length >= frame_size) {
            CameraFrame frame;
            frame.width = active_width_;
            frame.height = active_height_;
            frame.stride = active_stride_;
            frame.timestamp_us = esp_timer_get_time();
            CopyRgb565Frame(buffers_[buf.index].data, buffers_[buf.index].length,
                            active_stride_, active_height_, active_pixelformat_, frame.rgb565);
            RotateRgb565Frame180(frame);

            if (mutex_ != nullptr) {
                xSemaphoreTake(mutex_, portMAX_DELAY);
                frame.sequence = latest_frame_.sequence + 1;
                latest_frame_ = std::move(frame);
                has_frame_ = true;
                frame_count_++;
                xSemaphoreGive(mutex_);
            }
        }

        if (ioctl(fd_, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGW(TAG, "Camera requeue failed: %s", ErrnoName());
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
#endif

    CloseStream();
    MarkPreviewStopped();
    ESP_LOGI(TAG, "Camera preview stopped");
}

bool CameraService::OpenStream(int width, int height) {
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    esp_err_t ret = camera_device_.Acquire();
    if (ret != ESP_OK) {
        SetError(std::string("Camera init failed: ") + esp_err_to_name(ret));
        return false;
    }

    const char* device_path = camera_device_.dev_path();
    if (device_path == nullptr) {
        SetError("Camera device handle is not available");
        return false;
    }

    fd_ = open(device_path, O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        SetError(std::string("Failed to open camera device: ") + ErrnoName());
        return false;
    }

    v4l2_capability capability = {};
    if (ioctl(fd_, VIDIOC_QUERYCAP, &capability) != 0) {
        SetError(std::string("Failed to query camera capability: ") + ErrnoName());
        return false;
    }
    if ((capability.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0 ||
        (capability.capabilities & V4L2_CAP_STREAMING) == 0) {
        SetError("Camera does not expose streaming capture");
        return false;
    }

    v4l2_format format = {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565X;
    if (ioctl(fd_, VIDIOC_S_FMT, &format) != 0) {
        format = {};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = width;
        format.fmt.pix.height = height;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
        if (ioctl(fd_, VIDIOC_S_FMT, &format) != 0) {
            SetError("Camera RGB565/RGB565X output is not available");
            return false;
        }
    }

    active_width_ = static_cast<int>(format.fmt.pix.width);
    active_height_ = static_cast<int>(format.fmt.pix.height);
    active_stride_ = static_cast<int>(format.fmt.pix.bytesperline);
    if (active_stride_ <= 0) {
        active_stride_ = active_width_ * 2;
    }
    active_pixelformat_ = format.fmt.pix.pixelformat;
    if (active_pixelformat_ != V4L2_PIX_FMT_RGB565 && active_pixelformat_ != V4L2_PIX_FMT_RGB565X) {
        SetError("Camera returned an unsupported RGB format");
        return false;
    }

    v4l2_requestbuffers req = {};
    req.count = kBufferCount;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd_, VIDIOC_REQBUFS, &req) != 0 || req.count == 0) {
        SetError(std::string("Failed to allocate camera buffers: ") + ErrnoName());
        return false;
    }

    buffers_.assign(req.count, VideoBuffer{});
    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) != 0) {
            SetError(std::string("Failed to query camera buffer: ") + ErrnoName());
            return false;
        }

        auto* data = static_cast<uint8_t*>(mmap(nullptr, buf.length,
                                                PROT_READ | PROT_WRITE,
                                                MAP_SHARED, fd_, buf.m.offset));
        if (data == MAP_FAILED) {
            SetError(std::string("Failed to map camera buffer: ") + ErrnoName());
            return false;
        }
        buffers_[i].data = data;
        buffers_[i].length = buf.length;

        if (ioctl(fd_, VIDIOC_QBUF, &buf) != 0) {
            SetError(std::string("Failed to queue camera buffer: ") + ErrnoName());
            return false;
        }
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (StreamOnSuppressingBenignGpioIsrLog(fd_, &type) != 0) {
        SetError(std::string("Failed to start camera stream: ") + ErrnoName());
        return false;
    }

    last_error_.clear();
    return true;
#else
    (void)width;
    (void)height;
    SetError("Camera support is not enabled in this build");
    return false;
#endif
}

void CameraService::CloseStream() {
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    if (fd_ >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(fd_, VIDIOC_STREAMOFF, &type);
    }
    for (auto& buffer : buffers_) {
        if (buffer.data != nullptr && buffer.data != MAP_FAILED) {
            munmap(buffer.data, buffer.length);
        }
        buffer = {};
    }
    buffers_.clear();
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    camera_device_.Release();
    active_width_ = 0;
    active_height_ = 0;
    active_stride_ = 0;
    active_pixelformat_ = 0;
#endif
}

bool CameraService::ShouldStopPreview() const {
    if (mutex_ == nullptr) {
        return true;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool stop = stop_requested_;
    xSemaphoreGive(mutex_);
    return stop;
}

void CameraService::MarkPreviewStopped() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        preview_running_ = false;
        stop_requested_ = false;
        preview_task_ = nullptr;
        xSemaphoreGive(mutex_);
    }
}

void CameraService::SetError(const std::string& error) {
    ESP_LOGW(TAG, "%s", error.c_str());
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        last_error_ = error;
        xSemaphoreGive(mutex_);
    } else {
        last_error_ = error;
    }
}

std::string CameraService::BuildPhotoPath() {
    char name[48] = {};
    const std::time_t now = std::time(nullptr);
    if (static_cast<int64_t>(now) > kMinValidUnixTime) {
        std::tm timeinfo = {};
        localtime_r(&now, &timeinfo);
        std::strftime(name, sizeof(name), "IMG_%Y%m%d_%H%M%S.jpg", &timeinfo);
    } else {
        std::snprintf(name, sizeof(name), "IMG_%" PRId64 ".jpg", esp_timer_get_time() / 1000);
    }

    std::string path = JoinPath(kPhotoDir, name);
    if (file_service_ == nullptr) {
        return path;
    }

    const char* dot = std::strrchr(name, '.');
    const std::string stem = dot != nullptr ? std::string(name, static_cast<size_t>(dot - name)) : name;
    const std::string extension = dot != nullptr ? dot : ".jpg";

    for (int suffix = 1; suffix <= kMaxPhotoNameSuffix && file_service_->Exists(path); ++suffix) {
        char numbered[80] = {};
        std::snprintf(numbered, sizeof(numbered), "%s_%04d%s",
                      stem.c_str(), suffix, extension.c_str());
        path = JoinPath(kPhotoDir, numbered);
    }
    if (!file_service_->Exists(path)) {
        return path;
    }

    for (int attempt = 0; attempt <= kMaxPhotoNameSuffix && file_service_->Exists(path); ++attempt) {
        char unique_name[96] = {};
        const int64_t timestamp_us = esp_timer_get_time();
        if (dot != nullptr) {
            std::snprintf(unique_name, sizeof(unique_name), "%s_%" PRId64 "_%04d%s",
                          stem.c_str(), timestamp_us, attempt, extension.c_str());
        } else {
            std::snprintf(unique_name, sizeof(unique_name), "%s_%" PRId64 "_%04d.jpg",
                          stem.c_str(), timestamp_us, attempt);
        }
        path = JoinPath(kPhotoDir, unique_name);
    }
    if (file_service_->Exists(path)) {
        return {};
    }
    return path;
}

}  // namespace rodakos
