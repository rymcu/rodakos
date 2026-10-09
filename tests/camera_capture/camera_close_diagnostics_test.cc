#include "test_framework.h"
#include "host_runtime.h"
#include "phone_os/camera-teardown-diagnostics.h"
#include "phone_os/camera_service.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

namespace {
using namespace std::chrono_literals;

class Gate {
public:
    void Block() {
        std::unique_lock<std::mutex> lock(mutex_);
        entered_ = true;
        condition_.notify_all();
        condition_.wait(lock, [this] { return released_; });
    }
    bool WaitEntered() {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, 2s, [this] { return entered_; });
    }
    void Release() {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        condition_.notify_all();
    }
private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool entered_ = false;
    bool released_ = false;
};

struct ReleaseOnExit {
    Gate& gate;
    ~ReleaseOnExit() { gate.Release(); }
};

struct Fixture {
    rodakos::CameraService camera{nullptr};
    ~Fixture() {
        camera.StopPreview();
        camera_host::JoinTasks();
        camera_host::Reset("");
    }
    void Preview() {
        RODAK_CHECK(camera.StartPreview(2, 2));
        for (unsigned attempt = 0; attempt < 1000; ++attempt) {
            if (camera.GetState().has_frame) return;
            std::this_thread::sleep_for(1ms);
        }
        throw std::runtime_error("fake camera did not deliver a preview frame");
    }
};

void CheckSnapshot(uint32_t count, int status) {
    RodakCameraTeardownSnapshot snapshot{};
    RODAK_CHECK_EQ(rodak_camera_teardown_snapshot(&snapshot), 0u);
    RODAK_CHECK_EQ(snapshot.claimed_begin, count);
    RODAK_CHECK_EQ(snapshot.claimed_end, count);
    RODAK_CHECK_EQ(snapshot.committed_records, count);
    RODAK_CHECK_EQ(snapshot.pending_records, 0u);
    const uint32_t phases[] = {
        RODAK_CAMERA_TEARDOWN_IOCTL_ENTER, RODAK_CAMERA_TEARDOWN_IOCTL_RETURNED,
        RODAK_CAMERA_TEARDOWN_BEFORE_LOG, RODAK_CAMERA_TEARDOWN_AFTER_LOG
    };
    for (uint32_t index = 0; index < count; ++index) {
        RODAK_CHECK_EQ(snapshot.records[index].phase, phases[index % 4]);
        RODAK_CHECK_EQ(snapshot.records[index].commit_seq, index + 1);
        RODAK_CHECK_EQ(snapshot.records[index].core, 0u);
        RODAK_CHECK_EQ(snapshot.records[index].status, index % 4 == 0 ? 0 : status);
    }
    for (uint32_t index = count; index < RODAK_CAMERA_TEARDOWN_CAPACITY; ++index) {
        RODAK_CHECK_EQ(snapshot.records[index].commit_seq, 0u);
    }
}

void Run(bool block_log, int status) {
    camera_host::Reset("");
    Gate gate;
    Fixture fixture;
    ReleaseOnExit fixture_release{gate};
    camera_host::streamoff_result = status;
    if (block_log) {
        camera_host::log_hook = [&gate, status](const char* format) {
            const char* expected = status == 0
                                       ? "CloseStream: STREAMOFF complete"
                                       : "CloseStream: STREAMOFF failed; retaining fd and buffers for retry: %s";
            if (std::strcmp(format, expected) == 0) gate.Block();
        };
    } else {
        camera_host::streamoff_hook = [&gate] { gate.Block(); };
    }
    CheckSnapshot(0, status);
    fixture.Preview();
    auto stopped = std::async(std::launch::async, [&fixture] { fixture.camera.StopPreview(); });
    // Unwind must release the worker before the future's joining destructor.
    ReleaseOnExit release{gate};
    RODAK_CHECK(gate.WaitEntered());
    CheckSnapshot(block_log ? 3 : 1, status);
    RODAK_CHECK(fixture.camera.GetState().preview_running);
    RODAK_CHECK_EQ(stopped.wait_for(20ms), std::future_status::timeout);
    gate.Release();
    RODAK_CHECK_EQ(stopped.wait_for(3s), std::future_status::ready);
    stopped.get();
    camera_host::JoinTasks();
    CheckSnapshot(status == 0 ? 4 : 8, status);
    RODAK_CHECK_FALSE(fixture.camera.GetState().preview_running);
    if (status != 0) {
        // A failed STREAMOFF keeps the V4L2 ownership alive so the DVP
        // controller can be stopped again. Releasing the fd or mappings here
        // would make the lower-level cleanup permanently unrecoverable.
        RODAK_CHECK(camera_host::frame_mappings.load() > 0u);
        camera_host::streamoff_result = 0;
        fixture.camera.StopPreview();
        camera_host::JoinTasks();
    }
    RODAK_CHECK_EQ(camera_host::frame_mappings.load(), 0u);
    RODAK_CHECK_EQ(camera_host::preview_frame_bytes.load(), 0u);
}
}

int main(int argc, char** argv) {
    try {
        RODAK_CHECK(argc == 3);
        RODAK_CHECK(std::strcmp(argv[1], "ioctl") == 0 || std::strcmp(argv[1], "log") == 0);
        RODAK_CHECK(std::strcmp(argv[2], "0") == 0 || std::strcmp(argv[2], "-1") == 0);
        Run(std::strcmp(argv[1], "log") == 0, std::strcmp(argv[2], "0") == 0 ? 0 : -1);
        std::cout << "[PASS] CloseStream " << argv[1] << " status=" << argv[2] << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
