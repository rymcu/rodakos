#include "phone_os/display_service.h"

#include <algorithm>
#include <inttypes.h>
#include <cstring>
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
constexpr uint32_t kTaskStackSize = 8192;
constexpr int64_t kJpegStatsIntervalUs = 5 * 1000 * 1000;

struct EncodeMetrics {
    int64_t conversion_us = 0;
    int64_t open_us = 0;
    int64_t process_us = 0;
    int64_t close_us = 0;
    size_t output_size = 0;
};

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

bool EncodeJpeg(const uint8_t* input, size_t input_size, int width, int height,
                std::vector<uint8_t>& jpeg, EncodeMetrics* metrics) {
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
    if (open_ret != JPEG_ERR_OK || encoder == nullptr) return false;
    std::vector<uint8_t> encoded(std::max<size_t>(64 * 1024, input_size));
    int output_size = 0;
    const int64_t process_started_us = esp_timer_get_time();
    const auto ret = jpeg_enc_process(encoder, input, static_cast<int>(input_size),
                                      encoded.data(), static_cast<int>(encoded.size()), &output_size);
    if (metrics != nullptr) metrics->process_us = esp_timer_get_time() - process_started_us;
    const int64_t close_started_us = esp_timer_get_time();
    jpeg_enc_close(encoder);
    if (metrics != nullptr) metrics->close_us = esp_timer_get_time() - close_started_us;
    if (ret != JPEG_ERR_OK || output_size <= 0 || static_cast<size_t>(output_size) > encoded.size()) {
        return false;
    }
    encoded.resize(static_cast<size_t>(output_size));
    if (metrics != nullptr) metrics->output_size = encoded.size();
    jpeg = std::move(encoded);
    return true;
}

bool EncodeRgb565(const std::vector<uint8_t>& rgb565, int width, int height,
                  std::vector<uint8_t>& jpeg, EncodeMetrics* metrics = nullptr) {
    jpeg.clear();
    if (width <= 0 || height <= 0 || rgb565.size() < static_cast<size_t>(width) * height * 2) {
        return false;
    }
    const size_t pixel_count = static_cast<size_t>(width) * height;
    // ESP_NEW_JPEG 的编码输入支持 RGB888，不支持 RGB565；
    // RGB565 常量用于解码输出，不能直接交给编码器。
    const size_t rgb888_size = pixel_count * 3;
    auto* aligned_rgb888 = AllocAligned(rgb888_size);
    if (aligned_rgb888 == nullptr) return false;
    const int64_t conversion_started_us = esp_timer_get_time();
    for (size_t i = 0; i < pixel_count; ++i) {
        const uint16_t pixel = static_cast<uint16_t>(rgb565[i * 2]) |
                               (static_cast<uint16_t>(rgb565[i * 2 + 1]) << 8);
        const uint8_t r5 = static_cast<uint8_t>((pixel >> 11) & 0x1f);
        const uint8_t g6 = static_cast<uint8_t>((pixel >> 5) & 0x3f);
        const uint8_t b5 = static_cast<uint8_t>(pixel & 0x1f);
        aligned_rgb888[i * 3] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        aligned_rgb888[i * 3 + 1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
        aligned_rgb888[i * 3 + 2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    }
    if (metrics != nullptr) metrics->conversion_us = esp_timer_get_time() - conversion_started_us;
    const bool encoded = EncodeJpeg(aligned_rgb888, rgb888_size, width, height, jpeg, metrics);
    heap_caps_free(aligned_rgb888);
    return encoded;
}
}  // namespace

DisplayService::DisplayService(lv_display_t* display) : display_(display) {
    mutex_ = xSemaphoreCreateMutex();
    frame_ready_semaphore_ = xSemaphoreCreateBinary();
}

DisplayService::~DisplayService() {
    StopJpegStream();
    StopCapture();
    if (display_ != nullptr && lvgl_port_lock(1000)) {
        lv_async_call_cancel(&RefreshDisplayForCapture, display_);
        lvgl_port_unlock();
    }
    if (event_attached_ && display_ != nullptr) {
        lv_display_remove_event_cb_with_user_data(display_, &DisplayService::OnDisplayEvent, this);
        event_attached_ = false;
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
    if (display_ == nullptr || event_attached_) return display_ != nullptr;
    lv_display_add_event_cb(display_, &DisplayService::OnDisplayEvent, LV_EVENT_ALL, this);
    event_attached_ = true;
    return true;
}

bool DisplayService::StartCapture(int width, int height) {
    if (display_ == nullptr || !Attach() || width <= 0 || height <= 0 ||
        lv_display_get_horizontal_resolution(display_) != width ||
        lv_display_get_vertical_resolution(display_) != height) {
        SetError("Unsupported display capture dimensions");
        return false;
    }
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (capture_running_) {
        xSemaphoreGive(mutex_);
        return true;
    }
    const size_t frame_size = static_cast<size_t>(width) * height * 2;
    // 持久镜像必须保留所有未刷新的像素；发布缓冲仅供 JPEG worker 读取。
    mirror_rgb565_.assign(frame_size, 0);
    latest_frame_ = {};
    latest_frame_.rgb565.assign(frame_size, 0);
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
    xSemaphoreGive(mutex_);
    // MQTT 只安排刷新，实际绘制在 LVGL 任务完成。
    bool refresh_queued = false;
    if (lvgl_port_lock(1000)) {
        refresh_queued = lv_async_call(&RefreshDisplayForCapture, display_) == LV_RESULT_OK;
        lvgl_port_unlock();
    }
    if (!refresh_queued) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        capture_running_ = false;
        mirror_rgb565_.clear();
        latest_frame_ = {};
        xSemaphoreGive(mutex_);
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
    xSemaphoreTake(mutex_, portMAX_DELAY);
    capture_running_ = false;
    mirror_rgb565_.clear();
    latest_frame_ = {};
    has_frame_ = false;
    frame_pending_ = false;
    xSemaphoreGive(mutex_);
    if (frame_ready_semaphore_ != nullptr) xSemaphoreGive(frame_ready_semaphore_);
}

bool DisplayService::IsRunning() const {
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool running = capture_running_;
    xSemaphoreGive(mutex_);
    return running;
}

bool DisplayService::GetLatestFrame(DisplayFrame& frame) {
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool ok = capture_running_ && has_frame_;
    if (ok) frame = latest_frame_;
    xSemaphoreGive(mutex_);
    return ok;
}

bool DisplayService::CaptureJpeg(std::vector<uint8_t>& jpeg) {
    DisplayFrame frame;
    if (!GetLatestFrame(frame) || frame.width <= 0 || frame.height <= 0) {
        SetError("No display frame is ready yet");
        return false;
    }
    if (!EncodeRgb565(frame.rgb565, frame.width, frame.height, jpeg)) {
        SetError("Display JPEG encode failed");
        return false;
    }
    return true;
}

bool DisplayService::StartJpegStream(uint8_t fps, JpegFrameCallback callback) {
    if (fps == 0 || fps > 5 || !callback || mutex_ == nullptr) {
        SetError("Invalid display JPEG stream configuration");
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!capture_running_ || jpeg_stream_running_) {
        xSemaphoreGive(mutex_);
        SetError(!capture_running_ ? "Display capture is not running" : "Display JPEG stream is already running");
        return false;
    }
    jpeg_stream_fps_ = fps;
    jpeg_stream_callback_ = std::move(callback);
    jpeg_stream_stop_requested_ = false;
    jpeg_stream_running_ = true;
    jpeg_stream_task_ready_ = false;
    TaskHandle_t task = nullptr;
    if (xTaskCreateWithCaps(JpegStreamTaskEntry, "display_jpeg", kTaskStackSize, this, 3, &task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        jpeg_stream_running_ = false;
        jpeg_stream_callback_ = {};
        xSemaphoreGive(mutex_);
        SetError("Failed to start display JPEG stream task");
        return false;
    }
    jpeg_stream_task_ = task;
    jpeg_stream_task_ready_ = true;
    xSemaphoreGive(mutex_);
    return true;
}

void DisplayService::StopJpegStream() {
    if (mutex_ == nullptr) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const TaskHandle_t task = jpeg_stream_task_;
    jpeg_stream_stop_requested_ = true;
    xSemaphoreGive(mutex_);
    if (frame_ready_semaphore_ != nullptr) xSemaphoreGive(frame_ready_semaphore_);
    if (task == nullptr || task == xTaskGetCurrentTaskHandle()) return;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20));
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool running = jpeg_stream_task_ != nullptr;
        xSemaphoreGive(mutex_);
        if (!running) break;
    }
}

void DisplayService::OnDisplayEvent(lv_event_t* event) {
    auto* service = static_cast<DisplayService*>(lv_event_get_user_data(event));
    if (service != nullptr) service->HandleDisplayEvent(event);
}

void DisplayService::HandleDisplayEvent(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if ((code != LV_EVENT_FLUSH_START && code != LV_EVENT_FLUSH_FINISH) || mutex_ == nullptr) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool active = capture_running_ && width_ > 0 && height_ > 0 && !mirror_rgb565_.empty();
    xSemaphoreGive(mutex_);
    if (!active) return;
    auto* display = static_cast<lv_display_t*>(lv_event_get_current_target(event));
    if (code == LV_EVENT_FLUSH_FINISH) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
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
        xSemaphoreGive(mutex_);
        return;
    }
    auto* area = static_cast<lv_area_t*>(lv_event_get_param(event));
    auto* draw_buf = lv_display_get_buf_active(display);
    if (area == nullptr || draw_buf == nullptr || draw_buf->data == nullptr) return;
    const int area_width = lv_area_get_width(area);
    const int area_height = lv_area_get_height(area);
    if (area->x1 < 0 || area->y1 < 0 || area->x2 >= width_ || area->y2 >= height_ ||
        area_width <= 0 || area_height <= 0 || draw_buf->header.stride < area_width * 2) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (int y = 0; y < area_height; ++y) {
        const auto* source = draw_buf->data + static_cast<size_t>(y) * draw_buf->header.stride;
        auto* target = mirror_rgb565_.data() +
                       (static_cast<size_t>(area->y1 + y) * width_ + area->x1) * 2;
        std::memcpy(target, source, static_cast<size_t>(area_width) * 2);
    }
    frame_pending_ = true;
    xSemaphoreGive(mutex_);
}

void DisplayService::JpegStreamTaskEntry(void* arg) {
    auto* service = static_cast<DisplayService*>(arg);
    if (service != nullptr) service->JpegStreamTask();
    vTaskDeleteWithCaps(nullptr);
}

void DisplayService::JpegStreamTask() {
    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool ready = jpeg_stream_task_ready_;
        xSemaphoreGive(mutex_);
        if (ready) break;
        vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(1)));
    }
    uint32_t last_sequence = 0;
    int64_t last_encode_at_us = 0;
    int64_t stats_started_us = esp_timer_get_time();
    uint32_t stats_attempts = 0;
    uint32_t stats_encoded = 0;
    uint32_t stats_failed = 0;
    int64_t stats_conversion_us = 0;
    int64_t stats_open_us = 0;
    int64_t stats_process_us = 0;
    int64_t stats_close_us = 0;
    size_t stats_output_bytes = 0;
    while (true) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool stop = jpeg_stream_stop_requested_ || !jpeg_stream_running_;
        const uint8_t fps = jpeg_stream_fps_;
        auto callback = jpeg_stream_callback_;
        xSemaphoreGive(mutex_);
        if (stop || fps == 0 || !callback) break;

        // 即使 LVGL 在 worker 忙时发布多帧，也要保持配置的 FPS 上限。
        // 二值信号量会把突发通知合并为一个，只保留最新画面，避免旧截图排队。
        const int64_t interval_us = 1000000LL / static_cast<int64_t>(fps);
        if (last_encode_at_us != 0) {
            const int64_t remaining_us = interval_us - (esp_timer_get_time() - last_encode_at_us);
            if (remaining_us > 0) {
                const TickType_t wait_ticks = pdMS_TO_TICKS(
                    std::max<int64_t>(1, (remaining_us + 999) / 1000));
                // 限速窗口内不要消费通知；保留通知后，间隔到期即可立即编码。
                vTaskDelay(wait_ticks);
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

        xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool woke_stop = jpeg_stream_stop_requested_ || !jpeg_stream_running_;
        xSemaphoreGive(mutex_);
        if (woke_stop) break;

        DisplayFrame frame;
        if (GetLatestFrame(frame) && frame.sequence != last_sequence) {
            std::vector<uint8_t> jpeg;
            EncodeMetrics metrics;
            ++stats_attempts;
            if (EncodeRgb565(frame.rgb565, frame.width, frame.height, jpeg, &metrics)) {
                last_sequence = frame.sequence;
                last_encode_at_us = esp_timer_get_time();
                ++stats_encoded;
                stats_conversion_us += metrics.conversion_us;
                stats_open_us += metrics.open_us;
                stats_process_us += metrics.process_us;
                stats_close_us += metrics.close_us;
                stats_output_bytes += metrics.output_size;
                callback(std::move(jpeg), frame.sequence, frame.timestamp_us);
            } else {
                ++stats_failed;
            }
            const int64_t now_us = esp_timer_get_time();
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
    xSemaphoreTake(mutex_, portMAX_DELAY);
    jpeg_stream_running_ = false;
    jpeg_stream_stop_requested_ = false;
    jpeg_stream_fps_ = 0;
    jpeg_stream_callback_ = {};
    jpeg_stream_task_ = nullptr;
    jpeg_stream_task_ready_ = false;
    xSemaphoreGive(mutex_);
}

std::string DisplayService::last_error() const {
    if (mutex_ == nullptr) return last_error_;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const std::string error = last_error_;
    xSemaphoreGive(mutex_);
    return error;
}

void DisplayService::SetError(const std::string& error) {
    ESP_LOGW(TAG, "%s", error.c_str());
    if (mutex_ == nullptr) {
        last_error_ = error;
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    last_error_ = error;
    xSemaphoreGive(mutex_);
}

}  // namespace rodakos
