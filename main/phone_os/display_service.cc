#include "phone_os/display_service.h"

#include <algorithm>
#include <array>
#include <inttypes.h>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

#include <esp_heap_caps.h>
#include <esp_jpeg_enc.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_lvgl_port.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "DisplayService";
constexpr uint8_t kJpegQuality = 65;
constexpr size_t kJpegOutputCapacity = 100 * 1024;
constexpr uint32_t kTaskStackSize = 8192;
constexpr int64_t kJpegStatsIntervalUs = 5 * 1000 * 1000;
constexpr std::array<const char*, 4> kEncoderHeapStages = {
    "before_open", "after_open", "after_process", "after_close"};

struct EncoderHeapSnapshot {
    int64_t at_us = -1;
    size_t dma_free = 0;
    size_t dma_largest = 0;
    size_t psram_free = 0;
    size_t psram_largest = 0;
};

EncoderHeapSnapshot ReadEncoderHeap() {
    return {esp_timer_get_time(),
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
            heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)};
}

struct EncoderHeapWindow {
    EncoderHeapSnapshot minimum;
    uint32_t samples = 0;
    uint32_t lowest_dma_sequence = 0;

    void Observe(const EncoderHeapSnapshot& sample, uint32_t sequence) {
        if (sample.at_us < 0) return;
        if (samples++ == 0) {
            minimum = sample;
            lowest_dma_sequence = sequence;
            return;
        }
        if (sample.dma_largest < minimum.dma_largest) {
            minimum.at_us = sample.at_us;
            lowest_dma_sequence = sequence;
        }
        minimum.dma_free = std::min(minimum.dma_free, sample.dma_free);
        minimum.dma_largest = std::min(minimum.dma_largest, sample.dma_largest);
        minimum.psram_free = std::min(minimum.psram_free, sample.psram_free);
        minimum.psram_largest = std::min(minimum.psram_largest, sample.psram_largest);
    }
};

void LogEncoderHeapFirst(const std::array<EncoderHeapSnapshot, 4>& heap, uint32_t sequence) {
    for (size_t stage = 0; stage < heap.size(); ++stage) {
        const auto& sample = heap[stage];
        if (sample.at_us < 0) continue;
        ESP_LOGI(TAG, "JPEG heap first: seq=%" PRIu32 " stage=%s at_us=%" PRId64
                 " dma_free=%u dma_largest=%u psram_free=%u psram_largest=%u",
                 sequence, kEncoderHeapStages[stage], sample.at_us,
                 static_cast<unsigned>(sample.dma_free), static_cast<unsigned>(sample.dma_largest),
                 static_cast<unsigned>(sample.psram_free), static_cast<unsigned>(sample.psram_largest));
    }
}

void LogEncoderHeapWindow(const std::array<EncoderHeapWindow, 4>& heap, const char* window) {
    for (size_t stage = 0; stage < heap.size(); ++stage) {
        const auto& sample = heap[stage];
        if (sample.samples == 0) continue;
        // 各项是独立最低值；时间和序号只关联 dma_largest，不能视作同一时刻快照。
        ESP_LOGI(TAG, "JPEG heap min: window=%s stage=%s samples=%" PRIu32
                 " dma_free=%u dma_largest=%u psram_free=%u psram_largest=%u"
                 " dma_largest_at_us=%" PRId64 " dma_largest_seq=%" PRIu32,
                 window, kEncoderHeapStages[stage], sample.samples,
                 static_cast<unsigned>(sample.minimum.dma_free),
                 static_cast<unsigned>(sample.minimum.dma_largest),
                 static_cast<unsigned>(sample.minimum.psram_free),
                 static_cast<unsigned>(sample.minimum.psram_largest),
                 sample.minimum.at_us, sample.lowest_dma_sequence);
    }
}

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
    void operator()(uint8_t* data) const { heap_caps_free(data); }
};
using HeapBuffer = std::unique_ptr<uint8_t, HeapDeleter>;

struct EncoderDeleter {
    void operator()(void* encoder) const { jpeg_enc_close(encoder); }
};
using Encoder = std::unique_ptr<void, EncoderDeleter>;

uint8_t* AllocAligned(size_t size) {
    auto* buffer = static_cast<uint8_t*>(
        heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        buffer = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, size, MALLOC_CAP_8BIT));
    }
    return buffer;
}

void RefreshDisplayForCapture(void* user_data) {
    auto* display = static_cast<lv_display_t*>(user_data);
    if (display == nullptr) return;
    lv_obj_invalidate(lv_display_get_screen_active(display));
    lv_obj_invalidate(lv_display_get_layer_bottom(display));
    lv_obj_invalidate(lv_display_get_layer_top(display));
    lv_obj_invalidate(lv_display_get_layer_sys(display));
    // 在 LVGL 任务中刷新，确保会话从完整画面开始。
    lv_refr_now(display);
}

}  // namespace

struct DisplayService::EncodeMetrics {
    int64_t conversion_us = 0;
    int64_t open_us = 0;
    int64_t process_us = 0;
    int64_t close_us = 0;
    size_t output_size = 0;
    std::array<EncoderHeapSnapshot, 4> heap;
};

namespace {
bool HasRgb565Frame(const DisplayFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0) return false;
    if (static_cast<size_t>(frame.width) > SIZE_MAX / static_cast<size_t>(frame.height)) {
        return false;
    }
    const size_t pixel_count = static_cast<size_t>(frame.width) * frame.height;
    return pixel_count <= SIZE_MAX / 2 && frame.rgb565.size() >= pixel_count * 2;
}
}  // namespace

bool DisplayService::EncodeJpeg(const uint8_t* input, size_t input_size, int width, int height,
                                std::vector<uint8_t>& jpeg, EncodeMetrics* metrics) {
    if (metrics != nullptr) metrics->heap[0] = ReadEncoderHeap();
    const int64_t open_started_us = esp_timer_get_time();
    jpeg_enc_config_t config = DEFAULT_JPEG_ENC_CONFIG();
    config.width = width;
    config.height = height;
    config.src_type = JPEG_PIXEL_FORMAT_RGB888;
    config.subsampling = JPEG_SUBSAMPLE_420;
    config.quality = kJpegQuality;
    // BigSmart 的常驻语音任务占用较多内部 RAM；双任务编码器会额外
    // 创建内部任务并在真实设备上失败，因此保持单任务路径确保持续出帧。
    config.task_enable = false;
    jpeg_enc_handle_t encoder = nullptr;
    const auto open_ret = jpeg_enc_open(&config, &encoder);
    if (metrics != nullptr) metrics->open_us = esp_timer_get_time() - open_started_us;
    if (metrics != nullptr) metrics->heap[1] = ReadEncoderHeap();
    if (open_ret != JPEG_ERR_OK || encoder == nullptr) return false;
    Encoder owned_encoder(encoder);
    const auto close_encoder = [&]() {
        const int64_t close_started_us = esp_timer_get_time();
        owned_encoder.reset();
        if (metrics != nullptr) {
            metrics->close_us = esp_timer_get_time() - close_started_us;
            metrics->heap[3] = ReadEncoderHeap();
        }
    };
    // esp_new_jpeg 的 320x240 RGB888 示例使用 100 KiB 输出缓冲。屏幕帧实测约
    // 6-13 KiB，固定上限避免在 PNG 常驻时再申请一份 225 KiB 原始帧大小缓冲。
    HeapBuffer encoded(AllocAligned(kJpegOutputCapacity));
    if (!encoded) {
        close_encoder();
        return false;
    }
    int output_size = 0;
    const int64_t process_started_us = esp_timer_get_time();
    const auto ret = jpeg_enc_process(encoder, input, static_cast<int>(input_size),
                                      encoded.get(), static_cast<int>(kJpegOutputCapacity), &output_size);
    if (metrics != nullptr) metrics->process_us = esp_timer_get_time() - process_started_us;
    if (metrics != nullptr) metrics->heap[2] = ReadEncoderHeap();
    close_encoder();
    if (ret != JPEG_ERR_OK || output_size <= 0 ||
        static_cast<size_t>(output_size) > kJpegOutputCapacity) {
        return false;
    }
    // 传输队列只保留实际 JPEG 字节，不持有 100 KiB 编码缓冲。
    std::vector<uint8_t> compact(encoded.get(), encoded.get() + output_size);
    if (metrics != nullptr) metrics->output_size = compact.size();
    jpeg = std::move(compact);
    return true;
}

bool DisplayService::EncodeLatestJpeg(uint32_t previous_sequence, std::vector<uint8_t>& jpeg,
                                      uint32_t& sequence, int64_t& timestamp_us,
                                      EncodeMetrics* metrics, const char*& error) {
    jpeg.clear();
    sequence = 0;
    timestamp_us = 0;
    error = nullptr;

    int width = 0;
    int height = 0;
    {
        SemaphoreLock lock(mutex_);
        if (!capture_running_ || !has_frame_ || latest_frame_.sequence == previous_sequence ||
            !HasRgb565Frame(latest_frame_)) {
            error = "No new display frame is ready yet";
            return false;
        }
        width = latest_frame_.width;
        height = latest_frame_.height;
    }

    const size_t pixel_count = static_cast<size_t>(width) * height;
    if (pixel_count > SIZE_MAX / 3) {
        error = "Display frame dimensions are too large";
        return false;
    }
    const size_t rgb888_size = pixel_count * 3;
    HeapBuffer aligned_rgb888(AllocAligned(rgb888_size));
    if (!aligned_rgb888) {
        error = "Not enough memory for display JPEG input";
        return false;
    }

    const size_t rgb565_size = pixel_count * 2;
    {
        SemaphoreLock lock(mutex_);
        if (!capture_running_ || !has_frame_ || latest_frame_.sequence == previous_sequence ||
            latest_frame_.width != width || latest_frame_.height != height ||
            !HasRgb565Frame(latest_frame_)) {
            error = "No new display frame is ready yet";
            return false;
        }
        std::memcpy(aligned_rgb888.get(), latest_frame_.rgb565.data(), rgb565_size);
        sequence = latest_frame_.sequence;
        timestamp_us = latest_frame_.timestamp_us;
    }

    // RGB565 快照先占用目标缓冲的前 2 B/px，再从尾向头原地扩展为 RGB888。
    // 这样锁内只有一次 memcpy，避免 12-34 ms 全帧转换阻塞 LVGL flush。
    const int64_t conversion_started_us = esp_timer_get_time();
    for (size_t i = pixel_count; i-- > 0;) {
        const uint16_t pixel = static_cast<uint16_t>(aligned_rgb888.get()[i * 2]) |
                               (static_cast<uint16_t>(aligned_rgb888.get()[i * 2 + 1]) << 8);
        const uint8_t r5 = static_cast<uint8_t>((pixel >> 11) & 0x1f);
        const uint8_t g6 = static_cast<uint8_t>((pixel >> 5) & 0x3f);
        const uint8_t b5 = static_cast<uint8_t>(pixel & 0x1f);
        aligned_rgb888.get()[i * 3] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        aligned_rgb888.get()[i * 3 + 1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
        aligned_rgb888.get()[i * 3 + 2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    }
    if (metrics != nullptr) {
        metrics->conversion_us = esp_timer_get_time() - conversion_started_us;
    }

    try {
        if (!EncodeJpeg(aligned_rgb888.get(), rgb888_size, width, height, jpeg, metrics)) {
            error = "Display JPEG encode failed";
            return false;
        }
        return true;
    } catch (const std::bad_alloc&) {
        error = "Not enough memory for display JPEG output";
        return false;
    }
}

DisplayService::DisplayService(lv_display_t* display) : display_(display) {
    mutex_ = xSemaphoreCreateMutex();
    frame_ready_semaphore_ = xSemaphoreCreateBinary();
}

DisplayService::~DisplayService() {
    StopJpegStream();
    StopCapture();
    {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        if (display_ != nullptr && lvgl_port_lock(0)) {
            lv_async_call_cancel(&RefreshDisplayForCapture, display_);
            DetachWithLvglLock();
            lvgl_port_unlock();
        }
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    if (frame_ready_semaphore_ != nullptr) {
        vSemaphoreDelete(frame_ready_semaphore_);
        frame_ready_semaphore_ = nullptr;
    }
}

bool DisplayService::Attach() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (display_ == nullptr) return false;
    if (event_attached_) return true;
    if (!lvgl_port_lock(1000)) return false;
    AttachWithLvglLock();
    lvgl_port_unlock();
    return true;
}

void DisplayService::AttachWithLvglLock() {
    if (display_ == nullptr || event_attached_) return;
    lv_display_add_event_cb(display_, &DisplayService::OnDisplayEvent, LV_EVENT_ALL, this);
    event_attached_ = true;
}

void DisplayService::DetachWithLvglLock() {
    if (!event_attached_ || display_ == nullptr) return;
    lv_display_remove_event_cb_with_user_data(display_, &DisplayService::OnDisplayEvent, this);
    event_attached_ = false;
}

bool DisplayService::StartCapture(int width, int height) {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (display_ == nullptr || width <= 0 || height <= 0) {
        SetError("Unsupported display capture dimensions");
        return false;
    }
    if (!lvgl_port_lock(1000)) {
        SetError("Failed to lock display for capture");
        return false;
    }
    const bool dimensions_supported =
        lv_display_get_horizontal_resolution(display_) == width &&
        lv_display_get_vertical_resolution(display_) == height;
    if (dimensions_supported) AttachWithLvglLock();
    lvgl_port_unlock();
    if (!dimensions_supported) {
        SetError("Unsupported display capture dimensions");
        return false;
    }
    if (mutex_ == nullptr) return false;
    try {
        SemaphoreLock lock(mutex_);
        if (capture_running_) return true;
        const size_t frame_size = static_cast<size_t>(width) * height * 2;
        // 两份缓冲都可用后再发布，分配失败不能留下半初始化的镜像。
        std::vector<uint8_t> mirror(frame_size, 0);
        DisplayFrame latest;
        latest.rgb565.resize(frame_size, 0);
        mirror_rgb565_.swap(mirror);
        latest_frame_ = std::move(latest);
        if (frame_ready_semaphore_ != nullptr) {
            while (xSemaphoreTake(frame_ready_semaphore_, 0) == pdTRUE) {
            }
        }
        has_frame_ = false;
        frame_pending_ = false;
        frame_count_ = 0;
        width_ = width;
        height_ = height;
        capture_running_ = true;
        last_error_ = "";
    } catch (const std::bad_alloc&) {
        SetError("Not enough memory for display capture");
        return false;
    }
    // MQTT 只安排刷新，实际绘制在 LVGL 任务完成。
    bool refresh_queued = false;
    if (lvgl_port_lock(1000)) {
        refresh_queued = lv_async_call(&RefreshDisplayForCapture, display_) == LV_RESULT_OK;
        lvgl_port_unlock();
    }
    if (!refresh_queued) {
        {
            SemaphoreLock lock(mutex_);
            capture_running_ = false;
            std::vector<uint8_t>().swap(mirror_rgb565_);
            latest_frame_ = {};
        }
        SetError("Failed to schedule the initial display refresh");
        return false;
    }
    lvgl_port_task_wake(LVGL_PORT_EVENT_DISPLAY, nullptr);
    ESP_LOGI(TAG, "Display capture started: %dx%d", width, height);
    return true;
}

void DisplayService::StopCapture() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (mutex_ == nullptr) return;
    {
        SemaphoreLock lock(mutex_);
        capture_running_ = false;
        std::vector<uint8_t>().swap(mirror_rgb565_);
        latest_frame_ = {};
        has_frame_ = false;
        frame_pending_ = false;
    }
    if (frame_ready_semaphore_ != nullptr) xSemaphoreGive(frame_ready_semaphore_);
}

bool DisplayService::IsRunning() const {
    if (mutex_ == nullptr) return false;
    SemaphoreLock lock(mutex_);
    return capture_running_;
}

bool DisplayService::GetLatestFrame(DisplayFrame& frame) {
    frame = {};
    if (mutex_ == nullptr) return false;
    try {
        SemaphoreLock lock(mutex_);
        if (!capture_running_ || !has_frame_) {
            last_error_ = "No display frame is ready yet";
            return false;
        }
        DisplayFrame next = latest_frame_;
        frame = std::move(next);
        return true;
    } catch (const std::bad_alloc&) {
        SetError("Not enough memory for display frame");
        return false;
    }
}

bool DisplayService::CaptureJpeg(std::vector<uint8_t>& jpeg) {
    uint32_t sequence = 0;
    int64_t timestamp_us = 0;
    const char* error = nullptr;
    const bool encoded = EncodeLatestJpeg(0, jpeg, sequence, timestamp_us, nullptr, error);
    if (!encoded && error != nullptr) SetError(error);
    return encoded;
}

bool DisplayService::StartJpegStream(uint8_t fps, JpegFrameCallback callback) {
    if (fps == 0 || fps > 5 || !callback || mutex_ == nullptr) {
        SetError("Invalid display JPEG stream configuration");
        return false;
    }
    bool started = false;
    std::shared_ptr<JpegFrameCallback> callback_to_release;
    {
        SemaphoreLock lock(mutex_);
        if (!capture_running_ || jpeg_stream_running_) {
            last_error_ = !capture_running_ ? "Display capture is not running"
                                            : "Display JPEG stream is already running";
            return false;
        }
        // worker 只复制共享所有权；逐帧复制 std::function 也可能在持锁时抛 bad_alloc。
        try {
            jpeg_stream_callback_ = std::make_shared<JpegFrameCallback>(std::move(callback));
        } catch (const std::bad_alloc&) {
            last_error_ = "Not enough memory for display JPEG callback";
            return false;
        }
        jpeg_stream_fps_ = fps;
        jpeg_stream_stop_requested_ = false;
        jpeg_stream_running_ = true;
        jpeg_stream_task_ready_ = false;
        TaskHandle_t task = nullptr;
        if (xTaskCreateWithCaps(JpegStreamTaskEntry, "display_jpeg", kTaskStackSize, this, 3,
                                &task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            jpeg_stream_running_ = false;
            jpeg_stream_fps_ = 0;
            callback_to_release = std::move(jpeg_stream_callback_);
            last_error_ = "Failed to start display JPEG stream task";
        } else {
            jpeg_stream_task_ = task;
            ++jpeg_stream_generation_;
            jpeg_stream_task_ready_ = true;
            started = true;
        }
    }
    return started;
}

void DisplayService::StopJpegStream() {
    if (mutex_ == nullptr) return;
    TaskHandle_t task = nullptr;
    uint64_t generation = 0;
    {
        SemaphoreLock lock(mutex_);
        task = jpeg_stream_task_;
        generation = jpeg_stream_generation_;
        jpeg_stream_stop_requested_ = true;
    }
    if (frame_ready_semaphore_ != nullptr) xSemaphoreGive(frame_ready_semaphore_);
    if (task == nullptr || task == xTaskGetCurrentTaskHandle()) return;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20));
        SemaphoreLock lock(mutex_);
        if (jpeg_stream_completed_generation_ >= generation) break;
    }
}

void DisplayService::OnDisplayEvent(lv_event_t* event) {
    auto* service = static_cast<DisplayService*>(lv_event_get_user_data(event));
    if (service != nullptr) service->HandleDisplayEvent(event);
}

void DisplayService::HandleDisplayEvent(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if ((code != LV_EVENT_FLUSH_START && code != LV_EVENT_FLUSH_FINISH) || mutex_ == nullptr) return;
    SemaphoreLock lock(mutex_);
    const bool active = capture_running_ && width_ > 0 && height_ > 0 && !mirror_rgb565_.empty();
    if (!active) return;
    auto* display = static_cast<lv_display_t*>(lv_event_get_current_target(event));
    if (code == LV_EVENT_FLUSH_FINISH) {
        if (frame_pending_ && lv_display_flush_is_last(display)) {
            // partial flush 只更新脏区域，交换不同代缓冲会让未重绘部分
            // 回退到旧画面或黑屏。发布完整副本，保持持久镜像始终最新。
            std::memcpy(latest_frame_.rgb565.data(), mirror_rgb565_.data(),
                        mirror_rgb565_.size());
            latest_frame_.width = width_;
            latest_frame_.height = height_;
            latest_frame_.stride = width_ * 2;
            latest_frame_.timestamp_us = esp_timer_get_time();
            latest_frame_.sequence = ++frame_count_;
            has_frame_ = true;
            frame_pending_ = false;
            if (frame_ready_semaphore_ != nullptr) xSemaphoreGive(frame_ready_semaphore_);
        }
        return;
    }
    auto* area = static_cast<lv_area_t*>(lv_event_get_param(event));
    auto* draw_buf = lv_display_get_buf_active(display);
    if (area == nullptr || draw_buf == nullptr || draw_buf->data == nullptr) return;
    const int area_width = lv_area_get_width(area);
    const int area_height = lv_area_get_height(area);
    if (area->x1 < 0 || area->y1 < 0 || area->x2 >= width_ || area->y2 >= height_ ||
        area_width <= 0 || area_height <= 0 ||
        draw_buf->header.stride < static_cast<unsigned>(area_width) * 2U) return;
    for (int y = 0; y < area_height; ++y) {
        const auto* source = draw_buf->data + static_cast<size_t>(y) * draw_buf->header.stride;
        auto* target = mirror_rgb565_.data() +
                       (static_cast<size_t>(area->y1 + y) * width_ + area->x1) * 2;
        std::memcpy(target, source, static_cast<size_t>(area_width) * 2);
    }
    frame_pending_ = true;
}

void DisplayService::JpegStreamTaskEntry(void* arg) {
    auto* service = static_cast<DisplayService*>(arg);
    if (service != nullptr) service->JpegStreamTask();
    vTaskDeleteWithCaps(nullptr);
}

void DisplayService::JpegStreamTask() {
    uint64_t stream_generation = 0;
    while (true) {
        {
            SemaphoreLock lock(mutex_);
            if (jpeg_stream_task_ready_) {
                stream_generation = jpeg_stream_generation_;
                break;
            }
        }
        vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(1)));
    }
    uint32_t last_sequence = 0;
    int64_t last_attempt_at_us = -1;
    int64_t stats_started_us = esp_timer_get_time();
    uint32_t stats_attempts = 0;
    uint32_t stats_encoded = 0;
    uint32_t stats_failed = 0;
    int64_t stats_conversion_us = 0;
    int64_t stats_open_us = 0;
    int64_t stats_process_us = 0;
    int64_t stats_close_us = 0;
    size_t stats_output_bytes = 0;
    bool heap_first_logged = false;
    std::array<EncoderHeapWindow, 4> heap_window;
    while (true) {
        uint8_t fps = 0;
        std::shared_ptr<JpegFrameCallback> callback;
        {
            SemaphoreLock lock(mutex_);
            if (jpeg_stream_stop_requested_ || !jpeg_stream_running_) break;
            fps = jpeg_stream_fps_;
            callback = jpeg_stream_callback_;
        }
        if (fps == 0 || !callback) break;

        // 即使 LVGL 在 worker 忙时发布多帧，也要保持配置的 FPS 上限。
        // 二值信号量会把突发通知合并为一个，只保留最新画面，避免旧截图排队。
        const int64_t interval_us = 1000000LL / static_cast<int64_t>(fps);
        if (last_attempt_at_us >= 0) {
            const int64_t remaining_us = interval_us - (esp_timer_get_time() - last_attempt_at_us);
            if (remaining_us > 0) {
                const TickType_t wait_ticks = pdMS_TO_TICKS(
                    std::max<int64_t>(1, (remaining_us + 999) / 1000));
                // 限速窗口内不要消费通知；保留通知后，间隔到期即可立即编码。
                vTaskDelay(std::max<TickType_t>(1, std::min<TickType_t>(wait_ticks, pdMS_TO_TICKS(20))));
                continue;
            }
        }

        // 尚未发布画面，或上次编码期间已经消费通知时，等待新的发布事件。
        // 超时用于在没有新的刷新时仍能观察到停止标记。
        if (frame_ready_semaphore_ != nullptr) {
            xSemaphoreTake(frame_ready_semaphore_, pdMS_TO_TICKS(std::max(20, 1000 / static_cast<int>(fps))));
        } else {
            vTaskDelay(pdMS_TO_TICKS(std::max(20, 1000 / static_cast<int>(fps))));
        }

        {
            SemaphoreLock lock(mutex_);
            if (jpeg_stream_stop_requested_ || !jpeg_stream_running_) break;
            // 静态画面不必反复分配 150 KiB 帧副本。
            if (!capture_running_ || !has_frame_ || latest_frame_.sequence == last_sequence) continue;
        }

        {
            std::vector<uint8_t> jpeg;
            EncodeMetrics metrics;
            uint32_t sequence = 0;
            int64_t timestamp_us = 0;
            const char* error = nullptr;
            ++stats_attempts;
            if (EncodeLatestJpeg(last_sequence, jpeg, sequence, timestamp_us, &metrics, error)) {
                last_sequence = sequence;
                ++stats_encoded;
                stats_conversion_us += metrics.conversion_us;
                stats_open_us += metrics.open_us;
                stats_process_us += metrics.process_us;
                stats_close_us += metrics.close_us;
                stats_output_bytes += metrics.output_size;
                try {
                    (*callback)(std::move(jpeg), sequence, timestamp_us);
                } catch (const std::bad_alloc&) {
                    // 回调可能已接收该帧，不能自动重放同一 sequence。
                    ++stats_failed;
                    SetError("Not enough memory to deliver display JPEG");
                }
            } else {
                ++stats_failed;
            }
            if (!heap_first_logged && metrics.heap[0].at_us >= 0) {
                LogEncoderHeapFirst(metrics.heap, sequence);
                heap_first_logged = true;
            }
            for (size_t stage = 0; stage < heap_window.size(); ++stage) {
                heap_window[stage].Observe(metrics.heap[stage], sequence);
            }
            const int64_t now_us = esp_timer_get_time();
            // 复制或编码失败也消耗一次尝试，避免持续刷新在 OOM 时形成忙循环。
            last_attempt_at_us = now_us;
            if (now_us - stats_started_us >= kJpegStatsIntervalUs) {
                const uint32_t sample_count = stats_encoded;
                ESP_LOGI(TAG,
                         "JPEG stats: attempts=%" PRIu32 " encoded=%" PRIu32
                         " failed=%" PRIu32 " fps=%.2f avg_us(cvt/open/proc/close)="
                         "%.0f/%.0f/%.0f/%.0f avg_bytes=%u",
                         stats_attempts, stats_encoded, stats_failed,
                         static_cast<double>(stats_encoded) * 1000000.0 /
                             static_cast<double>(std::max<int64_t>(1, now_us - stats_started_us)),
                         sample_count == 0 ? 0.0 : static_cast<double>(stats_conversion_us) / sample_count,
                         sample_count == 0 ? 0.0 : static_cast<double>(stats_open_us) / sample_count,
                         sample_count == 0 ? 0.0 : static_cast<double>(stats_process_us) / sample_count,
                         sample_count == 0 ? 0.0 : static_cast<double>(stats_close_us) / sample_count,
                         sample_count == 0 ? 0U
                                           : static_cast<unsigned>(stats_output_bytes / sample_count));
                LogEncoderHeapWindow(heap_window, "periodic");
                heap_window = {};
                stats_started_us = now_us;
                stats_attempts = 0;
                stats_encoded = 0;
                stats_failed = 0;
                stats_conversion_us = 0;
                stats_open_us = 0;
                stats_process_us = 0;
                stats_close_us = 0;
                stats_output_bytes = 0;
            }
        }
    }
    LogEncoderHeapWindow(heap_window, "final");
    std::shared_ptr<JpegFrameCallback> callback_to_release;
    {
        SemaphoreLock lock(mutex_);
        jpeg_stream_running_ = false;
        jpeg_stream_stop_requested_ = false;
        jpeg_stream_fps_ = 0;
        callback_to_release = std::move(jpeg_stream_callback_);
        jpeg_stream_task_ = nullptr;
        jpeg_stream_task_ready_ = false;
        jpeg_stream_completed_generation_ =
            std::max(jpeg_stream_completed_generation_, stream_generation);
    }
}

std::string DisplayService::last_error() const {
    if (mutex_ == nullptr) return last_error_;
    SemaphoreLock lock(mutex_);
    return last_error_;
}

void DisplayService::SetError(const char* error) {
    ESP_LOGW(TAG, "%s", error);
    if (mutex_ == nullptr) {
        last_error_ = error;
        return;
    }
    SemaphoreLock lock(mutex_);
    last_error_ = error;
}

}  // namespace rodakos
