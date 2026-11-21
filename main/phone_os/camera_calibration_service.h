#pragma once

#include "phone_os/camera_calibration.h"
#include "task-retirement.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace rodakos {

class CameraService;

// Background worker for the camera calibration page. It never starts or stops the preview: the
// Camera app owns the preview lease, and this worker only reads the latest frame, decodes the
// calibration chart's QR code and scores sharpness. The page polls Snapshot().
//
// Large buffers (frame copy, luma image, quirc) live in PSRAM; the 16 KiB stack too, because
// quirc_end needs about 10 KiB. Internal RAM is not used for anything but the task control block.
class CameraCalibrationService {
public:
    struct Snapshot {
        bool running = false;
        // Valid once `revision` is nonzero.
        CameraCalibrationResult result;
        // Image size the result coordinates refer to.
        int frame_width = 0;
        int frame_height = 0;
        uint32_t revision = 0;
    };

    explicit CameraCalibrationService(CameraService* camera);
    ~CameraCalibrationService();
    CameraCalibrationService(const CameraCalibrationService&) = delete;
    CameraCalibrationService& operator=(const CameraCalibrationService&) = delete;

    bool Start();
    // Asks the worker to stop; returns at once. Results from the stopped run are discarded.
    void Stop();
    Snapshot snapshot() const;

private:
    static void TaskBody(void* arg);
    void Run(uint32_t generation);

    CameraService* camera_ = nullptr;
    mutable std::mutex mutex_;
    Snapshot snapshot_;
    std::atomic<uint32_t> generation_{0};
    std::atomic<bool> busy_{false};
    TaskRetirementOwner retirement_owner_;
    TaskRetirementTicket retirement_;
};

}  // namespace rodakos
