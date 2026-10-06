#pragma once
#include "host_runtime.h"
#include <cstdint>
#include <functional>
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
    bool StartJpegStream(uint8_t, JpegFrameCallback) {
        jpeg_running = jpeg_allowed;
        return jpeg_allowed;
    }
    void StopJpegStream() {
        rodakos_test::display_host::NotifyCleanupEntry();
        jpeg_running = false;
    }
    std::string last_error() const { return "host display rejected"; }
};
}
