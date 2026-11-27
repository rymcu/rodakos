#pragma once
#include "freertos/task.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace rodakos {
enum class CameraCaptureFailureStage : uint8_t {
    kNone,
    kServiceUnavailable,
    kNoFrame,
    kGateTimeout,
    kJpegInputAlloc,
    kJpegEncoderOpen,
    kJpegOutputAlloc,
    kJpegEncodeProcess,
    kJpegCompactAlloc,
    kStorageUnavailable,
    kDirectoryFailed,
    kPathSelectionFailed,
    kWriteFailed,
};

struct CameraCaptureOutcome {
    bool ok = false;
    CameraCaptureFailureStage stage = CameraCaptureFailureStage::kNone;
    int native_error = 0;
};

inline const char* CameraCaptureFailureStageName(CameraCaptureFailureStage) {
    return "fake capture stage";
}

enum class FrameCopyStatus : uint8_t { kUpdated, kUnchanged, kUnavailable, kAllocationFailed };
}

namespace camera_test {
class Gate {
public:
    void Enter() {
        std::unique_lock<std::mutex> lock(mutex_);
        entered_ = true;
        cv_.notify_all();
        cv_.wait(lock, [&] { return released_; });
    }
    bool Wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::seconds(2), [&] { return entered_; });
    }
    void Release() {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        cv_.notify_all();
    }
private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool entered_ = false;
    bool released_ = false;
};
struct CaptureOutcome {
    rodakos::CameraCaptureOutcome result{true, rodakos::CameraCaptureFailureStage::kNone, 0};
    std::string path = "/photos/test.jpg";
    std::shared_ptr<Gate> gate;
};
}
namespace rodakos {
struct CameraFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<uint8_t> rgb565;
    int64_t timestamp_us = 0;
    uint32_t sequence = 0;
};
struct CameraState {
    bool available = true;
    bool preview_running = false;
    bool has_frame = false;
    int width = 0;
    int height = 0;
    uint32_t frame_count = 0;
    std::string last_saved_path;
    std::string last_error;
};
class CameraService {
public:
    bool StartPreview() {
        ++starts;
        if (start_gate) start_gate->Enter();
        running = start_ok;
        if (!start_ok) preview_error = "Camera device unavailable";
        return start_ok;
    }
    void StopPreview() {
        ++stops;
        if (stop_gate) stop_gate->Enter();
        running = false;
    }
    FrameCopyStatus CopyLatestFrameIfNewer(uint32_t previous_sequence, CameraFrame& frame) {
        if (!running || !has_frame) return FrameCopyStatus::kUnavailable;
        if (previous_sequence == sequence) return FrameCopyStatus::kUnchanged;
        if (fail_frame_allocation) return FrameCopyStatus::kAllocationFailed;
        const auto size = static_cast<size_t>(frame_width * frame_height * 2);
        if (frame.rgb565.capacity() < size) ++frame_allocations;
        ++frame_copies;
        frame.rgb565.assign(size, frame_pixel_byte);
        frame.width = frame_width;
        frame.height = frame_height;
        frame.stride = frame_width * 2;
        frame.sequence = sequence;
        return FrameCopyStatus::kUpdated;
    }
    CameraState GetState() const {
        CameraState state;
        state.preview_running = running;
        state.has_frame = has_frame;
        state.last_error = preview_error;
        return state;
    }
    std::string last_error() const {
        return preview_error;
    }
    CameraCaptureOutcome CapturePhotoWithOutcome(std::string& saved_path) {
        camera_test::CaptureOutcome outcome;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const size_t index = captures++;
            if (index < outcomes_.size()) outcome = outcomes_[index];
        }
        if (outcome.gate) outcome.gate->Enter();
        saved_path = outcome.result.ok ? outcome.path : std::string{};
        ++completed;
        return outcome.result;
    }
    void Outcomes(std::vector<camera_test::CaptureOutcome> outcomes) {
        std::lock_guard<std::mutex> lock(mutex_);
        outcomes_ = std::move(outcomes);
    }
    void ReleaseGates() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& outcome : outcomes_) if (outcome.gate) outcome.gate->Release();
    }
    bool start_ok = true;
    std::shared_ptr<camera_test::Gate> start_gate;
    std::shared_ptr<camera_test::Gate> stop_gate;
    bool running = false;
    bool has_frame = true;
    bool fail_frame_allocation = false;
    uint32_t sequence = 1;
    int frame_width = 32;
    int frame_height = 24;
    uint8_t frame_pixel_byte = 0x58;
    size_t frame_allocations = 0;
    size_t frame_copies = 0;
    std::string preview_error;
    int starts = 0;
    int stops = 0;
    std::atomic<size_t> captures{0};
    std::atomic<size_t> completed{0};
private:
    std::mutex mutex_;
    std::vector<camera_test::CaptureOutcome> outcomes_;
};
}
