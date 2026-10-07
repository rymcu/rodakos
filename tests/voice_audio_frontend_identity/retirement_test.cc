#include "test_framework.h"
#include "host_runtime.h"
#include "task_retirement_host.h"

#include <atomic>
#include <array>
#include <chrono>
#include <thread>

#define private public
#include "phone_os/voice_audio_frontend.h"
#undef private

namespace {
using namespace std::chrono_literals;
using rodakos::VoiceAudioFrontend;
namespace frontend_host = rodakos_test::voice_frontend;

struct Fixture {
    rodakos::AudioCodecInput input;
    VoiceAudioFrontend frontend{input};
    Fixture() { frontend_host::Reset(); }
};

template <typename T>
bool WaitFor(T predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

TaskHandle_t observed_capture = nullptr;
retirement_host::Gate* delete_gate = nullptr;
std::atomic<bool> buffer_released_before_delete{false};
std::atomic<bool> capture_delete_observed{false};
void ObserveCaptureDelete(TaskHandle_t task) {
    if (task != observed_capture) return;
    buffer_released_before_delete = frontend_host::AfeFeedBufferReleased();
    capture_delete_observed = true;
    if (delete_gate) delete_gate->Enter();
}

#if !RODAK_FRONTEND_LEGACY_PROBE
VoiceAudioFrontend* publishing_frontend = nullptr;
std::atomic<bool> publication_wait_observed{false};
void ObservePublication(TaskHandle_t) {
    publication_wait_observed = publishing_frontend->task_ == nullptr &&
                                static_cast<bool>(publishing_frontend->capture_retirement_ticket_);
}
#endif
}

RODAK_TEST("Capture retirement frees the actual AFE vector before external task delete") {
    Fixture fixture;
    frontend_host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(WaitFor([] { return frontend_host::AfeFeedCount() > 0; }));
    RODAK_CHECK_NE(frontend_host::LastAfeFeedBuffer(), nullptr);
    RODAK_CHECK_FALSE(frontend_host::AfeFeedBufferReleased());
    observed_capture = fixture.frontend.task_;
    buffer_released_before_delete = false;
    capture_delete_observed = false;
    retirement_host::SetBeforeDeleteHook(ObserveCaptureDelete);
    fixture.frontend.Deinit();
    // The old body clears task_ before IDF aborts. Keep the test process alive
    // until that branch executes instead of racing into std::thread teardown.
    const bool deleted = WaitFor([] { return capture_delete_observed.load(); });
    retirement_host::SetBeforeDeleteHook(nullptr);
    observed_capture = nullptr;
    RODAK_CHECK(deleted);
    RODAK_CHECK(buffer_released_before_delete.load());
    RODAK_CHECK(frontend_host::AfeFeedBufferReleased());
    RODAK_CHECK_EQ(retirement_host::Snapshot().cleanup_create_attempts, 0U);
    // Only the intentionally persistent internal-stack notification worker remains.
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_tasks, 1U);
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_task_buffers, 2U);
}

#if !RODAK_FRONTEND_LEGACY_PROBE
RODAK_TEST("Capture Deinit waits for exact physical retirement under concurrent callers") {
    Fixture fixture;
    RODAK_CHECK(fixture.frontend.Init());
    observed_capture = fixture.frontend.task_;
    retirement_host::Gate gate;
    delete_gate = &gate;
    retirement_host::SetBeforeDeleteHook(ObserveCaptureDelete);
    std::atomic<bool> first_done{false}, second_done{false};
    std::thread first([&] { fixture.frontend.Deinit(); first_done = true; });
    const bool entered = gate.Wait();
    std::thread second([&] { fixture.frontend.Deinit(); second_done = true; });
    std::this_thread::sleep_for(15ms);
    const bool returned_before_delete = first_done || second_done;
    gate.Release();
    first.join();
    second.join();
    retirement_host::SetBeforeDeleteHook(nullptr);
    delete_gate = nullptr;
    observed_capture = nullptr;
    RODAK_CHECK(entered);
    RODAK_CHECK_FALSE(returned_before_delete);
    RODAK_CHECK(first_done && second_done);
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 1U);
    RODAK_CHECK(fixture.frontend.Init());
    fixture.frontend.Deinit();
    RODAK_CHECK_EQ(retirement_host::Snapshot().task_deletes, 2U);
}

RODAK_TEST("Capture publication gates a task scheduled before create returns") {
    Fixture fixture;
    publishing_frontend = &fixture.frontend;
    publication_wait_observed = false;
    retirement_host::SetBeforeCreateReturnsHook(ObservePublication);
    const bool initialized = fixture.frontend.Init();
    retirement_host::SetBeforeCreateReturnsHook(nullptr);
    publishing_frontend = nullptr;
    RODAK_CHECK(initialized);
    RODAK_CHECK(publication_wait_observed.load());
    fixture.frontend.Deinit();
    RODAK_CHECK_EQ(retirement_host::Snapshot().cleanup_create_attempts, 0U);
}

RODAK_TEST("Capture creation failure retains the previous retired ticket and allows retry") {
    Fixture fixture;
    RODAK_CHECK(fixture.frontend.Init());
    fixture.frontend.Deinit();
    const auto previous = fixture.frontend.capture_retirement_ticket_;
    RODAK_CHECK(previous);
    retirement_host::SetCreationAllowed(false);
    RODAK_CHECK_FALSE(fixture.frontend.Init());
    RODAK_CHECK(fixture.frontend.capture_retirement_ticket_);
    RODAK_CHECK_EQ(fixture.frontend.capture_retirement_ticket_.slot_, previous.slot_);
    RODAK_CHECK_EQ(fixture.frontend.capture_retirement_ticket_.generation_, previous.generation_);
    RODAK_CHECK_EQ(fixture.frontend.task_, nullptr);
    previous.Join();
    retirement_host::SetCreationAllowed(true);
    RODAK_CHECK(fixture.frontend.Init());
    fixture.frontend.Deinit();
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_tasks, 1U);
    RODAK_CHECK_EQ(retirement_host::Snapshot().cleanup_create_attempts, 0U);
}

RODAK_TEST("Capture creation failure deletes a newly blocked notification worker") {
    Fixture fixture;
    rodakos::TaskRetirementOwner capacity_owner;
    std::array<rodakos::TaskRetirementTicket, rodakos::kTaskRetirementSlots> occupied;
    for (auto& ticket : occupied) {
        ticket = rodakos::ReserveTaskRetirement(capacity_owner, [](void*) {}, nullptr);
    }
    const bool initialized = fixture.frontend.Init();
    for (const auto& ticket : occupied) rodakos::CancelTaskRetirement(ticket);
    RODAK_CHECK_FALSE(initialized);
    RODAK_CHECK_EQ(retirement_host::Snapshot().dynamic_tasks_created, 1U);
    RODAK_CHECK_EQ(retirement_host::Snapshot().live_tasks, 0U);
}

RODAK_TEST("Notification callback can reenter Deinit then capture can initialize again") {
    Fixture fixture;
    RODAK_CHECK(fixture.frontend.Init());
    std::atomic<bool> callback_returned{false};
    RODAK_CHECK(fixture.frontend.QueueDiagnosticCommand([&] {
        fixture.frontend.Deinit();
        callback_returned = true;
    }));
    RODAK_CHECK(WaitFor([&] { return callback_returned.load(); }));
    RODAK_CHECK(fixture.frontend.Init());
    fixture.frontend.Deinit();
    RODAK_CHECK_EQ(retirement_host::Snapshot().cleanup_create_attempts, 0U);
}

RODAK_TEST("External Deinit releases lifecycle before waiting for a reentrant notification") {
    Fixture fixture;
    RODAK_CHECK(fixture.frontend.Init());
    retirement_host::Gate callback_gate;
    std::atomic<bool> callback_returned{false}, deinit_returned{false};
    RODAK_CHECK(fixture.frontend.QueueDiagnosticCommand([&] {
        callback_gate.Enter();
        fixture.frontend.Stop();
        callback_returned = true;
    }));
    const bool callback_entered = callback_gate.Wait();
    std::thread stopper([&] { fixture.frontend.Deinit(); deinit_returned = true; });
    const bool released_model = WaitFor([&] {
        xSemaphoreTake(fixture.frontend.mutex_, portMAX_DELAY);
        const bool stopped = !fixture.frontend.initialized_;
        xSemaphoreGive(fixture.frontend.mutex_);
        return stopped;
    });
    const bool returned_before_callback = deinit_returned;
    callback_gate.Release();
    stopper.join();
    RODAK_CHECK(callback_entered);
    RODAK_CHECK(released_model);
    RODAK_CHECK_FALSE(returned_before_callback);
    RODAK_CHECK(callback_returned && deinit_returned);
    RODAK_CHECK_EQ(retirement_host::Snapshot().cleanup_create_attempts, 0U);
}
#endif
