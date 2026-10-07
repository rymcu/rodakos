#pragma once
#include "host_runtime.h"
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace rodakos {
class DisplayService {
public:
    using JpegFrameCallback = std::function<void(std::vector<uint8_t>&&, uint32_t, int64_t)>;
    bool capture_allowed = true;
    bool jpeg_allowed = true;
    bool capture_running = false;
    bool jpeg_running = false;
    bool StartCapture(int, int) { capture_running = capture_allowed; return capture_allowed; }
    void StopCapture() { capture_running = false; }
    bool StartJpegStream(uint8_t, JpegFrameCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        jpeg_running = jpeg_allowed;
        if (jpeg_running) jpeg_callback_ = std::move(callback);
        return jpeg_allowed;
    }
    void StopJpegStream() {
        rodakos_test::display_host::NotifyCleanupEntry();
        std::lock_guard<std::mutex> lock(callback_mutex_);
        jpeg_running = false;
        jpeg_callback_ = {};
    }
    bool EmitJpeg(std::vector<uint8_t>&& jpeg, uint32_t sequence, int64_t timestamp_us) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (!jpeg_running || !jpeg_callback_) return false;
        jpeg_callback_(std::move(jpeg), sequence, timestamp_us);
        return true;
    }
    std::string last_error() const { return "host display rejected"; }
private:
    std::mutex callback_mutex_;
    JpegFrameCallback jpeg_callback_;
};
}
