#pragma once
// Host stand-in for the calibration worker: the Camera UI tests cover the app's lifecycle, not
// frame analysis (tests/camera_calibration covers that with real fixtures).
#include "phone_os/camera_calibration.h"

#include <cstdint>

namespace rodakos {
class CameraService;

class CameraCalibrationService {
public:
    struct Snapshot {
        bool running = false;
        CameraCalibrationResult result;
        int frame_width = 0;
        int frame_height = 0;
        uint32_t revision = 0;
    };
    explicit CameraCalibrationService(CameraService*) {}
    bool Start() { running_ = true; return true; }
    void Stop() { running_ = false; }
    Snapshot snapshot() const { Snapshot s; s.running = running_; return s; }

private:
    bool running_ = false;
};
}  // namespace rodakos
