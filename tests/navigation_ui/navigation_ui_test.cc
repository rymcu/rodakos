#include "test_framework.h"
#include "apps/camera/camera_app.h"
#include "phone_os/phone_system.h"
#include "phone_os/camera_service.h"
#include "phone_os/audio_focus_service.h"
#include "phone_ui/phone_ui.h"
#include "esp_lvgl_port.h"

#include <src/others/test/lv_test.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

// Hardware/Shell/Home content are fixtures; System, Navigation, Registry, Host,
// CameraApp, PhoneUi and all LVGL scheduling/locking are the production sources.
PhoneShell::PhoneShell(PhoneSystem& system, PhoneUi& ui, PhoneServices& services)
    : system_(system), ui_(ui), services_(services) {}
bool PhoneShell::Initialize() { return true; }
bool PhoneShell::PrepareNavigation() { return true; }
bool PhoneShell::Lock() { return true; }
bool PhoneShell::ToggleControlCenter() { return true; }
bool PhoneShell::SetLockOnBoot(bool value) { preferences_.lock_on_boot = value; return true; }
bool PhoneShell::SetControlCenterGestureEnabled(bool value) {
    preferences_.control_center_gesture_enabled = value;
    return true;
}

namespace {
int home_requests = 0;
int partial_cleanups = 0;
int stale_partial_ticks = 0;
int teardown_pipe = -1;
class TeardownFailure : public PhoneApp {
public:
    explicit TeardownFailure(bool fail_create) : fail_create_(fail_create) {}
    bool OnCreate(PhoneAppContext&) override {
        lv_timer_create([](lv_timer_t*) {}, 20, this);
        if (fail_create_) throw std::bad_alloc();
        return true;
    }
    void OnResume() override {}
    void OnPause() override {}
    void OnDestroy() override {
        (void)!write(teardown_pipe, "T", 1);
        throw std::bad_alloc();
    }
private:
    bool fail_create_;
};
class PartialCreateFailure : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext&) override {
        root_ = lv_obj_create(lv_screen_active());
        timer_ = lv_timer_create([](lv_timer_t*) { ++stale_partial_ticks; }, 20, this);
        throw std::bad_alloc();
    }
    void OnResume() override {}
    void OnPause() override {}
    void OnDestroy() override {
        ++partial_cleanups;
        lv_timer_delete(timer_);
        lv_obj_delete(root_);
    }
private:
    lv_obj_t* root_ = nullptr;
    lv_timer_t* timer_ = nullptr;
};
class HomeFixture : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext&) override { return true; }
    void OnResume() override {}
    void OnPause() override {}
    void OnDestroy() override {}
    bool OnThemeChanged(PhoneAppContext&) override { return true; }
    bool OnHomeRequested() override { ++home_requests; return true; }
};
struct Completion {
    int calls = 0;
    bool ok = false;
    static void Receive(void* p, bool value) {
        auto& self = *static_cast<Completion*>(p);
        ++self.calls;
        self.ok = value;
    }
};
void Pump(uint32_t time = 160) {
    std::lock_guard<std::recursive_timed_mutex> lock(camera_test::ui_mutex);
    lv_test_wait(time);
    lv_obj_update_layout(lv_screen_active());
}
size_t TimerCount() {
    size_t count = 0;
    for (auto* timer = lv_timer_get_next(nullptr); timer != nullptr; timer = lv_timer_get_next(timer)) ++count;
    return count;
}
struct Fixture {
    rodakos::CameraService camera;
    rodakos::AudioFocusService focus;
    PhoneUi ui{320, 240};
    PhoneServices services;
    std::unique_ptr<PhoneSystem> system;
    size_t home_timers = 0;
    uint32_t home_children = 0;
    Fixture() {
        home_requests = 0;
        camera_test::fail_next_ui_lock = false;
        services.SetCamera(&camera);
        services.SetAudioFocus(&focus);
        ui.SetThemeName("dark");
        system = std::make_unique<PhoneSystem>(ui, services);
        RODAK_CHECK(system->Start());
        Pump();
        home_timers = TimerCount();
    }
    ~Fixture() {
        if (camera.start_gate) camera.start_gate->Release();
        if (camera.stop_gate) camera.stop_gate->Release();
        camera_test::JoinTasks();
        system.reset();
        Pump(2000);
        lv_obj_clean(lv_screen_active());
        lv_obj_clean(lv_layer_top());
    }
    PhoneNavigation& navigation() { return system->navigation(); }
    bool Enqueue(const char* id, Completion& result) {
        return navigation().RequestLaunch(id, Completion::Receive, &result);
    }
    void LaunchCamera() {
        Completion result;
        RODAK_CHECK(Enqueue("camera", result));
        Pump();
        RODAK_CHECK_EQ(result.calls, 1);
        RODAK_CHECK(result.ok);
        RODAK_CHECK(camera.running);
    }
    void CheckHome() {
        RODAK_CHECK_EQ(system->GetAppHostState().current_app_id, "home");
        RODAK_CHECK_FALSE(camera.running);
        RODAK_CHECK_EQ(lv_obj_get_child_count(lv_screen_active()), home_children);
        RODAK_CHECK_EQ(TimerCount(), home_timers);
    }
    void ClickCameraHeader(int index) {
        std::lock_guard<std::recursive_timed_mutex> lock(camera_test::ui_mutex);
        auto* root = lv_obj_get_child(lv_screen_active(), 0);
        auto* header = lv_obj_get_child(root, 0);
        RODAK_CHECK_EQ(lv_obj_get_child_count(header), 3U);
        RODAK_CHECK_EQ(std::string(lv_label_get_text(lv_obj_get_child(header, 1))), "Camera");
        lv_obj_send_event(lv_obj_get_child(header, index), LV_EVENT_CLICKED, nullptr);
    }
};
}

void RegisterRodakBuiltInApps(PhoneAppRegistry& registry) {
    PhoneAppDescriptor home;
    home.id = "home";
    home.title = "Home";
    home.role = PhoneAppRole::kHome;
    home.show_on_home = false;
    home.aliases = {"Desktop", "桌面"};
    home.create = [] { return std::make_unique<HomeFixture>(); };
    registry.Register(std::move(home));
    RegisterCameraApp(registry);
    PhoneAppDescriptor failing;
    failing.id = "factory-error";
    failing.title = "Factory error";
    failing.create = []() -> std::unique_ptr<PhoneApp> { throw std::bad_alloc(); };
    registry.Register(failing);
    failing.id = "create-error";
    failing.title = "Create error";
    failing.create = [] { return std::make_unique<PartialCreateFailure>(); };
    registry.Register(std::move(failing));
    for (bool fail_create : {false, true}) {
        PhoneAppDescriptor teardown;
        teardown.id = fail_create ? "create-teardown-error" : "teardown-error";
        teardown.title = teardown.id;
        teardown.create = [fail_create] { return std::make_unique<TeardownFailure>(fail_create); };
        registry.Register(std::move(teardown));
    }
}

RODAK_TEST("serial Home is retained while Camera start holds the outer LVGL lock") {
    Fixture f;
    auto gate = std::make_shared<camera_test::Gate>();
    f.camera.start_gate = gate;
    Completion camera, home;
    RODAK_CHECK(f.Enqueue("camera", camera));
    std::thread ui([&] { Pump(160); });
    const bool entered = gate->Wait();
    // The old serial lvgl_port_lock(1000) path rejects this very window.
    const bool old_lock = lvgl_port_lock(1000);
    if (old_lock) lvgl_port_unlock();
    const auto begin = std::chrono::steady_clock::now();
    const bool queued = f.Enqueue("Desktop", home);
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    const int before = home.calls;
    gate->Release();
    ui.join();
    RODAK_CHECK(entered);
    RODAK_CHECK_FALSE(old_lock);
    RODAK_CHECK(queued);
    RODAK_CHECK(elapsed < std::chrono::milliseconds(100));
    RODAK_CHECK_EQ(before, 0);
    Pump(500);
    RODAK_CHECK_EQ(home.calls, 1);
    RODAK_CHECK(home.ok);
    RODAK_CHECK_EQ(f.camera.stops, 1);
    f.CheckHome();
}

RODAK_TEST("Home completion waits for Camera stop and later admission remains independent") {
    Fixture f;
    f.LaunchCamera();
    auto gate = std::make_shared<camera_test::Gate>();
    f.camera.stop_gate = gate;
    Completion first, second;
    RODAK_CHECK(f.Enqueue("home", first));
    std::thread ui([&] { Pump(100); });
    const bool entered = gate->Wait();
    const bool queued = f.Enqueue("桌面", second);
    const int before = first.calls + second.calls;
    gate->Release();
    ui.join();
    RODAK_CHECK(entered);
    RODAK_CHECK(queued);
    RODAK_CHECK_EQ(before, 0);
    Pump(500);
    RODAK_CHECK_EQ(first.calls, 1);
    RODAK_CHECK_EQ(second.calls, 1);
    RODAK_CHECK(first.ok && second.ok);
    f.CheckHome();
}

RODAK_TEST("serial admission survives a real LVGL display flush event gate") {
    Fixture f;
    f.LaunchCamera();
    auto gate = std::make_shared<camera_test::Gate>();
    auto* display = lv_display_get_default();
    const uint32_t event_index = lv_display_get_event_count(display);
    lv_display_add_event_cb(display, [](lv_event_t* e) {
        static_cast<camera_test::Gate*>(lv_event_get_user_data(e))->Enter();
    }, LV_EVENT_FLUSH_START, gate.get());
    std::thread ui([&] {
        std::lock_guard<std::recursive_timed_mutex> lock(camera_test::ui_mutex);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
    });
    const bool entered = gate->Wait();
    Completion home;
    const bool queued = f.Enqueue("home", home);
    const int before = home.calls;
    gate->Release();
    ui.join();
    lv_display_delete_event(display, event_index);
    RODAK_CHECK(entered);
    RODAK_CHECK(queued);
    RODAK_CHECK_EQ(before, 0);
    Pump(500);
    RODAK_CHECK_EQ(home.calls, 1);
    RODAK_CHECK(home.ok);
    f.CheckHome();
}

RODAK_TEST("full and invalid admissions never overwrite or execute later") {
    Fixture f;
    Completion admitted[DeferredNavigation::kCapacity], rejected;
    for (auto& result : admitted) RODAK_CHECK(f.Enqueue("home", result));
    RODAK_CHECK_FALSE(f.Enqueue("camera", rejected));
    RODAK_CHECK_FALSE(f.Enqueue("unknown-app", rejected));
    RODAK_CHECK_FALSE(f.Enqueue("", rejected));
    RODAK_CHECK_FALSE(f.navigation().RequestLaunch(std::string(64, 'x')));
    RODAK_CHECK_FALSE(f.navigation().RequestLaunch(std::string_view("home\0camera", 11)));
    Pump(1000);
    for (const auto& result : admitted) {
        RODAK_CHECK_EQ(result.calls, 1);
        RODAK_CHECK(result.ok);
    }
    RODAK_CHECK_EQ(rejected.calls, 0);
    RODAK_CHECK_EQ(f.camera.starts, 0);
    f.CheckHome();
}

RODAK_TEST("Camera back and repeated Home use the permanent queue") {
    Fixture f;
    f.LaunchCamera();
    f.ClickCameraHeader(0);
    RODAK_CHECK_EQ(f.system->GetAppHostState().current_app_id, "camera");
    Pump(400);
    f.CheckHome();
    RODAK_CHECK(f.navigation().RequestHome());
    RODAK_CHECK(f.navigation().RequestHome());
    Pump(200);
    RODAK_CHECK_EQ(home_requests, 2);
}

RODAK_TEST("rapid Camera Home requests leave no old Camera timers or pixels") {
    Fixture f;
    Completion results[4];
    RODAK_CHECK(f.Enqueue("CAP-ture", results[0]));
    RODAK_CHECK(f.Enqueue("home", results[1]));
    RODAK_CHECK(f.Enqueue("相机", results[2]));
    RODAK_CHECK(f.Enqueue("home", results[3]));
    Pump(1000);
    for (const auto& result : results) {
        RODAK_CHECK_EQ(result.calls, 1);
        RODAK_CHECK(result.ok);
    }
    f.CheckHome();
    const int starts = f.camera.starts;
    Pump(2000);
    RODAK_CHECK_EQ(starts, f.camera.starts);
    f.CheckHome();
}

RODAK_TEST("Camera Home shows rejection when full and a new click can recover") {
    Fixture f;
    f.LaunchCamera();
    Completion retained[DeferredNavigation::kCapacity];
    for (auto& result : retained) RODAK_CHECK(f.Enqueue("camera", result));
    f.ClickCameraHeader(2);
    auto* toast = lv_obj_get_child(lv_screen_active(), -1);
    RODAK_CHECK(toast != nullptr);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(toast)), "返回桌面失败，请重试");
    f.home_children = 1;
    Pump(2400);
    for (const auto& result : retained) RODAK_CHECK_EQ(result.calls, 1);
    RODAK_CHECK_EQ(f.system->GetAppHostState().current_app_id, "camera");
    f.ClickCameraHeader(2);
    Pump(500);
    f.CheckHome();
}

RODAK_TEST("shutdown cancels accepted work exactly once and revokes future admission") {
    Fixture f;
    Completion first, second;
    RODAK_CHECK(f.Enqueue("camera", first));
    RODAK_CHECK(f.Enqueue("home", second));
    f.navigation().CloseDeferred();
    RODAK_CHECK_EQ(first.calls, 1);
    RODAK_CHECK_EQ(second.calls, 1);
    RODAK_CHECK_FALSE(first.ok || second.ok);
    RODAK_CHECK_FALSE(f.Enqueue("camera", first));
    RODAK_CHECK_FALSE(f.navigation().RequestHome());
    Pump(500);
    RODAK_CHECK_EQ(first.calls, 1);
    RODAK_CHECK_EQ(f.camera.starts, 0);
}

RODAK_TEST("completion may reenter Close without repeating itself or queued requests") {
    DeferredNavigation queue;
    struct State {
        DeferredNavigation* queue;
        int dispatches = 0;
        int completions = 0;
        bool requeued = true;
    } state{&queue};
    RODAK_CHECK(queue.Initialize([](void* p, std::string_view, bool) {
        ++static_cast<State*>(p)->dispatches;
        return true;
    }, &state));
    Completion cancelled;
    RODAK_CHECK(queue.Enqueue("home", false, [](void* p, bool ok) {
        auto& s = *static_cast<State*>(p);
        RODAK_CHECK(ok);
        ++s.completions;
        s.queue->Close();
        s.requeued = s.queue->Enqueue("camera", false);
    }, &state));
    RODAK_CHECK(queue.Enqueue("camera", false, Completion::Receive, &cancelled));
    Pump(500);
    RODAK_CHECK_EQ(state.dispatches, 1);
    RODAK_CHECK_EQ(state.completions, 1);
    RODAK_CHECK_FALSE(state.requeued);
    RODAK_CHECK_EQ(cancelled.calls, 1);
    RODAK_CHECK_FALSE(cancelled.ok);
}

RODAK_TEST("factory and partial OnCreate exceptions report failure and release navigation") {
    Fixture f;
    partial_cleanups = 0;
    stale_partial_ticks = 0;
    Completion factory, create, home;
    RODAK_CHECK(f.Enqueue("factory-error", factory));
    RODAK_CHECK(f.Enqueue("create-error", create));
    RODAK_CHECK(f.Enqueue("home", home));
    Pump(500);
    RODAK_CHECK_EQ(factory.calls, 1);
    RODAK_CHECK_FALSE(factory.ok);
    RODAK_CHECK_EQ(create.calls, 1);
    RODAK_CHECK_FALSE(create.ok);
    RODAK_CHECK_EQ(home.calls, 1);
    RODAK_CHECK(home.ok);
    RODAK_CHECK_FALSE(f.system->GetAppHostState().transition_in_progress);
    RODAK_CHECK_EQ(partial_cleanups, 1);
    RODAK_CHECK_EQ(stale_partial_ticks, 0);
    f.CheckHome();
}

RODAK_TEST("throwing completions do not strand later dispatch or Close cancellations") {
    DeferredNavigation queue;
    int throws = 0;
    RODAK_CHECK(queue.Initialize([](void*, std::string_view, bool) { return true; }, nullptr));
    auto throwing = [](void* p, bool) { ++*static_cast<int*>(p); throw std::bad_alloc(); };
    Completion next, cancelled;
    RODAK_CHECK(queue.Enqueue("home", false, throwing, &throws));
    RODAK_CHECK(queue.Enqueue("home", false, Completion::Receive, &next));
    Pump(200);
    RODAK_CHECK_EQ(throws, 1);
    RODAK_CHECK_EQ(next.calls, 1);
    RODAK_CHECK(next.ok);
    RODAK_CHECK(queue.Enqueue("home", false, throwing, &throws));
    RODAK_CHECK(queue.Enqueue("home", false, Completion::Receive, &cancelled));
    queue.Close();
    RODAK_CHECK_EQ(throws, 2);
    RODAK_CHECK_EQ(cancelled.calls, 1);
    RODAK_CHECK_FALSE(cancelled.ok);
}

RODAK_TEST("unsafe teardown and unknown lifecycle exceptions must abort before continuing") {
    for (int mode = 0; mode < 3; ++mode) {
        int pipe_fd[2];
        RODAK_CHECK_EQ(pipe(pipe_fd), 0);
        const pid_t child = fork();
        RODAK_CHECK(child >= 0);
        if (child == 0) {
            close(pipe_fd[0]);
            teardown_pipe = pipe_fd[1];
            if (mode < 2) {
                Fixture f;
                Completion result;
                f.Enqueue(mode == 0 ? "teardown-error" : "create-teardown-error", result);
                Pump(160);
                if (mode == 1) _exit(91);
                f.Enqueue("home", result);
                Pump(160);
                _exit(91);
            } else {
                DeferredNavigation queue;
                queue.Initialize([](void*, std::string_view, bool) -> bool {
                    (void)!write(teardown_pipe, "T", 1);
                    throw std::bad_alloc();
                }, nullptr);
                queue.Enqueue("home", false);
                Pump(160);
                _exit(91);
            }
            _exit(91);
        }
        close(pipe_fd[1]);
        char marker = 0;
        const auto bytes = read(pipe_fd[0], &marker, 1);
        close(pipe_fd[0]);
        int status = 0;
        RODAK_CHECK_EQ(waitpid(child, &status, 0), child);
        RODAK_CHECK_EQ(bytes, 1);
        RODAK_CHECK_EQ(marker, 'T');
        RODAK_CHECK(WIFSIGNALED(status));
        RODAK_CHECK_EQ(WTERMSIG(status), SIGABRT);
    }
}
