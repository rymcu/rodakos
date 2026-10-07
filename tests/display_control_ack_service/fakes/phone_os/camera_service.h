#pragma once
#include "host_runtime.h"
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace rodakos {
class CameraService {
public:
    enum class PreviewOwner { kRemote };
    using JpegFrameCallback = std::function<void(std::vector<uint8_t>&&, uint32_t, int64_t)>;
    bool preview_allowed = true;
    bool jpeg_allowed = true;
    bool preview_running = false;
    bool jpeg_running = false;
    bool StartPreview(PreviewOwner, int, int) {
        preview_running = preview_allowed;
        return preview_allowed;
    }
    void StopPreview(PreviewOwner) { preview_running = false; }
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
    std::string last_error() const { return "host camera rejected"; }
private:
    std::mutex callback_mutex_;
    JpegFrameCallback jpeg_callback_;
};
}  // namespace rodakos
