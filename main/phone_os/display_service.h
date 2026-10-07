#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "phone_os/task-retirement.h"

namespace rodakos {

struct DisplayFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<uint8_t> rgb565;
    int64_t timestamp_us = 0;
    uint32_t sequence = 0;
};

class DisplayService {
public:
    using JpegFrameCallback =
        std::function<void(std::vector<uint8_t>&&, uint32_t sequence, int64_t timestamp_us)>;

    explicit DisplayService(lv_display_t* display);
    ~DisplayService();

    DisplayService(const DisplayService&) = delete;
    DisplayService& operator=(const DisplayService&) = delete;

    bool Attach();
    bool StartCapture(int width = 320, int height = 240);
    void StopCapture();
    bool GetLatestFrame(DisplayFrame& frame);
    bool CaptureJpeg(std::vector<uint8_t>& jpeg);
    bool StartJpegStream(uint8_t fps, JpegFrameCallback callback);
    void StopJpegStream();
    bool IsRunning() const;
    std::string last_error() const;

private:
    struct EncodeMetrics;

    static void OnDisplayEvent(lv_event_t* event);
    static void JpegStreamTaskEntry(void* arg);
    void AttachWithLvglLock();
    void DetachWithLvglLock();
    void HandleDisplayEvent(lv_event_t* event);
    void JpegStreamTask();
    bool EncodeJpeg(const uint8_t* input, size_t input_size, int width, int height,
                    std::vector<uint8_t>& jpeg, EncodeMetrics* metrics);
    bool EncodeLatestJpeg(uint32_t previous_sequence, std::vector<uint8_t>& jpeg,
                          uint32_t& sequence, int64_t& timestamp_us, EncodeMetrics* metrics,
                          const char*& error);
    void SetError(const char* error);

    lv_display_t* display_ = nullptr;
    bool event_attached_ = false;
    mutable std::mutex lifecycle_mutex_;
    SemaphoreHandle_t mutex_ = nullptr;
    TaskRetirementOwner task_retirement_owner_;
    TaskRetirementTicket jpeg_stream_retirement_;
    TaskHandle_t jpeg_stream_task_ = nullptr;
    // 完整 LVGL 帧发布后立即唤醒 JPEG worker。按目标 FPS 轮询最多会增加一
    // 个帧间隔的延迟，页面切换后容易短暂显示旧桌面。
    SemaphoreHandle_t frame_ready_semaphore_ = nullptr;
    bool jpeg_stream_task_ready_ = false;
    bool closing_ = false;
    bool capture_running_ = false;
    bool frame_pending_ = false;
    bool jpeg_stream_running_ = false;
    bool jpeg_stream_stop_requested_ = false;
    uint8_t jpeg_stream_fps_ = 0;
    std::shared_ptr<JpegFrameCallback> jpeg_stream_callback_;
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> mirror_rgb565_;
    DisplayFrame latest_frame_;
    bool has_frame_ = false;
    uint32_t frame_count_ = 0;
    // 错误路径也可能没有可用堆空间；这里只保留静态诊断文本。
    const char* last_error_ = "";
};

}  // namespace rodakos
