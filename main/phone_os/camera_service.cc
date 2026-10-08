#include "phone_os/resource_failure_injection.h"
#include "phone_os/camera_service.h"
#include "phone_os/camera-teardown-diagnostics.h"

#include "rodakos_adapters/file_service.h"

#include "sdkconfig.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <inttypes.h>
#include <memory>
#include <new>
#include <utility>

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_jpeg_enc.h>
#include <esp_log.h>
#include <esp_log_level.h>
#include <esp_timer.h>

#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
#include <esp_video_ioctl.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace rodakos {
namespace {
constexpr const char* TAG = "CameraService";
constexpr const char* kPhotoDir = "/photos";
constexpr int kBufferCount = 2;
constexpr uint32_t kPreviewTaskStackSize = 4096;
constexpr int64_t kFirstFrameTimeoutUs = 3000000;
constexpr int kMaxConsecutiveDequeueFailures = 10;
constexpr suseconds_t kDequeueTimeoutUs = 200000;
constexpr uint8_t kJpegQuality = 82;
constexpr int64_t kMinValidUnixTime = 1700000000;
constexpr int kMaxPhotoNameSuffix = 9999;
constexpr const char* kGpioLogTag = "gpio";
// Keep this fallback inside libstdc++'s small-string storage.
constexpr const char* kAllocationError = "Camera OOM";

class SemaphoreLock {
public:
    explicit SemaphoreLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    ~SemaphoreLock() { xSemaphoreGive(mutex_); }
    SemaphoreLock(const SemaphoreLock&) = delete;
    SemaphoreLock& operator=(const SemaphoreLock&) = delete;
private:
    SemaphoreHandle_t mutex_;
};

struct HeapDeleter {
    void operator()(uint8_t* pointer) const { heap_caps_free(pointer); }
};
struct EncoderDeleter {
    void operator()(void* encoder) const { jpeg_enc_close(encoder); }
};
using HeapBuffer = std::unique_ptr<uint8_t, HeapDeleter>;
using Encoder = std::unique_ptr<void, EncoderDeleter>;


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
    task_owner_.Close();
    StopJpegStream();
    StopPreview(PreviewOwner::kRemote);
    StopPreview();
    task_owner_.Drain();
    // A JPEG callback can itself capture a photo, so drain those workers before
    // taking this lock. In-flight storage still owns the service state until it returns.
    std::lock_guard<std::mutex> capture_lock(capture_mutex_);
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool CameraService::IsAvailable() const {
    return camera_device_.IsConfigured();
}

bool CameraService::StartPreview(PreviewOwner owner, int width, int height) {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (!IsAvailable()) {
        SetError("Camera device is not configured");
        return false;
    }
    if (width <= 0 || height <= 0) {
        SetError("Invalid camera preview dimensions");
        return false;
    }
    if (mutex_ == nullptr) {
        SetError("Camera service mutex is not available");
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (task_owner_.IsClosed()) {
        xSemaphoreGive(mutex_);
        SetError("Camera service is closing");
        return false;
    }
    bool& lease = PreviewLease(owner);
    if (!preview_running_) {
        // An unexpected task exit invalidates all previously held leases.
        local_preview_lease_ = false;
        remote_preview_lease_ = false;
    }
    if (preview_running_ && stop_requested_) {
        xSemaphoreGive(mutex_);
        SetError("Camera preview is still stopping");
        return false;
    }
    if (lease && preview_running_) {
        const bool dimensions_match = active_width_ == width && active_height_ == height;
        xSemaphoreGive(mutex_);
        if (!dimensions_match) {
            SetError("Camera preview is already running at a different resolution");
            return false;
        }
        return true;
    }
    if (preview_running_) {
        const bool dimensions_match = active_width_ == width && active_height_ == height;
        if (!dimensions_match) {
            xSemaphoreGive(mutex_);
            SetError("Camera preview is already running at a different resolution");
            return false;
        }
        lease = true;
        xSemaphoreGive(mutex_);
        return true;
    }
    stop_requested_ = false;
    has_frame_ = false;
    frame_count_ = 0;
    latest_frame_ = {};
    xSemaphoreGive(mutex_);

    auto ticket = ReserveTaskRetirement(task_owner_, PreviewTaskEntry, this);
    if (!ticket) {
        SetError("Camera task retirement capacity is unavailable");
        return false;
    }
    bool opened = false;
    try {
        opened = OpenStream(width, height);
    } catch (const std::bad_alloc&) {
        SetError(kAllocationError);
    }
    if (!opened) {
        CloseStream();
        CancelTaskRetirement(ticket);
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    preview_running_ = true;
    lease = true;
    xSemaphoreGive(mutex_);

    TaskHandle_t task_handle = nullptr;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const auto previous_task = preview_task_;
    preview_task_ = ticket;
    const BaseType_t task_ret = FailResource(ResourceFailure::kCameraTask) ? pdFAIL :
        xTaskCreatePinnedToCoreWithCaps(TaskRetirementEntry, "camera_preview", kPreviewTaskStackSize,
                                        TaskRetirementContext(ticket), 3, &task_handle, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (task_ret == pdPASS && task_handle != nullptr) {
        PublishTaskRetirement(ticket, task_handle);
    } else {
        CancelTaskRetirement(ticket);
        preview_task_ = previous_task;
    }
    xSemaphoreGive(mutex_);
    if (task_handle == nullptr) {
        LogPreviewTaskCreateFailure();
        SetError("Failed to start camera preview task");
        xSemaphoreTake(mutex_, portMAX_DELAY);
        preview_running_ = false;
        lease = false;
        xSemaphoreGive(mutex_);
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

void CameraService::StopPreview(PreviewOwner owner) {
    ESP_LOGI(TAG, "StopPreview: begin");
    TaskRetirementTicket task;
    bool should_wait = false;
    {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        if (mutex_ == nullptr) return;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        bool& lease = PreviewLease(owner);
        lease = false;
        task = preview_task_;
        should_wait = bool(task) && !local_preview_lease_ && !remote_preview_lease_;
        if (should_wait && preview_running_) stop_requested_ = true;
        xSemaphoreGive(mutex_);
    }
    if (should_wait) ESP_LOGI(TAG, "StopPreview: stop requested");
    if (!should_wait || task.IsCurrentTask()) {
        ESP_LOGI(TAG, "StopPreview: no worker wait");
        return;
    }
    // Neither service lock is held: reentry and a replacement Start can
    // progress while this caller waits for its captured generation only.
    task.Join();
    ESP_LOGI(TAG, "StopPreview: worker stopped");
}

bool CameraService::GetLatestFrame(CameraFrame& frame) {
    if (mutex_ == nullptr) {
        return false;
    }
    try {
        SemaphoreLock lock(mutex_);
        if (!has_frame_) {
            last_error_ = "No camera frame is ready yet";
            return false;
        }
        // Failure cannot mutate a snapshot already owned by the caller.
        CameraFrame next = latest_frame_;
        frame = std::move(next);
        return true;
    } catch (const std::bad_alloc&) {
        SetError(kAllocationError);
        return false;
    }
}

std::string CameraService::last_error() const {
    if (mutex_ == nullptr) {
        return last_error_;
    }
    try {
        SemaphoreLock lock(mutex_);
        return last_error_;
    } catch (const std::bad_alloc&) {
        return kAllocationError;
    }
}

bool CameraService::CaptureJpeg(std::vector<uint8_t>& jpeg) {
    jpeg.clear();
    try {
        CameraFrame frame;
        if (!GetLatestFrame(frame)) {
            return false;
        }

        auto packed = PackRgb565Frame(frame);
        if (packed.empty()) {
            SetError("Camera frame is incomplete");
            return false;
        }

        const size_t rgb888_size = static_cast<size_t>(frame.width) * frame.height * 3;
        HeapBuffer aligned_input(AllocAlignedJpegInput(rgb888_size));
        if (!aligned_input) {
            SetError("Not enough memory for JPEG input");
            return false;
        }
        if (!CopyRgb565LeToRgb888(packed, frame.width, frame.height, aligned_input.get(), rgb888_size)) {
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
        const auto open_result = jpeg_enc_open(&jpeg_cfg, &encoder);
        Encoder owned_encoder(encoder);
        if (open_result != JPEG_ERR_OK || encoder == nullptr) {
            SetError("Failed to open JPEG encoder");
            return false;
        }

        const size_t out_capacity = std::max<size_t>(64 * 1024, rgb888_size);
        std::vector<uint8_t> encoded(out_capacity);
        int out_len = 0;
        const jpeg_error_t enc_ret = jpeg_enc_process(encoder, aligned_input.get(),
                                                      static_cast<int>(rgb888_size),
                                                      encoded.data(),
                                                      static_cast<int>(encoded.size()),
                                                      &out_len);
        owned_encoder.reset();
        aligned_input.reset();
        if (enc_ret != JPEG_ERR_OK || out_len <= 0 || static_cast<size_t>(out_len) > encoded.size()) {
            SetError("JPEG encode failed");
            return false;
        }
        encoded.resize(static_cast<size_t>(out_len));
        jpeg = std::move(encoded);
        return true;
    } catch (const std::bad_alloc&) {
        SetError(kAllocationError);
        return false;
    }
}

bool CameraService::StartJpegStream(uint8_t fps, JpegFrameCallback callback) {
    if (fps == 0 || fps > 30 || !callback) {
        SetError("Invalid JPEG stream configuration");
        return false;
    }

    if (mutex_ == nullptr) {
        SetError("Camera service mutex is not available");
        return false;
    }

    JpegFrameCallback callback_to_release;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (jpeg_stream_running_) {
        xSemaphoreGive(mutex_);
        SetError("JPEG stream is already running");
        return false;
    }
    if (!preview_running_) {
        xSemaphoreGive(mutex_);
        SetError("Camera preview must be running before JPEG stream");
        return false;
    }
    auto ticket = ReserveTaskRetirement(task_owner_, JpegStreamTaskEntry, this);
    if (!ticket) {
        xSemaphoreGive(mutex_);
        SetError("Camera task retirement capacity is unavailable");
        return false;
    }
    jpeg_stream_fps_ = fps;
    jpeg_stream_callback_ = std::move(callback);
    jpeg_stream_stop_requested_ = false;
    jpeg_stream_running_ = true;
    const auto previous_task = jpeg_stream_task_;
    jpeg_stream_task_ = ticket;

    TaskHandle_t created_task = nullptr;
    if (xTaskCreateWithCaps(TaskRetirementEntry, "camera_jpeg", 8192,
                            TaskRetirementContext(ticket), 3,
                            &created_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        jpeg_stream_running_ = false;
        callback_to_release = std::move(jpeg_stream_callback_);
        CancelTaskRetirement(ticket);
        jpeg_stream_task_ = previous_task;
        xSemaphoreGive(mutex_);
        SetError("Failed to start JPEG stream task");
        return false;
    }
    PublishTaskRetirement(ticket, created_task);
    xSemaphoreGive(mutex_);
    return true;
}

void CameraService::StopJpegStream() {
    if (mutex_ == nullptr) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const TaskRetirementTicket task = jpeg_stream_task_;
    if (jpeg_stream_running_) jpeg_stream_stop_requested_ = true;
    xSemaphoreGive(mutex_);
    task.Join();
}

bool CameraService::CapturePhoto(std::string& saved_path) {
    std::lock_guard<std::mutex> capture_lock(capture_mutex_);
    saved_path.clear();
    try {
        std::vector<uint8_t> encoded;
        if (!CaptureJpeg(encoded)) {
            return false;
        }

        if (file_service_ == nullptr) {
            SetError("File service is not available");
            return false;
        }
        std::string committed_path;
        std::string saved_history;
        const bool saved = file_service_->WithIoLock([&]() {
            if (!file_service_->IsMounted() && !file_service_->Init()) {
                SetError("SD card is not available");
                return false;
            }
            if (!file_service_->Exists(kPhotoDir) && !file_service_->CreateDirectory(kPhotoDir)) {
                SetError("Failed to create /photos on SD card");
                return false;
            }

            const std::string candidate = BuildPhotoPath();
            if (candidate.empty()) {
                SetError("Failed to choose a unique photo path");
                return false;
            }
            // Allocate both publication strings before committing a new file.
            // Successful storage must not be followed by a fallible result copy.
            committed_path = candidate;
            saved_history = candidate;
            // The I/O lock serializes service writers; exclusive creation also
            // protects existing photos from writers outside this service instance.
            if (!file_service_->WriteNewFile(candidate, encoded)) {
                SetError("Failed to save photo");
                return false;
            }
            return true;
        });
        if (!saved) {
            return false;
        }

        saved_path = std::move(committed_path);
        if (mutex_ != nullptr) {
            SemaphoreLock lock(mutex_);
            last_saved_path_ = std::move(saved_history);
            last_error_.clear();
        }
        ESP_LOGI(TAG, "Saved photo: %s (%u bytes)", saved_path.c_str(), static_cast<unsigned>(encoded.size()));
        return true;
    } catch (const std::bad_alloc&) {
        SetError(kAllocationError);
        return false;
    }
}

CameraState CameraService::GetState() const {
    CameraState state;
    state.available = IsAvailable();
    if (mutex_ != nullptr) {
        SemaphoreLock lock(mutex_);
        state.preview_running = preview_running_;
        state.has_frame = has_frame_;
        state.width = active_width_;
        state.height = active_height_;
        state.frame_count = frame_count_;
        try {
            state.last_saved_path = last_saved_path_;
            state.last_error = last_error_;
        } catch (const std::bad_alloc&) {
            state.last_saved_path.clear();
            state.last_error = kAllocationError;
        }
    }
    return state;
}

void CameraService::PreviewTaskEntry(void* arg) {
    auto* service = static_cast<CameraService*>(arg);
    if (service != nullptr) {
        try {
            service->PreviewTask();
        } catch (const std::bad_alloc&) {
            service->CloseStream();
            service->SetError(kAllocationError);
            service->MarkPreviewStopped();
        }
    }
}

void CameraService::JpegStreamTaskEntry(void* arg) {
    auto* service = static_cast<CameraService*>(arg);
    if (service != nullptr) {
        service->JpegStreamTask();
    }
}

void CameraService::JpegStreamTask() {
    uint32_t last_sequence = 0;
    while (true) {
        uint8_t fps = 0;
        try {
            JpegFrameCallback callback;
            bool should_stop = false;
            if (mutex_ != nullptr) {
                SemaphoreLock lock(mutex_);
                should_stop = jpeg_stream_stop_requested_ || !jpeg_stream_running_;
                fps = jpeg_stream_fps_;
                // Stop must remain observable even when every callback copy fails.
                if (!should_stop && fps != 0) callback = jpeg_stream_callback_;
            }
            if (should_stop || fps == 0 || !callback) {
                break;
            }

            CameraFrame latest;
            if (GetLatestFrame(latest) && latest.sequence != last_sequence) {
                std::vector<uint8_t> jpeg;
                if (CaptureJpeg(jpeg)) {
                    last_sequence = latest.sequence;
                    callback(std::move(jpeg), latest.sequence, latest.timestamp_us);
                }
            }
        } catch (const std::bad_alloc&) {
            // A callback may already own this sequence; never replay it.
            SetError(kAllocationError);
        }
        vTaskDelay(pdMS_TO_TICKS(1000 / static_cast<int>(std::max<uint8_t>(1, fps))));
    }

    JpegFrameCallback callback_to_release;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        jpeg_stream_running_ = false;
        jpeg_stream_stop_requested_ = false;
        jpeg_stream_fps_ = 0;
        callback_to_release = std::move(jpeg_stream_callback_);
        xSemaphoreGive(mutex_);
    }
    // Releasing captures can reenter Start/Stop. Logical completion precedes
    // destruction, but retirement publishes finished only after this returns.
}

void CameraService::PreviewTask() {
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    const size_t frame_size = static_cast<size_t>(active_stride_) * active_height_;
    const int64_t started_at_us = esp_timer_get_time();
    int64_t last_frame_at_us = 0;
    int consecutive_dequeue_failures = 0;
    bool received_frame = false;

    while (!ShouldStopPreview()) {
        v4l2_buffer buf = {};
        buf.type = type;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd_, VIDIOC_DQBUF, &buf) != 0) {
            if (ShouldStopPreview()) {
                break;
            }
            ++consecutive_dequeue_failures;
            const bool first_frame_timed_out =
                !received_frame && esp_timer_get_time() - started_at_us >= kFirstFrameTimeoutUs;
            if (first_frame_timed_out) {
                SetError("Camera preview timed out waiting for the first frame");
                break;
            }
            if (received_frame &&
                consecutive_dequeue_failures >= kMaxConsecutiveDequeueFailures) {
                SetError(std::string("Camera preview dequeue failed: ") + ErrnoName());
                break;
            }
            continue;
        }
        consecutive_dequeue_failures = 0;

        if ((buf.flags & V4L2_BUF_FLAG_DONE) != 0 &&
            buf.index < buffers_.size() &&
            buffers_[buf.index].data != nullptr &&
            buffers_[buf.index].length >= frame_size &&
            buf.bytesused >= frame_size) {
            CameraFrame frame;
            frame.width = active_width_;
            frame.height = active_height_;
            frame.stride = active_stride_;
            frame.timestamp_us = esp_timer_get_time();
            try {
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
                    if (!received_frame) {
                        ESP_LOGI(TAG, "Camera first frame ready: %dx%d stride=%d elapsed_ms=%" PRId64,
                                 active_width_, active_height_, active_stride_,
                                 (esp_timer_get_time() - started_at_us) / 1000);
                    }
                    received_frame = true;
                    last_frame_at_us = esp_timer_get_time();
                }
            } catch (const std::bad_alloc&) {
                // Requeue the driver buffer and retain published frames/owners.
                // A later frame can recover without reopening the device.
                SetError(kAllocationError);
            }
        }

        if (ioctl(fd_, VIDIOC_QBUF, &buf) != 0) {
            SetError(std::string("Camera requeue failed: ") + ErrnoName());
            break;
        }

        if (!received_frame &&
            esp_timer_get_time() - started_at_us >= kFirstFrameTimeoutUs) {
            SetError("Camera preview timed out waiting for the first frame");
            break;
        }
    }
#endif

    ESP_LOGI(TAG, "Preview exit: CloseStream begin");
    CloseStream();
    ESP_LOGI(TAG, "Preview exit: CloseStream complete");
    const auto state = GetState();
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    const int64_t stopped_at_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Camera preview stopped: frames=%" PRIu32 " elapsed_ms=%" PRId64
             " last_frame_age_ms=%" PRId64,
             state.frame_count, (stopped_at_us - started_at_us) / 1000,
             last_frame_at_us == 0 ? -1 : (stopped_at_us - last_frame_at_us) / 1000);
#else
    ESP_LOGI(TAG, "Camera preview stopped: frames=%" PRIu32, state.frame_count);
#endif
    // No service access follows logical completion. Retirement still waits for
    // this function's local state and its caller's complete return.
    MarkPreviewStopped();
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
    ESP_LOGI(TAG,
             "Starting camera stream: dma_buffer_limit=%u internal_dma_free=%u "
             "internal_dma_largest=%u",
             static_cast<unsigned>(CONFIG_CAM_CTRL_DVP_DMA_BUFFER_SIZE),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)));
    if (StreamOnSuppressingBenignGpioIsrLog(fd_, &type) != 0) {
        SetError(std::string("Failed to start camera stream: ") + ErrnoName());
        return false;
    }

    timeval dequeue_timeout = {};
    dequeue_timeout.tv_usec = kDequeueTimeoutUs;
    if (ioctl(fd_, VIDIOC_S_DQBUF_TIMEOUT, &dequeue_timeout) != 0) {
        SetError(std::string("Failed to configure camera dequeue timeout: ") + ErrnoName());
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

bool& CameraService::PreviewLease(PreviewOwner owner) {
    return owner == PreviewOwner::kRemote ? remote_preview_lease_ : local_preview_lease_;
}

void CameraService::CloseStream() {
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
    if (fd_ >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ESP_LOGI(TAG, "CloseStream: STREAMOFF begin");
        rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_IOCTL_ENTER, xPortGetCoreID(), 0);
        const int streamoff_result = ioctl(fd_, VIDIOC_STREAMOFF, &type);
        rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_IOCTL_RETURNED, xPortGetCoreID(),
                                     streamoff_result);
        rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_BEFORE_LOG, xPortGetCoreID(),
                                     streamoff_result);
        ESP_LOGI(TAG, "CloseStream: STREAMOFF complete");
        rodak_camera_teardown_record(RODAK_CAMERA_TEARDOWN_AFTER_LOG, xPortGetCoreID(),
                                     streamoff_result);
    }
    for (auto& buffer : buffers_) {
        if (buffer.data != nullptr && buffer.data != MAP_FAILED) {
            munmap(buffer.data, buffer.length);
        }
        buffer = {};
    }
    buffers_.clear();
    if (fd_ >= 0) {
        ESP_LOGI(TAG, "CloseStream: fd close begin");
        close(fd_);
        fd_ = -1;
        ESP_LOGI(TAG, "CloseStream: fd close complete");
    }
    ESP_LOGI(TAG, "CloseStream: device release begin");
    const esp_err_t release_ret = camera_device_.Release();
    if (release_ret == ESP_OK) {
        ESP_LOGI(TAG, "CloseStream: device release complete");
    } else {
        ESP_LOGW(TAG, "CloseStream: device release deferred for retry: %s",
                 esp_err_to_name(release_ret));
    }
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
        local_preview_lease_ = false;
        remote_preview_lease_ = false;
        stop_requested_ = false;
        has_frame_ = false;
        latest_frame_ = {};
        xSemaphoreGive(mutex_);
    }
}

void CameraService::SetError(const std::string& error) {
    ESP_LOGW(TAG, "%s", error.c_str());
    if (mutex_ != nullptr) {
        SemaphoreLock lock(mutex_);
        try {
            last_error_ = error;
        } catch (const std::bad_alloc&) {
            last_error_ = kAllocationError;
        }
    } else {
        try {
            last_error_ = error;
        } catch (const std::bad_alloc&) {
            last_error_ = kAllocationError;
        }
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
