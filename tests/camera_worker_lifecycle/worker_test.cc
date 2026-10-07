#include <chrono>
#include <future>
#include <thread>

#include "host_runtime.h"
#include "test_framework.h"

namespace {
namespace host = worker_host;
struct Fixture {
    esp_cam_ctlr_handle_t controller = nullptr;
    Fixture() { host::Reset(); }
    ~Fixture() {
        host::ReleaseLog();
        host::ReleaseReceive();
        host::ReleaseAfterReceive();
        host::ReleaseCallback();
        host::ReleaseFinalUnlock();
        host::ReleaseCaptureStart();
        if (controller) worker_delete(controller);
        host::Join();
        host::Reset();
    }
    void Create() { RODAK_CHECK_EQ(worker_create(&controller), ESP_OK); }
    int Delete() {
        auto* value = controller;
        controller = nullptr;
        return worker_delete(value);
    }
};
}  // namespace

RODAK_TEST("worker constructor uses PSRAM only with original stack priority queue and ring") {
    Fixture fixture;
    fixture.Create();
    auto* ctlr = static_cast<dvp_cam_ctlr_t*>(fixture.controller);
    RODAK_CHECK_EQ(host::CreatedStackCaps(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    RODAK_CHECK_EQ(host::CreatedStackBytes(), 3072u);
    RODAK_CHECK_EQ(host::CreatedPriority(), 23u);
    RODAK_CHECK_EQ(ctlr->dma_buffer_hsize, 3840u);
    RODAK_CHECK_EQ(ctlr->dma_buffer_size, 7680u);
    RODAK_CHECK_EQ(ctlr->dma_desc_hcnt, 1u);
    RODAK_CHECK_EQ(fixture.Delete(), ESP_OK);
    host::Join();
    RODAK_CHECK_EQ(host::DeleteCalls(), 1u);
    RODAK_CHECK_EQ(host::WithCapsDeleteCalls(), 1u);
}

RODAK_TEST("worker constructor allocation failures release every owned object") {
    for (int index = 1; index <= 3; ++index) {
        Fixture fixture;
        host::FailAllocation(index);
        RODAK_CHECK_EQ(worker_create(&fixture.controller), ESP_ERR_NO_MEM);
        RODAK_CHECK_EQ(fixture.controller, nullptr);
        RODAK_CHECK_EQ(host::LiveAllocations(), 0u);
        RODAK_CHECK_EQ(host::LiveQueues(), 0u);
        RODAK_CHECK_EQ(host::DeleteCalls(), 0u);
    }
}

RODAK_TEST("worker constructor queue and WithCaps failures do not start cleanup tasks") {
    for (int failure = 0; failure < 3; ++failure) {
        Fixture fixture;
        if (failure == 0) host::FailQueueCreation(true);
        if (failure == 1) host::FailTaskStack(true);
        if (failure == 2) host::FailTaskTcb(true);
        RODAK_CHECK_EQ(worker_create(&fixture.controller), ESP_ERR_NO_MEM);
        RODAK_CHECK_EQ(fixture.controller, nullptr);
        RODAK_CHECK_EQ(host::LiveAllocations(), 0u);
        RODAK_CHECK_EQ(host::LiveQueues(), 0u);
        RODAK_CHECK_EQ(host::DeleteCalls(), 0u);
    }
}

RODAK_TEST("worker shutdown wakes an empty queue before receive") {
    Fixture fixture;
    host::BlockReceive();
    fixture.Create();
    RODAK_CHECK(host::WaitReceive());
    auto deletion = std::async(std::launch::async, [&] { return fixture.Delete(); });
    const bool owner_progressed = host::WaitOwnerProgress();
    const bool resources_retained = host::DeleteCalls() == 0 && host::LiveAllocations() == 3;
    host::ReleaseReceive();
    RODAK_CHECK_EQ(deletion.get(), ESP_OK);
    host::Join();
    RODAK_CHECK(owner_progressed);
    RODAK_CHECK(resources_retained);
    RODAK_CHECK_EQ(host::CallbackCalls(), 0u);
}

RODAK_TEST("worker shutdown handles a full existing event queue without blocking send") {
    Fixture fixture;
    host::BlockReceive();
    fixture.Create();
    RODAK_CHECK(host::WaitReceive());
    for (int i = 0; i < 3; ++i) host::QueueEvent(fixture.controller, DVP_CAM_EVENT_SYNC_END);
    auto deletion = std::async(std::launch::async, [&] { return fixture.Delete(); });
    const bool owner_progressed = host::WaitOwnerProgress();
    host::ReleaseReceive();
    RODAK_CHECK_EQ(deletion.get(), ESP_OK);
    host::Join();
    RODAK_CHECK(owner_progressed);
    RODAK_CHECK_EQ(host::QueueFullWakeups(), 1u);
    RODAK_CHECK_EQ(host::CallbackCalls(), 0u);
}

RODAK_TEST("worker shutdown rechecks after a queued event has already been received") {
    Fixture fixture;
    host::BlockAfterReceive();
    fixture.Create();
    auto* ctlr = static_cast<dvp_cam_ctlr_t*>(fixture.controller);
    ctlr->cbs.on_get_new_trans = host::Callback;
    ctlr->dvp_fsm = DVP_CAM_FSM_STARTED;
    host::QueueEvent(fixture.controller, DVP_CAM_EVENT_SYNC_END);
    RODAK_CHECK(host::WaitAfterReceive());
    auto deletion = std::async(std::launch::async, [&] { return fixture.Delete(); });
    const bool owner_progressed = host::WaitOwnerProgress();
    host::ReleaseAfterReceive();
    RODAK_CHECK_EQ(deletion.get(), ESP_OK);
    host::Join();
    RODAK_CHECK(owner_progressed);
    RODAK_CHECK_EQ(host::CallbackCalls(), 0u);
}

RODAK_TEST("worker shutdown permits an admitted callback to finish before deletion") {
    Fixture fixture;
    host::BlockCallback();
    fixture.Create();
    auto* ctlr = static_cast<dvp_cam_ctlr_t*>(fixture.controller);
    ctlr->cbs.on_get_new_trans = host::Callback;
    ctlr->dvp_fsm = DVP_CAM_FSM_STARTED;
    host::QueueEvent(fixture.controller, DVP_CAM_EVENT_SYNC_END);
    RODAK_CHECK(host::WaitCallback());
    auto deletion = std::async(std::launch::async, [&] { return fixture.Delete(); });
    // The real callback holds the controller lock, so the owner cannot even
    // publish shutdown until it returns. Release explicitly, never by timeout.
    const bool resources_retained = host::DeleteCalls() == 0 && host::LiveAllocations() == 3;
    host::ReleaseCallback();
    RODAK_CHECK_EQ(deletion.get(), ESP_OK);
    host::Join();
    RODAK_CHECK(resources_retained);
    RODAK_CHECK_FALSE(host::DeletedDuringCallback());
    RODAK_CHECK_EQ(host::CallbackCalls(), 1u);
}

RODAK_TEST("worker final access confirmation includes releasing its controller lock") {
    Fixture fixture;
    host::BlockFinalUnlock();
    fixture.Create();
    auto deletion = std::async(std::launch::async, [&] { return fixture.Delete(); });
    const bool reached = host::WaitFinalUnlock();
    const bool owner_observed = host::WaitOwnerAtFinalUnlock();
    const bool retained = host::DeleteCalls() == 0 && host::LiveAllocations() == 3;
    host::ReleaseFinalUnlock();
    RODAK_CHECK_EQ(deletion.get(), ESP_OK);
    host::Join();
    RODAK_CHECK(reached);
    RODAK_CHECK(owner_observed);
    RODAK_CHECK(retained);
    RODAK_CHECK_FALSE(host::DeletedBeforeFinalUnlock());
}

RODAK_TEST("worker callback self deletion is rejected before taking its held lock") {
    Fixture fixture;
    host::DeleteFromCallback(true);
    fixture.Create();
    auto* ctlr = static_cast<dvp_cam_ctlr_t*>(fixture.controller);
    ctlr->cbs.on_get_new_trans = host::Callback;
    ctlr->dvp_fsm = DVP_CAM_FSM_STARTED;
    host::QueueEvent(fixture.controller, DVP_CAM_EVENT_SYNC_END);
    RODAK_CHECK(host::WaitCallback());
    for (int i = 0; i < 2000 && host::CallbackDeleteResult() == -999; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    RODAK_CHECK_EQ(host::CallbackDeleteResult(), ESP_ERR_INVALID_STATE);
    RODAK_CHECK_EQ(host::DeleteCalls(), 0u);
    RODAK_CHECK_EQ(fixture.Delete(), ESP_OK);
    host::Join();
    RODAK_CHECK_EQ(host::WithCapsDeleteCalls(), 1u);
}

RODAK_TEST("worker rejects absent owner handle and supports distinct repeated lifecycles") {
    RODAK_CHECK_EQ(worker_delete(nullptr), ESP_ERR_INVALID_ARG);
    for (int i = 0; i < 3; ++i) {
        Fixture fixture;
        fixture.Create();
        RODAK_CHECK_EQ(fixture.Delete(), ESP_OK);
        host::Join();
        RODAK_CHECK_EQ(host::LiveAllocations(), 0u);
        RODAK_CHECK_EQ(host::LiveQueues(), 0u);
    }
}

RODAK_TEST("worker may run before the creator publishes its handle") {
    Fixture fixture;
    host::RunBeforeHandlePublished(true);
    fixture.Create();
    RODAK_CHECK(host::WaitReceive());
    RODAK_CHECK_EQ(fixture.Delete(), ESP_OK);
    host::Join();
    RODAK_CHECK_EQ(host::WithCapsDeleteCalls(), 1u);
    RODAK_CHECK_EQ(host::LiveAllocations(), 0u);
}

RODAK_TEST("worker shutdown lets an admitted capture start return before deletion") {
    Fixture fixture;
    host::ProvideFrame(true);
    host::BlockCaptureStart();
    fixture.Create();
    auto* ctlr = static_cast<dvp_cam_ctlr_t*>(fixture.controller);
    ctlr->cbs.on_get_new_trans = host::Callback;
    ctlr->dvp_fsm = DVP_CAM_FSM_STARTED;
    host::QueueEvent(fixture.controller, DVP_CAM_EVENT_SYNC_END);
    RODAK_CHECK(host::WaitCaptureStart());
    auto deletion = std::async(std::launch::async, [&] { return fixture.Delete(); });
    const bool owner_progressed = host::WaitOwnerProgress();
    const bool retained = host::DeleteCalls() == 0 && host::LiveAllocations() == 3;
    host::ReleaseCaptureStart();
    RODAK_CHECK_EQ(deletion.get(), ESP_OK);
    host::Join();
    RODAK_CHECK(owner_progressed);
    RODAK_CHECK(retained);
    RODAK_CHECK_FALSE(host::DeletedDuringCaptureStart());
}

RODAK_TEST("worker deletion waits for active log and releases resources after quiescence") {
    Fixture fixture;
    host::BlockLog();
    fixture.Create();
    host::QueueEvent(fixture.controller, DVP_CAM_EVENT_SYNC_END);
    RODAK_CHECK(host::WaitLog());
    auto deletion = std::async(std::launch::async, [&] { return fixture.Delete(); });
    const bool owner_progressed = host::WaitOwnerProgress();
    host::ReleaseLog();
    RODAK_CHECK_EQ(deletion.get(), ESP_OK);
    host::Join();
    RODAK_CHECK(owner_progressed);
    RODAK_CHECK_FALSE(host::DeletedWhileLogHeld());
    RODAK_CHECK_EQ(host::LiveAllocations(), 0u);
    RODAK_CHECK_EQ(host::LiveQueues(), 0u);
}
