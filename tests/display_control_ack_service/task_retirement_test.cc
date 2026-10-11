#include "test_framework.h"
#include "host_runtime.h"
#include "task_retirement_host.h"
#include "phone_os/camera_service.h"
#include "phone_os/display_service.h"
#include "phone_os/webrtc_camera_service.h"
#include "phone_os/webrtc_display_service.h"
#include "phone_os/task-retirement.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace {
namespace host = rodakos_test::display_host;
using namespace std::chrono_literals;

struct ResetHost { ResetHost() { host::Reset(); } };
struct Fixture {
    ResetHost reset;
    rodakos::CameraService camera;
    rodakos::DisplayService display;
    rodakos::WebRtcCameraService camera_peer{&camera};
    rodakos::WebRtcDisplayService display_peer{&display};
    uint64_t nonce = 0;

    ~Fixture() {
        camera_peer.Stop();
        display_peer.Stop();
        host::JoinTasks();
    }
    bool Start(bool camera_kind, std::function<void(esp_peer_state_t)> callback = {}) {
        if (camera_kind) return camera_peer.Start({}, [](auto, auto&&) {}, std::move(callback));
        rodakos::WebRtcDisplayService::Config config;
        config.stream_lease = std::make_shared<rodakos::StreamLease>(1, 1, ++nonce, "retirement");
        return display_peer.Start(config, [](auto, auto&&) {}, std::move(callback));
    }
    void Stop(bool camera_kind) {
        if (camera_kind) camera_peer.Stop();
        else display_peer.Stop();
    }
    bool Running(bool camera_kind) {
        return camera_kind ? camera_peer.IsRunning() : display_peer.IsRunning();
    }
};

template <typename F> bool Wait(F&& predicate, std::chrono::milliseconds timeout = 2500ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}
}  // namespace

namespace {
void CheckNoCleanupAllocation(bool camera_kind) {
    Fixture fixture;
    retirement_host::RejectCleanupTask(true);
    RODAK_CHECK(fixture.Start(camera_kind));
    fixture.Stop(camera_kind);
    host::JoinTasks();
    const auto tasks = retirement_host::Snapshot();
    RODAK_CHECK_EQ(tasks.cleanup_create_attempts, 0u);
    RODAK_CHECK_EQ(tasks.task_deletes, 1u);
    RODAK_CHECK_EQ(tasks.live_tasks, 0u);
    RODAK_CHECK_EQ(tasks.live_task_buffers, 0u);
}
}  // namespace
RODAK_TEST("retirement camera peer exits without allocating an IDF cleanup task") {
    CheckNoCleanupAllocation(true);
}
RODAK_TEST("retirement display peer exits without allocating an IDF cleanup task") {
    CheckNoCleanupAllocation(false);
}

RODAK_TEST("retirement peer late Stop waits for callback destruction and allows self Stop") {
    for (bool camera_kind : {false, true}) {
        Fixture fixture;
        retirement_host::Gate destructor;
        std::atomic<size_t> callbacks{0};
        auto token = std::shared_ptr<int>(new int(1), [&](int* value) {
            delete value;
            destructor.Enter();
        });
        std::function<void(esp_peer_state_t)> callback = [&, token](auto) {
            fixture.Stop(camera_kind);
            ++callbacks;
        };
        token.reset();
        RODAK_CHECK(fixture.Start(camera_kind, std::move(callback)));
        host::EmitState(host::LatestPeer(), ESP_PEER_STATE_DISCONNECTED);
        host::RunPeerTasks();
        const bool entered = destructor.Wait();
        std::atomic<bool> returned{false};
        std::thread stopping([&] { fixture.Stop(camera_kind); returned = true; });
        std::this_thread::sleep_for(30ms);
        rodakos::PumpTaskRetirements();
        const bool returned_early = returned.load();
        const size_t deleted_early = retirement_host::Snapshot().task_deletes;
        destructor.Release();
        stopping.join();
        host::JoinTasks();
        RODAK_CHECK(entered);
        RODAK_CHECK_EQ(callbacks.load(), 1u);
        RODAK_CHECK_FALSE(returned_early);
        RODAK_CHECK_EQ(deleted_early, 0u);
        RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 1u);
        RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
    }
}

RODAK_TEST("retirement peer Stop joins old generation while callback destructor starts replacement") {
    for (bool camera_kind : {false, true}) {
        Fixture fixture;
        retirement_host::Gate destructor;
        std::atomic<bool> replacement_started{false};
        auto token = std::shared_ptr<int>(new int(1), [&](int* value) {
            delete value;
            replacement_started = fixture.Start(camera_kind);
            destructor.Enter();
        });
        std::function<void(esp_peer_state_t)> callback = [token](auto) {};
        token.reset();
        RODAK_CHECK(fixture.Start(camera_kind, std::move(callback)));
        const auto old_peer = host::LatestPeer();
        std::atomic<bool> old_joined{false};
        std::thread stopping([&] { fixture.Stop(camera_kind); old_joined = true; });
        const bool entered = destructor.Wait();
        const bool replacement_running = fixture.Running(camera_kind);
        const bool returned_early = old_joined.load();
        destructor.Release();
        const bool joined_old_only = Wait([&] { return old_joined.load(); }, 500ms);
        // Always release the replacement before reporting a failed assertion so
        // a cross-generation negative control does not merely time out.
        fixture.Stop(camera_kind);
        stopping.join();
        host::JoinTasks();
        RODAK_CHECK(entered);
        RODAK_CHECK(replacement_started.load());
        RODAK_CHECK(replacement_running);
        RODAK_CHECK_FALSE(returned_early);
        RODAK_CHECK(joined_old_only);
        RODAK_CHECK(host::IsClosed(old_peer));
        RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 2u);
        RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
    }
}

RODAK_TEST("retirement autonomous peer failure is reclaimed by permanent pump without Stop") {
    for (bool camera_kind : {false, true}) {
        Fixture fixture;
        retirement_host::RejectCleanupTask(true);
        std::atomic<size_t> callbacks{0};
        RODAK_CHECK(fixture.Start(camera_kind, [&](auto state) {
            if (state == ESP_PEER_STATE_CONNECT_FAILED) ++callbacks;
        }));
        const auto peer = host::LatestPeer();
        host::SetMainLoopResult(ESP_PEER_ERR_NO_MEM);
        host::RunPeerTasks();
        const bool reclaimed = Wait([&] {
            rodakos::PumpTaskRetirements();
            return retirement_host::Snapshot().task_deletes == 1;
        });
        RODAK_CHECK(reclaimed);
        RODAK_CHECK_EQ(callbacks.load(), 1u);
        RODAK_CHECK(host::IsClosed(peer));
        RODAK_CHECK_FALSE(fixture.Running(camera_kind));
        RODAK_CHECK_EQ(retirement_host::Snapshot().cleanup_create_attempts, 0u);
        RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
    }
}

RODAK_TEST("camera peer without an open channel closes at the connect deadline and allows retry") {
    // Reproduces the device case where DTLS returned ALLOC_FAILED and then retried with
    // unexpected-message errors: esp_peer kept main_loop healthy and never reported a
    // terminal state, so the session held the peer and camera indefinitely.
    Fixture fixture;
    retirement_host::RejectCleanupTask(true);
    std::atomic<size_t> failed{0};
    std::atomic<size_t> terminal{0};
    rodakos::WebRtcCameraService::Config config;
    // The host clock is real time plus an offset, so leave seconds of margin for test runtime.
    config.connect_timeout_ms = 10000;
    RODAK_CHECK(fixture.camera_peer.Start(config, [](auto, auto&&) {}, [&](auto state) {
        ++terminal;
        if (state == ESP_PEER_STATE_CONNECT_FAILED) ++failed;
    }));
    const auto peer = host::LatestPeer();
    host::RunPeerTasks();
    RODAK_CHECK(host::WaitForMainLoops(3));
    RODAK_CHECK(fixture.camera_peer.IsRunning());
    RODAK_CHECK_EQ(terminal.load(), 0u);
    host::AdvanceTimeUs(7000000);
    RODAK_CHECK(host::WaitForMainLoops(host::MainLoopCount() + 3));
    RODAK_CHECK(fixture.camera_peer.IsRunning());
    RODAK_CHECK_EQ(terminal.load(), 0u);
    host::AdvanceTimeUs(4000000);
    const bool reclaimed = Wait([&] {
        rodakos::PumpTaskRetirements();
        return retirement_host::Snapshot().task_deletes == 1;
    });
    RODAK_CHECK(reclaimed);
    RODAK_CHECK_EQ(failed.load(), 1u);
    RODAK_CHECK_EQ(terminal.load(), 1u);
    RODAK_CHECK(host::IsClosed(peer));
    RODAK_CHECK_FALSE(fixture.camera_peer.IsRunning());
    RODAK_CHECK_FALSE(fixture.camera.preview_running);
    RODAK_CHECK_FALSE(fixture.camera.jpeg_running);
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);

    RODAK_CHECK(fixture.camera_peer.Start(config, [](auto, auto&&) {}, {}));
    RODAK_CHECK(host::LatestPeer() != peer);
    fixture.camera_peer.Stop();
}

RODAK_TEST("camera peer with an open JPEG channel is not closed by the connect deadline") {
    Fixture fixture;
    std::atomic<size_t> terminal{0};
    rodakos::WebRtcCameraService::Config config;
    config.connect_timeout_ms = 10000;
    RODAK_CHECK(fixture.camera_peer.Start(config, [](auto, auto&&) {}, [&](auto) { ++terminal; }));
    const auto peer = host::LatestPeer();
    host::OpenCameraChannel(peer, 1);
    RODAK_CHECK(fixture.camera_peer.IsChannelOpen());
    host::RunPeerTasks();
    host::AdvanceTimeUs(60000000);
    RODAK_CHECK(host::WaitForMainLoops(host::MainLoopCount() + 5));
    RODAK_CHECK(fixture.camera_peer.IsRunning());
    RODAK_CHECK_FALSE(host::IsClosed(peer));
    RODAK_CHECK_EQ(terminal.load(), 0u);
    fixture.camera_peer.Stop();
    RODAK_CHECK_EQ(terminal.load(), 1u);
}

RODAK_TEST("camera peer rejects a zero connect deadline before touching media") {
    Fixture fixture;
    rodakos::WebRtcCameraService::Config config;
    config.connect_timeout_ms = 0;
    RODAK_CHECK_FALSE(fixture.camera_peer.Start(config, [](auto, auto&&) {}, {}));
    RODAK_CHECK_FALSE(fixture.camera.preview_running);
    RODAK_CHECK(host::LatestPeer() == nullptr);
}

RODAK_TEST("retirement peer concurrent Stop and pump delete one original task") {
    for (bool camera_kind : {false, true}) {
        Fixture fixture;
        retirement_host::Gate destructor;
        auto token = std::shared_ptr<int>(new int(1), [&](int* value) {
            delete value;
            destructor.Enter();
        });
        std::function<void(esp_peer_state_t)> callback = [token](auto) {};
        token.reset();
        RODAK_CHECK(fixture.Start(camera_kind, std::move(callback)));
        host::EmitState(host::LatestPeer(), ESP_PEER_STATE_DISCONNECTED);
        host::RunPeerTasks();
        const bool entered = destructor.Wait();
        std::atomic<size_t> joined{0};
        std::thread first([&] { fixture.Stop(camera_kind); ++joined; });
        std::thread second([&] { fixture.Stop(camera_kind); ++joined; });
        std::thread pump([&] {
            while (joined.load() != 2) {
                rodakos::PumpTaskRetirements();
                std::this_thread::sleep_for(1ms);
            }
        });
        destructor.Release();
        first.join();
        second.join();
        pump.join();
        host::JoinTasks();
        RODAK_CHECK(entered);
        RODAK_CHECK_EQ(joined.load(), 2u);
        RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 1u);
        RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 0u);
    }
}
