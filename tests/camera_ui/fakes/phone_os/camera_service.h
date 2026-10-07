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
    bool ok = true;
    std::string path = "/photos/test.jpg";
    std::string error;
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
    bool GetLatestFrame(CameraFrame& frame) {
        if (!running || !has_frame) return false;
        frame.width = 32;
        frame.height = 24;
        frame.stride = 64;
        frame.sequence = sequence;
        frame.rgb565.assign(32 * 24 * 2, 0x58);
        return true;
    }
    CameraState GetState() const {
        CameraState state;
        state.preview_running = running;
        state.has_frame = has_frame;
        state.last_error = preview_error;
        return state;
    }
    std::string last_error() const {
        return camera_test::capture_worker ? capture_error_ : preview_error;
    }
    bool CapturePhoto(std::string& saved_path) {
        camera_test::CaptureOutcome outcome;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const size_t index = captures++;
            if (index < outcomes_.size()) outcome = outcomes_[index];
        }
        if (outcome.gate) outcome.gate->Enter();
        saved_path = outcome.ok ? outcome.path : std::string{};
        capture_error_ = outcome.error;
        ++completed;
        return outcome.ok;
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
    uint32_t sequence = 1;
    std::string preview_error;
    int starts = 0;
    int stops = 0;
    std::atomic<size_t> captures{0};
    std::atomic<size_t> completed{0};
private:
    std::mutex mutex_;
    std::vector<camera_test::CaptureOutcome> outcomes_;
    inline static thread_local std::string capture_error_;
};
}
