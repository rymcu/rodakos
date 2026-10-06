#include "test_framework.h"
#include "phone_os/display_control_ack_tracker.h"
#include "phone_os/remote_input_controller.h"
#include "phone_os/touch_pointer_state.h"
#include "phone_ui/remote_text_input.h"

#include <deque>
#include <lvgl.h>
#include <src/others/test/lv_test.h>

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
                         {static_cast<lv_coord_t>(sample.x), static_cast<lv_coord_t>(sample.y)}, result);
        });
        return result;
    }
    std::string Text() { return lv_textarea_get_text(textarea); }
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
