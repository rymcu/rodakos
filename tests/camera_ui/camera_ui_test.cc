#include "test_framework.h"
#include "phone_os/camera_service.h"
#include "phone_os/audio_focus_service.h"
#include "phone_os/phone_app.h"
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#define private public
#include "apps/camera/camera_app.h"
#undef private
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_host.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_ui.h"
#include "esp_lvgl_port.h"
#include "settings.h"
#include <src/others/test/lv_test.h>

namespace camera_test {
std::atomic<bool> reject_async{false};
std::atomic<int> worker_async_attempts{0};
std::atomic<int> worker_ui_writes{0};
int timer_creations = 0;
int fail_timer_creation = 0;
}
extern "C" {
lv_result_t __real_lv_async_call(lv_async_cb_t, void*);
lv_result_t __wrap_lv_async_call(lv_async_cb_t callback, void* data) {
    if (camera_test::capture_worker) ++camera_test::worker_async_attempts;
    if (camera_test::reject_async) return LV_RESULT_INVALID;
    return __real_lv_async_call(callback, data);
}
void __real_lv_label_set_text(lv_obj_t*, const char*);
void __wrap_lv_label_set_text(lv_obj_t* label, const char* text) {
    if (camera_test::capture_worker) ++camera_test::worker_ui_writes;
    __real_lv_label_set_text(label, text);
}
lv_timer_t* __real_lv_timer_create(lv_timer_cb_t, uint32_t, void*);
lv_timer_t* __wrap_lv_timer_create(lv_timer_cb_t callback, uint32_t period, void* data) {
    const int call = ++camera_test::timer_creations;
    if (camera_test::fail_timer_creation == call) return nullptr;
    return __real_lv_timer_create(callback, period, data);
}
}
namespace {
void Pump(uint32_t milliseconds = 130) {
    lv_test_wait(milliseconds);
    lv_obj_update_layout(lv_screen_active());
}
void Await(const std::function<bool()>& condition) {
    for (int i = 0; i < 300; ++i) {
        if (condition()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    RODAK_CHECK(condition());
}
void SaveScreenshot(const char* name) {
    Pump(10);
    const auto* buffer = lv_display_get_buf_active(lv_display_get_default());
    RODAK_CHECK_EQ(buffer->header.cf, LV_COLOR_FORMAT_XRGB8888);
    std::ofstream out(name, std::ios::binary);
    out << "P6\n320 240\n255\n";
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            const auto* pixel = buffer->data + y * buffer->header.stride + x * 4;
            const char rgb[] = {static_cast<char>(pixel[2]), static_cast<char>(pixel[1]),
                                static_cast<char>(pixel[0])};
            out.write(rgb, 3);
        }
    }
}
struct Fixture {
    rodakos::CameraService camera;
    rodakos::AudioFocusService focus;
    PhoneUi ui{320, 240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui, navigation, registry, services, settings};
    PhoneAppHost host;
    PhoneAppDescriptor descriptor;
    Fixture() {
        ui.SetThemeName("dark");
        camera_test::fail_task_creation = false;
        camera_test::deny_worker_lock = true;
        camera_test::worker_lock_attempts = 0;
        camera_test::worker_async_attempts = 0;
        camera_test::worker_ui_writes = 0;
        camera_test::reject_async = false;
        camera_test::fail_next_ui_lock = false;
        camera_test::timer_creations = 0;
        camera_test::fail_timer_creation = 0;
        services.SetCamera(&camera);
        services.SetAudioFocus(&focus);
        descriptor.id = "camera";
        descriptor.title = "Camera";
        descriptor.create = [] { return std::make_unique<CameraApp>(); };
    }
    ~Fixture() {
        camera.ReleaseGates();
        camera_test::JoinTasks();
        host.CloseCurrent();
        Pump(2000);
        lv_obj_clean(lv_screen_active());
        lv_obj_clean(lv_layer_top());
        camera_test::reject_async = false;
        camera_test::fail_timer_creation = 0;
    }
    CameraApp& app() { return *static_cast<CameraApp*>(host.current_app()); }
    void Create() {
        RODAK_CHECK(host.Launch(descriptor, context));
        Pump(180);
    }
    std::string Status() { return lv_label_get_text(app().status_label_); }
    bool Disabled() { return lv_obj_has_state(app().capture_button_, LV_STATE_DISABLED); }
    void Click() { lv_obj_send_event(app().capture_button_, LV_EVENT_CLICKED, nullptr); }
    void Complete() {
        camera_test::JoinTasks();
        Pump(140);
        RODAK_CHECK_FALSE(app().CaptureInFlight());
    }
};
}

RODAK_TEST("capture completion does not need a worker LVGL lock or async allocation") {
    Fixture f;
    f.Create();
    RODAK_CHECK_FALSE(f.Disabled());
    camera_test::reject_async = true;
    f.Click();
    RODAK_CHECK(f.Disabled());
    f.Complete();
    RODAK_CHECK_EQ(f.Status(), "/photos/test.jpg");
    RODAK_CHECK_FALSE(f.Disabled());
    RODAK_CHECK_EQ(camera_test::worker_lock_attempts.load(), 0);
    RODAK_CHECK_EQ(camera_test::worker_async_attempts.load(), 0);
    RODAK_CHECK_EQ(camera_test::worker_ui_writes.load(), 0);
    RODAK_CHECK_EQ(f.focus.requests, 1);
    RODAK_CHECK_EQ(f.focus.last_request.owner, "camera");
    RODAK_CHECK_EQ(f.focus.last_request.gain, rodakos::AudioFocusGain::kExclusive);
    RODAK_CHECK_FALSE(f.focus.last_request.resume_on_release);
    RODAK_CHECK(f.focus.last_request.release_playback_hardware);
}

RODAK_TEST("storage failure is shown and a second click can save successfully") {
    Fixture f;
    f.camera.Outcomes({{false, "", "SD card is not available", {}}, {true, "/photos/retry.jpg", "", {}}});
    f.Create();
    f.Click();
    f.Complete();
    RODAK_CHECK_EQ(f.Status(), "SD card is not available");
    RODAK_CHECK_FALSE(f.Disabled());
    Pump(2000);
    SaveScreenshot("camera-storage-error.ppm");
    f.Click();
    f.Complete();
    RODAK_CHECK_EQ(f.Status(), "/photos/retry.jpg");
    RODAK_CHECK_EQ(f.camera.captures.load(), 2U);
}

RODAK_TEST("slow capture accepts only one task and retains its result until UI polling") {
    Fixture f;
    auto gate = std::make_shared<camera_test::Gate>();
    f.camera.Outcomes({{true, "/photos/slow.jpg", "", gate}});
    f.Create();
    f.Click();
    RODAK_CHECK(gate->Wait());
    f.app().CapturePhoto();
    RODAK_CHECK_EQ(f.camera.captures.load(), 1U);
    gate->Release();
    camera_test::JoinTasks();
    RODAK_CHECK(f.app().CaptureInFlight());
    RODAK_CHECK(f.Disabled());
    RODAK_CHECK_EQ(f.Status(), "Saving photo...");
    Pump(140);
    RODAK_CHECK_EQ(f.Status(), "/photos/slow.jpg");
    RODAK_CHECK_FALSE(f.Disabled());
    lv_label_set_text(f.app().status_label_, "Keep current message");
    Pump(500);
    RODAK_CHECK_EQ(f.Status(), "Keep current message");
}

RODAK_TEST("task creation failure leaves the button retryable") {
    Fixture f;
    f.Create();
    camera_test::fail_task_creation = true;
    f.Click();
    RODAK_CHECK_FALSE(f.app().CaptureInFlight());
    RODAK_CHECK_FALSE(f.Disabled());
    RODAK_CHECK_EQ(f.Status(), "Failed to start capture task");
    camera_test::fail_task_creation = false;
    f.Click();
    f.Complete();
    RODAK_CHECK_EQ(f.Status(), "/photos/test.jpg");
}

RODAK_TEST("preview failure does not remove the separate capture result consumer") {
    Fixture f;
    auto gate = std::make_shared<camera_test::Gate>();
    f.camera.Outcomes({{false, "", "SD write failed", gate}});
    f.Create();
    f.Click();
    RODAK_CHECK(gate->Wait());
    f.camera.running = false;
    f.camera.preview_error = "Camera disconnected";
    f.app().UpdatePreview();
    RODAK_CHECK(f.app().preview_timer_ == nullptr);
    RODAK_CHECK(f.app().capture_result_timer_ != nullptr);
    gate->Release();
    f.Complete();
    RODAK_CHECK_EQ(f.Status(), "SD write failed");
    RODAK_CHECK(f.Disabled());
}

RODAK_TEST("theme recreation rejects the old worker result and preserves the new instance") {
    Fixture f;
    auto gate = std::make_shared<camera_test::Gate>();
    f.camera.Outcomes({{true, "/photos/old-theme.jpg", "", gate},
                       {true, "/photos/new-theme.jpg", "", {}}});
    f.Create();
    f.Click();
    RODAK_CHECK(gate->Wait());
    std::weak_ptr<CameraCaptureGuard> old_guard = f.app().capture_guard_;
    f.ui.SetThemeName("light");
    RODAK_CHECK_FALSE(f.host.RefreshCurrentTheme(f.context));
    RODAK_CHECK(f.host.RecreateCurrent(f.descriptor, f.context));
    Pump(180);
    RODAK_CHECK_EQ(f.Status(), "Ready");
    RODAK_CHECK_FALSE(f.Disabled());
    f.Click();
    Await([&] { return f.camera.completed.load() == 1; });
    for (int i = 0; i < 20 && f.app().CaptureInFlight(); ++i) {
        Pump(140);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    RODAK_CHECK_EQ(f.Status(), "/photos/new-theme.jpg");
    gate->Release();
    f.Complete();
    RODAK_CHECK(old_guard.expired());
    RODAK_CHECK_EQ(f.Status(), "/photos/new-theme.jpg");
    RODAK_CHECK_FALSE(f.Disabled());
}

RODAK_TEST("destroyed app is not retained or called by its late worker") {
    Fixture f;
    auto gate = std::make_shared<camera_test::Gate>();
    f.camera.Outcomes({{false, "", "Late SD failure", gate}});
    f.Create();
    f.Click();
    RODAK_CHECK(gate->Wait());
    std::weak_ptr<CameraCaptureGuard> guard = f.app().capture_guard_;
    f.host.CloseCurrent();
    RODAK_CHECK_EQ(f.focus.releases, 1);
    RODAK_CHECK_FALSE(guard.expired());
    gate->Release();
    camera_test::JoinTasks();
    RODAK_CHECK(guard.expired());
    Pump(300);
    RODAK_CHECK(f.host.current_app() == nullptr);
    RODAK_CHECK_EQ(lv_obj_get_child_count(lv_screen_active()), 0U);
}

RODAK_TEST("stale generation cannot finish a later capture in the same app") {
    Fixture f;
    auto gate = std::make_shared<camera_test::Gate>();
    f.camera.Outcomes({{true, "/photos/first.jpg", "", {}},
                       {true, "/photos/second.jpg", "", gate}});
    f.Create();
    f.Click();
    f.Complete();
    f.Click();
    RODAK_CHECK(gate->Wait());
    f.app().OnCaptureComplete(true, "/photos/stale.jpg", "", 1);
    RODAK_CHECK_EQ(f.Status(), "Saving photo...");
    RODAK_CHECK(f.Disabled());
    gate->Release();
    f.Complete();
    RODAK_CHECK_EQ(f.Status(), "/photos/second.jpg");
}

RODAK_TEST("result timer allocation failure fails creation and cleans the partial UI") {
    Fixture f;
    camera_test::fail_timer_creation = 1;
    RODAK_CHECK_FALSE(f.host.Launch(f.descriptor, f.context));
    RODAK_CHECK(f.host.current_app() == nullptr);
    RODAK_CHECK_EQ(lv_obj_get_child_count(lv_screen_active()), 0U);
    camera_test::fail_timer_creation = 0;
    f.Create();
    f.Click();
    f.Complete();
    RODAK_CHECK_FALSE(f.Disabled());
}

RODAK_TEST("preview start timer allocation failure cleans the already-created result timer") {
    Fixture f;
    camera_test::fail_timer_creation = 2;
    RODAK_CHECK_FALSE(f.host.Launch(f.descriptor, f.context));
    Pump(500);
    RODAK_CHECK_EQ(lv_obj_get_child_count(lv_screen_active()), 0U);
}

RODAK_TEST("unavailable camera cannot start a capture and releases the audio focus") {
    Fixture f;
    f.camera.start_ok = false;
    f.Create();
    RODAK_CHECK(f.Disabled());
    RODAK_CHECK_EQ(f.focus.releases, 1);
    f.app().CapturePhoto();
    RODAK_CHECK_EQ(f.camera.captures.load(), 0U);
    Pump(2000);
    SaveScreenshot("camera-unavailable.ppm");
}

RODAK_TEST("long saved paths stay on one status line below the capture button") {
    Fixture f;
    f.ui.SetThemeName("light");
    f.camera.Outcomes({{true, "/photos/a-very-long-folder-name/camera-capture-with-a-very-long-name-20261006.jpg", "", {}}});
    f.Create();
    f.Click();
    f.Complete();
    Pump(2000);
    lv_area_t status{}, button{};
    lv_obj_get_coords(f.app().status_label_, &status);
    lv_obj_get_coords(f.app().capture_button_, &button);
    RODAK_CHECK(status.y1 > button.y2);
    RODAK_CHECK(status.y2 < 240);
    RODAK_CHECK_EQ(lv_obj_get_height(f.app().status_label_), 20);
    SaveScreenshot("camera-long-path.ppm");
}

RODAK_TEST("teardown waits for LVGL ownership instead of leaving live callbacks") {
    Fixture f;
    f.Create();
    camera_test::ui_mutex.lock();
    std::atomic<bool> finished{false};
    std::thread destroy([&] { f.host.CloseCurrent(); finished = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const bool returned_early = finished.load();
    const uint32_t timeout = camera_test::last_lock_timeout.load();
    camera_test::ui_mutex.unlock();
    destroy.join();
    RODAK_CHECK_FALSE(returned_early);
    RODAK_CHECK_EQ(timeout, 0U);
    Pump(500);
    RODAK_CHECK_EQ(lv_obj_get_child_count(lv_screen_active()), 0U);
}
