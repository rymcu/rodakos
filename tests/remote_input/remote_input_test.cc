#include "test_framework.h"
#include "phone_os/display_control_ack_tracker.h"
#include "phone_os/remote_input_controller.h"
#include "phone_os/touch_pointer_state.h"
#include "phone_ui/remote_text_input.h"

#include <deque>
#include <cstdlib>
#include <cstring>
#include <new>
#if defined(__linux__)
#include <pthread.h>
#endif
#include <lvgl.h>
#include <src/others/test/lv_test.h>

namespace {
thread_local bool reject_cpp_allocations = false;
#if defined(__linux__)
thread_local void (*after_mutex_unlock)(void*) = nullptr;
thread_local void* after_mutex_unlock_context = nullptr;
#endif
}
#if defined(__linux__)
extern "C" int __real_pthread_mutex_unlock(pthread_mutex_t* mutex);
extern "C" int __wrap_pthread_mutex_unlock(pthread_mutex_t* mutex) {
    const int result = __real_pthread_mutex_unlock(mutex);
    if (auto hook = after_mutex_unlock) {
        after_mutex_unlock = nullptr;
        hook(after_mutex_unlock_context);
    }
    return result;
}
#endif
void* operator new(size_t size) {
    if (reject_cpp_allocations) throw std::bad_alloc();
    if (auto* value = std::malloc(size == 0 ? 1 : size)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, size_t) noexcept { std::free(value); }

namespace {
using rodakos::StreamLease;
using rodakos::StreamLeasePtr;
using rodakos::RemoteInputController;
using rodakos::DisplayControlAckTracker;
constexpr const char* kEnable = R"({"version":1,"kind":"control","action":"enable"})";
constexpr const char* kDisable = R"({"version":1,"kind":"control","action":"disable"})";
constexpr const char* kText = R"({"version":1,"kind":"text","text":"hello"})";
constexpr const char* kHome = R"({"version":1,"kind":"shortcut","shortcut":"home"})";
constexpr const char* kDown = R"({"version":1,"kind":"pointer","action":"down","x":55,"y":60})";
constexpr const char* kUp = R"({"version":1,"kind":"pointer","action":"up","x":55,"y":60})";
struct CppAllocationFailure {
    CppAllocationFailure() { reject_cpp_allocations = true; }
    ~CppAllocationFailure() { reject_cpp_allocations = false; }
};

struct Fixture {
    std::deque<std::function<void()>> deferred;
    std::vector<std::string> navigations;
    std::vector<bool> replies;
    std::function<void()> during_navigation;
    lv_obj_t* textarea = nullptr;
    rodakos::TouchPointerState pointer;
    RemoteInputController controller{
        {[&](const std::string& kind, const std::string& value) {
            const auto result = rodakos::ApplyRemoteTextInput(kind, value);
            return rodakos::RemoteInputResult{result.accepted, result.reason};
        }, [&](const std::string& action) {
            navigations.push_back(action);
            controller.ResetForPageTransition();
            if (during_navigation) during_navigation();
            return true;
        }, [&](std::function<void()> action) {
            deferred.push_back(std::move(action));
            return true;
        }, []() {}}};

    Fixture() {
        lv_obj_clean(lv_screen_active());
        lv_obj_clean(lv_layer_top());
        textarea = lv_textarea_create(lv_screen_active());
        lv_textarea_set_text(textarea, "");
        lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    }
    StreamLeasePtr Lease(uint64_t nonce = 1) { return std::make_shared<StreamLease>(1, 1, nonce, "reused-session"); }
    void Input(const StreamLeasePtr& lease, const std::string& payload) {
        controller.Handle(lease, payload, [&](bool accepted, const char*) { replies.push_back(accepted); });
    }
    void RunNavigation() {
        RODAK_CHECK_FALSE(deferred.empty());
        auto action = std::move(deferred.front());
        deferred.pop_front();
        action();
    }
    lv_indev_data_t Read() {
        lv_indev_data_t result{};
        controller.ReadPointer([&](const rodakos::RemotePointerSample& sample) {
            pointer.Read(false, {0, 0}, sample.pressed,
                         {static_cast<lv_coord_t>(sample.x), static_cast<lv_coord_t>(sample.y)}, result,
                         sample.cancel_generation);
        });
        return result;
    }
    std::string Text() { return lv_textarea_get_text(textarea); }
};

struct PointerFixture {
    RemoteInputController controller{{}};
    StreamLeasePtr lease = std::make_shared<StreamLease>(1, 1, 1, "pointer-instance");
    rodakos::TouchPointerState pointer;
    lv_indev_t* indev = nullptr;
    lv_obj_t* remote_button = nullptr;
    lv_obj_t* local_button = nullptr;
    bool local_pressed = false;
    lv_point_t local_point{205, 55};
    int remote_clicks = 0;
    int local_clicks = 0;
    int remote_releases = 0;
    int resets = 0;
    std::vector<std::string> reasons;

    PointerFixture() {
        lv_obj_clean(lv_screen_active());
        lv_obj_clean(lv_layer_top());
        remote_button = lv_button_create(lv_screen_active());
        lv_obj_set_pos(remote_button, 10, 10);
        lv_obj_set_size(remote_button, 100, 100);
        local_button = lv_button_create(lv_screen_active());
        lv_obj_set_pos(local_button, 160, 10);
        lv_obj_set_size(local_button, 100, 100);
        const auto event = [](lv_event_t* value) {
            auto* self = static_cast<PointerFixture*>(lv_event_get_user_data(value));
            const bool remote = lv_event_get_target(value) == self->remote_button;
            const auto code = lv_event_get_code(value);
            if (code == LV_EVENT_CLICKED) {
                if (remote) ++self->remote_clicks;
                else ++self->local_clicks;
            }
            if (code == LV_EVENT_RELEASED && remote) ++self->remote_releases;
            if (code == LV_EVENT_INDEV_RESET) ++self->resets;
        };
        lv_obj_add_event_cb(remote_button, event, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(local_button, event, LV_EVENT_ALL, this);
        lv_obj_update_layout(lv_screen_active());
        indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(indev, this);
        lv_indev_set_read_cb(indev, [](lv_indev_t* device, lv_indev_data_t* data) {
            auto* self = static_cast<PointerFixture*>(lv_indev_get_user_data(device));
            self->controller.ProcessActions();
            bool cancelled = false;
            self->controller.ReadPointer([&](const rodakos::RemotePointerSample& sample) {
                const lv_point_t point{static_cast<lv_coord_t>(sample.x), static_cast<lv_coord_t>(sample.y)};
                if (self->pointer.Read(self->local_pressed, self->local_point, sample.pressed,
                                       point, *data, sample.cancel_generation)) {
                    lv_indev_reset(device, nullptr);
                    cancelled = true;
                }
            });
            data->continue_reading = cancelled;
        });
        Input(kEnable);
    }
    ~PointerFixture() {
        lv_indev_delete(indev);
        lv_obj_clean(lv_screen_active());
    }
    void Input(const char* payload) {
        controller.Handle(lease, payload, [&](bool accepted, const char* reason) {
            reasons.emplace_back(accepted ? "accepted" : reason ? reason : "rejected");
        });
    }
    void Read() { lv_indev_read(indev); }
    void PressWithQueuedUp() {
        Input(kDown);
        Input(kUp);
        Read();
        RODAK_CHECK_EQ(remote_clicks, 0);
        RODAK_CHECK(lv_obj_has_state(remote_button, LV_STATE_PRESSED));
    }
};
}

RODAK_TEST("remote input requires explicit enable before text and pointer") {
    Fixture f;
    auto lease = f.Lease();
    f.Input(lease, kText);
    f.Input(lease, kDown);
    f.controller.ProcessActions();
    RODAK_CHECK_EQ(f.Text(), "");
    RODAK_CHECK_EQ(f.Read().state, LV_INDEV_STATE_RELEASED);
    RODAK_CHECK_EQ(f.replies.size(), 2u);
    RODAK_CHECK_FALSE(f.replies[0]);
    RODAK_CHECK_FALSE(f.replies[1]);
}

RODAK_TEST("current grant executes real LVGL text shortcuts and ordered pointer") {
    Fixture f;
    auto lease = f.Lease();
    f.Input(lease, kEnable);
    f.Input(lease, kText);
    f.Input(lease, R"({"version":1,"kind":"shortcut","shortcut":"backspace"})");
    f.controller.ProcessActions();
    RODAK_CHECK_EQ(f.Text(), "hell");
    f.Input(lease, kDown);
    f.Input(lease, kUp);
    RODAK_CHECK_EQ(f.Read().state, LV_INDEV_STATE_PRESSED);
    RODAK_CHECK_EQ(f.Read().state, LV_INDEV_STATE_RELEASED);
    for (bool accepted : f.replies) RODAK_CHECK(accepted);
}

RODAK_TEST("revoked queued text and keys cannot use a replacement with same sessionId") {
    Fixture f;
    auto old = f.Lease();
    f.Input(old, kEnable);
    f.Input(old, kText);
    f.Input(old, R"({"version":1,"kind":"shortcut","shortcut":"enter"})");
    old->Revoke();
    auto next = f.Lease(2);
    f.Input(next, kEnable);
    f.controller.ProcessActions();
    RODAK_CHECK_EQ(f.Text(), "");
    f.Input(next, kText);
    f.controller.ProcessActions();
    RODAK_CHECK_EQ(f.Text(), "hello");
}

RODAK_TEST("disable and enable on the same stream create a new authorization grant") {
    Fixture f;
    auto lease = f.Lease();
    f.Input(lease, kEnable);
    f.Input(lease, kText);
    f.Input(lease, kDown);
    f.Input(lease, kDisable);
    f.Input(lease, kEnable);
    f.controller.ProcessActions();
    RODAK_CHECK_EQ(f.Text(), "");
    RODAK_CHECK_EQ(f.Read().state, LV_INDEV_STATE_RELEASED);
}

RODAK_TEST("deferred old navigation cannot clear or run a replacement navigation") {
    Fixture f;
    auto old = f.Lease();
    f.Input(old, kEnable);
    f.Input(old, kHome);
    f.controller.ProcessActions();
    old->Revoke();
    auto next = f.Lease(2);
    f.Input(next, kEnable);
    f.Input(next, kHome);
    f.controller.ProcessActions();
    f.Input(next, kText);
    f.RunNavigation();
    f.controller.ProcessActions();
    RODAK_CHECK(f.navigations.empty());
    RODAK_CHECK_EQ(f.Text(), "");
    f.RunNavigation();
    f.controller.ProcessActions();
    RODAK_CHECK_EQ(f.navigations.size(), 1u);
    RODAK_CHECK_EQ(f.Text(), "hello");
}

RODAK_TEST("same stream reenable cannot revive an already scheduled navigation") {
    Fixture f;
    auto lease = f.Lease();
    f.Input(lease, kEnable);
    f.Input(lease, kHome);
    f.controller.ProcessActions();
    f.Input(lease, kDisable);
    f.Input(lease, kEnable);
    f.RunNavigation();
    RODAK_CHECK(f.navigations.empty());
}

RODAK_TEST("old empty cleanup cannot release the replacement held pointer") {
    Fixture f;
    auto old = f.Lease();
    f.Input(old, kEnable);
    old->Revoke();
    auto next = f.Lease(2);
    f.Input(next, kEnable);
    f.Input(next, kDown);
    // Replacement first retires the old gesture, then admits its own down.
    RODAK_CHECK_EQ(f.Read().state, LV_INDEV_STATE_RELEASED);
    RODAK_CHECK_EQ(f.Read().state, LV_INDEV_STATE_PRESSED);
    f.Input(old, "");
    RODAK_CHECK(f.controller.IsEnabled());
    RODAK_CHECK_EQ(f.Read().state, LV_INDEV_STATE_PRESSED);
    f.Input(next, "");
    RODAK_CHECK_EQ(f.Read().state, LV_INDEV_STATE_RELEASED);
}

RODAK_TEST("revocation releases held pointer without waiting for teardown callback") {
    Fixture f;
    auto lease = f.Lease();
    f.Input(lease, kEnable);
    f.Input(lease, kDown);
    RODAK_CHECK_EQ(f.Read().state, LV_INDEV_STATE_PRESSED);
    lease->Revoke();
    RODAK_CHECK_FALSE(f.controller.IsEnabled());
    RODAK_CHECK_EQ(f.Read().state, LV_INDEV_STATE_RELEASED);
}

RODAK_TEST("revoked producer cannot reenable or disable the new owner") {
    Fixture f;
    auto old = f.Lease();
    old->Revoke();
    auto next = f.Lease(2);
    f.Input(next, kEnable);
    f.Input(old, kDisable);
    f.Input(old, kEnable);
    f.Input(next, kText);
    f.controller.ProcessActions();
    RODAK_CHECK_EQ(f.Text(), "hello");
}

RODAK_TEST("navigation may synchronously clean its owner without controller lock inversion") {
    Fixture f;
    auto lease = f.Lease();
    f.Input(lease, kEnable);
    f.during_navigation = [&]() { lease->Revoke(); f.Input(lease, ""); };
    f.Input(lease, kHome);
    f.controller.ProcessActions();
    f.RunNavigation();
    RODAK_CHECK_EQ(f.navigations.size(), 1u);
    RODAK_CHECK_FALSE(f.controller.IsEnabled());
}

RODAK_TEST("pointer boundaries precede reliable navigation and page reset preserves its tail") {
    Fixture f;
    auto lease = f.Lease();
    f.Input(lease, kEnable);
    f.Input(lease, kDown);
    f.Input(lease, kUp);
    f.Input(lease, kHome);
    f.controller.ProcessActions();
    RODAK_CHECK(f.deferred.empty());
    f.Read(); f.Read();
    f.controller.ProcessActions();
    f.Input(lease, kText);
    f.RunNavigation();
    f.controller.ProcessActions();
    RODAK_CHECK_EQ(f.Text(), "hello");
}

RODAK_TEST("page reset rejects cancelled input once outside controller lock") {
    Fixture f;
    auto lease = f.Lease();
    f.Input(lease, kEnable);
    std::vector<std::string> reasons;
    const auto reply = [&](bool accepted, const char* reason) {
        RODAK_CHECK_FALSE(accepted);
        reasons.emplace_back(reason);
        RODAK_CHECK(f.controller.IsEnabled());
        f.controller.ResetForPageTransition();
    };
    f.controller.Handle(lease, kText, reply);
    f.controller.Handle(lease, kDown, reply);
    f.controller.Handle(lease, kUp, reply);
    f.controller.ResetForPageTransition();
    RODAK_CHECK_EQ(reasons.size(), 3u);
    for (const auto& reason : reasons) RODAK_CHECK_EQ(reason, "page_transition");
    f.controller.ProcessActions();
    RODAK_CHECK_EQ(f.Read().state, LV_INDEV_STATE_RELEASED);
    RODAK_CHECK_EQ(f.Text(), "");
    RODAK_CHECK_EQ(reasons.size(), 3u);
}

RODAK_TEST("local touch completes cancelled pointer replies while preserving queued text") {
    Fixture f;
    auto lease = f.Lease();
    f.Input(lease, kEnable);
    std::vector<std::string> reasons;
    const auto reply = [&](bool accepted, const char* reason) {
        RODAK_CHECK_FALSE(accepted);
        reasons.emplace_back(reason);
        RODAK_CHECK(f.controller.IsEnabled());
        f.controller.OnLocalTouch();
    };
    f.Input(lease, kText);
    f.controller.Handle(lease, kDown, reply);
    f.controller.Handle(lease, kUp, reply);
    f.controller.OnLocalTouch();
    RODAK_CHECK_EQ(reasons.size(), 2u);
    for (const auto& reason : reasons) RODAK_CHECK_EQ(reason, "local_touch_active");
    f.controller.ProcessActions();
    RODAK_CHECK_EQ(f.Read().state, LV_INDEV_STATE_RELEASED);
    RODAK_CHECK_EQ(f.Text(), "hello");
}

RODAK_TEST("disable rejects pending navigation and its tail exactly once before reenable") {
    Fixture f;
    auto lease = f.Lease();
    f.Input(lease, kEnable);
    std::vector<std::string> reasons;
    const auto reply = [&](bool accepted, const char* reason) {
        RODAK_CHECK_FALSE(accepted);
        reasons.emplace_back(reason);
        RODAK_CHECK_FALSE(f.controller.IsEnabled());
    };
    f.controller.Handle(lease, kHome, reply);
    f.controller.ProcessActions();
    f.controller.Handle(lease, kText, reply);
    f.controller.Handle(lease, kDown, reply);
    f.controller.Handle(lease, kUp, reply);
    f.Input(lease, kDisable);
    RODAK_CHECK_EQ(reasons.size(), 4u);
    for (const auto& reason : reasons) RODAK_CHECK_EQ(reason, "control_disabled");
    f.Input(lease, kEnable);
    f.RunNavigation();
    f.controller.ProcessActions();
    RODAK_CHECK(f.navigations.empty());
    RODAK_CHECK_EQ(f.Text(), "");
    RODAK_CHECK_EQ(reasons.size(), 4u);
}

RODAK_TEST("navigation already admitted retains its result through synchronous disable") {
    Fixture f;
    auto lease = f.Lease();
    f.Input(lease, kEnable);
    f.during_navigation = [&]() { f.Input(lease, kDisable); };
    std::vector<bool> results;
    f.controller.Handle(lease, kHome, [&](bool accepted, const char*) { results.push_back(accepted); });
    f.controller.ProcessActions();
    f.RunNavigation();
    RODAK_CHECK_EQ(results.size(), 1u);
    RODAK_CHECK(results[0]);
    RODAK_CHECK_EQ(f.navigations.size(), 1u);
    RODAK_CHECK_FALSE(f.controller.IsEnabled());
}

RODAK_TEST("full-queue cancellation delivers all replies without heap allocation") {
    for (const auto mode : {0, 1, 2}) {
        Fixture f;
        auto lease = f.Lease();
        f.Input(lease, kEnable);
        size_t cancelled = 0;
        const char* expected = mode == 0 ? "page_transition" : mode == 1 ? "local_touch_active" : "control_disabled";
        const auto reply = [&](bool accepted, const char* reason) {
            RODAK_CHECK_FALSE(accepted);
            RODAK_CHECK(reason && std::strcmp(reason, expected) == 0);
            ++cancelled;
        };
        if (mode == 2) {
            f.controller.Handle(lease, kHome, reply);
            f.controller.ProcessActions();
        }
        if (mode != 1) for (int i = 0; i < 16; ++i) f.controller.Handle(lease, kText, reply);
        for (int i = 0; i < 32; ++i) f.controller.Handle(lease, i % 2 == 0 ? kDown : kUp, reply);
        const std::string disable = kDisable;
        {
            CppAllocationFailure failure;
            if (mode == 0) f.controller.ResetForPageTransition();
            else if (mode == 1) f.controller.OnLocalTouch();
            else f.controller.Handle(lease, disable, {});
        }
        RODAK_CHECK_EQ(cancelled, mode == 0 ? 48u : mode == 1 ? 32u : 49u);
        if (mode == 2) f.RunNavigation();
        f.controller.ProcessActions();
        RODAK_CHECK_EQ(f.Text(), "");
    }
}

RODAK_TEST("real LVGL normal remote up clicks exactly once without cancelling") {
    PointerFixture f;
    f.PressWithQueuedUp();
    f.Read();
    f.Read();
    RODAK_CHECK_EQ(f.remote_clicks, 1);
    RODAK_CHECK_EQ(f.remote_releases, 1);
    RODAK_CHECK_EQ(f.resets, 0);
    RODAK_CHECK_FALSE(lv_obj_has_state(f.remote_button, LV_STATE_PRESSED));
}

RODAK_TEST("real LVGL disable rejects queued up without synthesizing a click") {
    PointerFixture f;
    f.PressWithQueuedUp();
    f.Input(kDisable);
    f.Read();
    f.Read();
    RODAK_CHECK_EQ(f.remote_clicks, 0);
    RODAK_CHECK_EQ(f.remote_releases, 0);
    RODAK_CHECK_EQ(f.resets, 1);
    RODAK_CHECK_EQ(f.reasons[f.reasons.size() - 2], "control_disabled");
    RODAK_CHECK_FALSE(lv_obj_has_state(f.remote_button, LV_STATE_PRESSED));
    f.Input(kEnable);
    f.PressWithQueuedUp();
    f.Read();
    RODAK_CHECK_EQ(f.remote_clicks, 1);
}

RODAK_TEST("real LVGL stream revocation or cleanup cancels an admitted held pointer") {
    for (bool cleanup : {false, true}) {
        PointerFixture f;
        f.PressWithQueuedUp();
        f.lease->Revoke();
        if (cleanup) f.Input("");
        f.Read();
        RODAK_CHECK_EQ(f.remote_clicks, 0);
        RODAK_CHECK_EQ(f.remote_releases, 0);
        RODAK_CHECK_EQ(f.resets, 1);
        RODAK_CHECK_FALSE(lv_obj_has_state(f.remote_button, LV_STATE_PRESSED));
    }
}

RODAK_TEST("real LVGL page transition cancels a held remote gesture without an up click") {
    PointerFixture f;
    f.PressWithQueuedUp();
    f.controller.ResetForPageTransition();
    f.controller.ResetForPageTransition();
    f.Read();
    RODAK_CHECK_EQ(f.remote_clicks, 0);
    RODAK_CHECK_EQ(f.remote_releases, 0);
    RODAK_CHECK_EQ(f.resets, 1);
    RODAK_CHECK_EQ(f.reasons.back(), "page_transition");
    f.PressWithQueuedUp();
    f.Read();
    RODAK_CHECK_EQ(f.remote_clicks, 1);
}

RODAK_TEST("real LVGL local takeover cancels only remote gesture and keeps physical click") {
    PointerFixture f;
    f.PressWithQueuedUp();
    f.local_pressed = true;
    f.controller.OnLocalTouch();
    f.Read();
    for (int i = 0; i < 4; ++i) {
        f.controller.OnLocalTouch();
        f.Read();
    }
    f.local_pressed = false;
    f.Read();
    RODAK_CHECK_EQ(f.remote_clicks, 0);
    RODAK_CHECK_EQ(f.remote_releases, 0);
    RODAK_CHECK_EQ(f.local_clicks, 1);
    RODAK_CHECK_EQ(f.resets, 1);
    RODAK_CHECK_EQ(f.reasons.back(), "local_touch_active");
}

RODAK_TEST("real LVGL local publication before controller cancellation cannot inherit remote press") {
    PointerFixture f;
    f.PressWithQueuedUp();
    f.local_pressed = true;
    // Match TouchPollTask: publish the physical sample, then call OnLocalTouch.
    f.Read();
    f.controller.OnLocalTouch();
    f.Read();
    f.local_pressed = false;
    f.Read();
    RODAK_CHECK_EQ(f.remote_clicks, 0);
    RODAK_CHECK_EQ(f.remote_releases, 0);
    RODAK_CHECK_EQ(f.local_clicks, 1);
    RODAK_CHECK_EQ(f.resets, 1);
}

RODAK_TEST("real LVGL reenable before next read cannot complete the cancelled old gesture") {
    for (bool replacement : {false, true}) {
        PointerFixture f;
        f.PressWithQueuedUp();
        const auto old_lease = f.lease;
        f.Input(kDisable);
        if (replacement) f.lease = std::make_shared<StreamLease>(1, 2, 2, "pointer-instance");
        f.Input(kEnable);
        f.Read();
        RODAK_CHECK_EQ(f.remote_clicks, 0);
        f.PressWithQueuedUp();
        if (replacement) f.controller.Handle(old_lease, "", {});
        f.Read();
        RODAK_CHECK_EQ(f.remote_clicks, 1);
        RODAK_CHECK_EQ(f.resets, 1);
    }
}

RODAK_TEST("real LVGL a queued new press starts independently after cancellation") {
    PointerFixture f;
    f.PressWithQueuedUp();
    f.Input(kDisable);
    f.Input(kEnable);
    f.Input(kDown);
    f.Input(kUp);
    f.Read();
    RODAK_CHECK_EQ(f.remote_clicks, 0);
    RODAK_CHECK_EQ(f.resets, 1);
    f.Read();
    RODAK_CHECK_EQ(f.remote_clicks, 1);
    RODAK_CHECK_EQ(f.remote_releases, 1);
}

RODAK_TEST("real LVGL remote disable does not cancel a physical-only gesture") {
    PointerFixture f;
    f.local_pressed = true;
    f.Read();
    f.Input(kDown);
    f.Read();
    f.Input(kDisable);
    f.Read();
    f.local_pressed = false;
    f.Read();
    RODAK_CHECK_EQ(f.local_clicks, 1);
    RODAK_CHECK_EQ(f.remote_clicks, 0);
    RODAK_CHECK_EQ(f.resets, 0);
}

#if defined(__linux__)
RODAK_TEST("real LVGL cancellation between dequeue up and final admission cannot click") {
    for (bool revoke : {false, true}) {
        PointerFixture f;
        f.PressWithQueuedUp();
        struct Race {
            PointerFixture* fixture;
            bool revoke;
        } race{&f, revoke};
        // ProcessActions takes the first controller lock in the indev callback;
        // arm only for ReadPointer's dequeue unlock using a dedicated read cb.
        lv_indev_set_read_cb(f.indev, [](lv_indev_t* device, lv_indev_data_t* data) {
            auto* self = static_cast<PointerFixture*>(lv_indev_get_user_data(device));
            self->controller.ReadPointer([&](const rodakos::RemotePointerSample& sample) {
                const lv_point_t point{static_cast<lv_coord_t>(sample.x), static_cast<lv_coord_t>(sample.y)};
                if (self->pointer.Read(false, {0, 0}, sample.pressed, point, *data,
                                       sample.cancel_generation)) lv_indev_reset(device, nullptr);
            });
            data->continue_reading = false;
        });
        after_mutex_unlock_context = &race;
        after_mutex_unlock = [](void* context) {
            auto* current = static_cast<Race*>(context);
            if (current->revoke) current->fixture->lease->Revoke();
            else current->fixture->Input(kDisable);
        };
        f.Read();
        RODAK_CHECK_EQ(after_mutex_unlock, nullptr);
        after_mutex_unlock_context = nullptr;
        RODAK_CHECK_EQ(f.remote_clicks, 0);
        RODAK_CHECK_EQ(f.remote_releases, 0);
        RODAK_CHECK_EQ(f.resets, 1);
        RODAK_CHECK_EQ(f.reasons.back(), "stale_control_lease");
        RODAK_CHECK_FALSE(lv_obj_has_state(f.remote_button, LV_STATE_PRESSED));
    }
}
#endif

RODAK_TEST("late production ACK callback cannot reach a new peer or reuse its sequence") {
    auto tracker = std::make_shared<DisplayControlAckTracker>();
    tracker->Begin();
    auto old = tracker->Current();
    RODAK_CHECK(tracker->AdmitSequence(old, 9));
    auto reply = tracker->MakeReply(old, 9, "text");
    tracker->Close();
    tracker->Begin();
    auto next = tracker->Current();
    RODAK_CHECK_NE(old, next);
    RODAK_CHECK(tracker->AdmitSequence(next, 1));
    reply(true, nullptr);
    RODAK_CHECK(tracker->Take().empty());
    tracker->MakeReply(next, 1, "text")(false, nullptr);
    const auto pending = tracker->Take();
    RODAK_CHECK_EQ(pending.size(), 1u);
    RODAK_CHECK_EQ(pending.front().sequence, 1u);
    RODAK_CHECK_EQ(pending.front().reason, "text_target_unavailable");
}

RODAK_TEST("ACK taken before replacement fails the real final instance check") {
    auto tracker = std::make_shared<DisplayControlAckTracker>();
    tracker->Begin();
    auto old = tracker->Current();
    tracker->Queue(old, 1, true, nullptr);
    auto pending = tracker->Take();
    RODAK_CHECK_EQ(pending.size(), 1u);
    tracker->Begin();
    RODAK_CHECK_FALSE(tracker->IsCurrent(pending.front().instance));
    RODAK_CHECK_FALSE(tracker->AdmitSequence(old, 2));
    RODAK_CHECK(tracker->AdmitSequence(tracker->Current(), 1));
    RODAK_CHECK_FALSE(tracker->AdmitSequence(tracker->Current(), 1));
}

RODAK_TEST("production delayed ACK callback does not retain or access a destroyed service tracker") {
    auto tracker = std::make_shared<DisplayControlAckTracker>();
    tracker->Begin();
    auto reply = tracker->MakeReply(tracker->Current(), 1, "pointer");
    std::weak_ptr<DisplayControlAckTracker> weak = tracker;
    tracker.reset();
    RODAK_CHECK(weak.expired());
    reply(true, nullptr);
}

RODAK_TEST("destroyed input controller cancels deferred callbacks without touching a new controller") {
    std::function<void()> deferred;
    int navigations = 0;
    int replies = 0;
    auto lease = std::make_shared<StreamLease>(1, 1, 1, "same-session");
    {
        RemoteInputController old({{}, [&](const std::string&) { ++navigations; return true; },
            [&](std::function<void()> callback) { deferred = std::move(callback); return true; }, {}});
        old.Handle(lease, kEnable, {});
        old.Handle(lease, kHome, [&](bool, const char*) { ++replies; });
        old.ProcessActions();
    }
    Fixture next;
    auto next_lease = next.Lease(2);
    next.Input(next_lease, kEnable);
    next.Input(next_lease, kDown);
    RODAK_CHECK_EQ(next.Read().state, LV_INDEV_STATE_PRESSED);
    deferred();
    RODAK_CHECK_EQ(navigations, 0);
    RODAK_CHECK_EQ(replies, 0);
    RODAK_CHECK_EQ(next.Read().state, LV_INDEV_STATE_PRESSED);
}

int main() {
    lv_init();
    lv_test_display_create(320, 240);
    const int result = rodakos_test::RunAllTests();
    lv_deinit();
    return result;
}
