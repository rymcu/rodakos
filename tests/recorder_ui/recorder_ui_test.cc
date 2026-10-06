#include "test_framework.h"
#include "phone_os/recording_service.h"
#include "phone_os/audio_focus_service.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/audio_service.h"
#include "rodakos_adapters/audio_codec_input.h"
#include "host_files.h"
#include "host_runtime.h"
#include "stdio_faults.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#define private public
#include "apps/recorder/recorder_app.h"
#undef private
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_ui.h"
#include "settings.h"
#include <src/others/test/lv_test.h>

namespace recorder_ui_test {
bool fail_next_timer = false;
}
extern "C" lv_timer_t* __real_lv_timer_create(lv_timer_cb_t, uint32_t, void*);
extern "C" lv_timer_t* __wrap_lv_timer_create(lv_timer_cb_t callback, uint32_t period, void* data) {
    if (recorder_ui_test::fail_next_timer) {
        recorder_ui_test::fail_next_timer = false;
        return nullptr;
    }
    return __real_lv_timer_create(callback, period, data);
}
namespace {
recording_host::StdioFaults Fault(bool recording_host::StdioFaults::* field) {
    recording_host::StdioFaults faults{};
    faults.*field = true;
    return faults;
}
void Pump(uint32_t milliseconds = 550) {
    lv_test_wait(milliseconds);
    lv_obj_update_layout(lv_screen_active());
}
void Await(const std::function<bool()>& condition) {
    for (int i = 0; i < 400; ++i) {
        if (condition()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    RODAK_CHECK(condition());
}
bool HasText(lv_obj_t* root, const std::string& expected) {
    if (lv_obj_check_type(root, &lv_label_class) && lv_label_get_text(root) == expected) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        if (HasText(lv_obj_get_child(root, i), expected)) return true;
    }
    return false;
}
void SaveScreenshot(const char* name) {
    Pump(10);
    const auto* frame = lv_display_get_buf_active(lv_display_get_default());
    std::ofstream out(name, std::ios::binary);
    out << "P6\n320 240\n255\n";
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            const auto* pixel = frame->data + y * frame->header.stride + x * 4;
            const char rgb[] = {static_cast<char>(pixel[2]), static_cast<char>(pixel[1]),
                                static_cast<char>(pixel[0])};
            out.write(rgb, 3);
        }
    }
}
struct TempDirectory {
    std::string path;
    TempDirectory() {
        static unsigned sequence = 0;
        path = (std::filesystem::temp_directory_path() /
                ("rodakos-recorder-ui-" + std::to_string(getpid()) + "-" + std::to_string(++sequence))).string();
        std::filesystem::create_directories(path);
    }
    ~TempDirectory() { std::filesystem::remove_all(path); }
};
struct Fixture {
    TempDirectory directory;
    recording_host::Files files{directory.path};
    rodakos::AudioCodecInput input;
    rodakos::AudioFocusService focus;
    rodakos::RecordingService recording{input, &files, &focus};
    rodakos::AudioService audio;
    rodakos::AudioOutputService output;
    PhoneUi ui{320, 240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui, navigation, registry, services, settings};
    RecorderApp app;
    bool created = false;
    Fixture() {
        recording_host::ResetStdioFaults();
        recorder_ui_test::fail_next_timer = false;
        audio_host::fail_task_creation = false;
        audio_host::task_start_hook = {};
        ui.SetThemeName("dark");
        services.SetRecording(&recording);
        services.SetAudio(&audio);
        services.SetAudioOutput(&output);
    }
    ~Fixture() {
        if (created) app.OnDestroy();
        recording.Stop();
        audio_host::JoinTasks();
        Pump(2000);
        lv_obj_clean(lv_screen_active());
        lv_obj_clean(lv_layer_top());
        recording_host::ResetStdioFaults();
    }
    void Create() {
        RODAK_CHECK(app.OnCreate(context));
        created = true;
        Pump(10);
    }
    void ClickRecord() { lv_obj_send_event(app.record_button_, LV_EVENT_CLICKED, nullptr); }
    std::string Status() { return lv_label_get_text(app.status_label_); }
    void Start() {
        ClickRecord();
        Await([&] { return recording.GetState().bytes_written > 0; });
        Pump();
        RODAK_CHECK_EQ(recording.GetState().status, rodakos::RecordingStatus::kRecording);
    }
    void Stop() {
        ClickRecord();
        audio_host::JoinTasks();
        Pump();
    }
};
void VerifyFinalizationFailure(recording_host::StdioFaults faults) {
    Fixture f;
    f.Create();
    recording_host::SetStdioFaults(faults);
    f.Start();
    f.Stop();
    const auto state = f.recording.GetState();
    RODAK_CHECK_EQ(state.status, rodakos::RecordingStatus::kError);
    RODAK_CHECK_EQ(f.Status(), "Recording failed");
    RODAK_CHECK_FALSE(HasText(f.app.root_, "Saved"));
    RODAK_CHECK(HasText(f.app.list_, state.last_error));
    RODAK_CHECK_FALSE(lv_obj_has_state(f.app.record_button_, LV_STATE_DISABLED));
    RODAK_CHECK(f.app.displayed_recordings_.empty());
}
}

RODAK_TEST("final WAV header failure is visible in the real Recorder UI") {
    VerifyFinalizationFailure(Fault(&recording_host::StdioFaults::final_header));
}
RODAK_TEST("final flush failure is visible in the real Recorder UI") {
    VerifyFinalizationFailure(Fault(&recording_host::StdioFaults::flush));
}
RODAK_TEST("final close failure is visible in the real Recorder UI") {
    VerifyFinalizationFailure(Fault(&recording_host::StdioFaults::close));
}
RODAK_TEST("failed recording can retry through the record and stop buttons") {
    Fixture f;
    f.Create();
    recording_host::SetStdioFaults(Fault(&recording_host::StdioFaults::final_header));
    f.Start();
    f.Stop();
    RODAK_CHECK_EQ(f.Status(), "Recording failed");
    Pump(2000);
    SaveScreenshot("recorder-save-error.ppm");
    recording_host::ResetStdioFaults();
    f.Start();
    f.Stop();
    RODAK_CHECK_EQ(f.Status(), "Saved");
    RODAK_CHECK_EQ(f.app.displayed_recordings_.size(), 1U);
    RODAK_CHECK_FALSE(HasText(f.app.list_, "Recording failed"));
}
RODAK_TEST("missing card appears as a library error instead of no recordings") {
    Fixture f;
    f.files.fail_mount = true;
    f.Create();
    const auto state = f.recording.GetState();
    RODAK_CHECK_EQ(state.status, rodakos::RecordingStatus::kIdle);
    RODAK_CHECK_EQ(f.Status(), state.library_error);
    RODAK_CHECK(HasText(f.app.list_, state.library_error));
    RODAK_CHECK_FALSE(HasText(f.app.list_, "No recordings"));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.count_label_)), "Unavailable");
    SaveScreenshot("recorder-missing-card.ppm");
}
RODAK_TEST("successful save remains Saved while a separate list scan fails") {
    Fixture f;
    f.Create();
    f.Start();
    f.files.fail_list = true;
    f.Stop();
    const auto state = f.recording.GetState();
    RODAK_CHECK_EQ(state.status, rodakos::RecordingStatus::kCompleted);
    RODAK_CHECK_EQ(f.Status(), "Saved");
    RODAK_CHECK(HasText(f.app.list_, "Failed to list recordings"));
    RODAK_CHECK(std::filesystem::exists(state.full_path));
    RODAK_CHECK(f.app.displayed_recordings_.empty());
    f.ui.SetThemeName("light");
    RODAK_CHECK(f.app.OnThemeChanged(f.context));
    Pump(2000);
    SaveScreenshot("recorder-saved-list-error.ppm");
    f.files.fail_list = false;
    RODAK_CHECK(f.recording.RefreshRecordings());
    Pump();
    RODAK_CHECK_EQ(f.Status(), "Saved");
    RODAK_CHECK_EQ(f.app.displayed_recordings_.size(), 1U);
    RODAK_CHECK_FALSE(HasText(f.app.list_, "Failed to list recordings"));
}
RODAK_TEST("idle directory read failure recovers without a recording-status transition") {
    Fixture f;
    f.files.fail_list = true;
    f.Create();
    RODAK_CHECK(HasText(f.app.list_, "Failed to list recordings"));
    f.files.fail_list = false;
    RODAK_CHECK(f.recording.RefreshRecordings());
    Pump();
    RODAK_CHECK_EQ(f.Status(), "Ready");
    RODAK_CHECK(HasText(f.app.list_, "No recordings"));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.count_label_)), "0 files");
}
RODAK_TEST("microphone failure reaches the UI asynchronously and allows retry") {
    Fixture f;
    f.Create();
    f.input.fail_read = true;
    f.ClickRecord();
    Await([&] { return f.recording.GetState().status == rodakos::RecordingStatus::kError; });
    audio_host::JoinTasks();
    Pump();
    RODAK_CHECK_EQ(f.Status(), "Recording failed");
    RODAK_CHECK(HasText(f.app.list_, f.recording.GetState().last_error));
    f.input.fail_read = false;
    f.Start();
    f.Stop();
    RODAK_CHECK_EQ(f.Status(), "Saved");
}
RODAK_TEST("pause resume and theme rebuild preserve a pending recording result") {
    Fixture f;
    f.Create();
    f.Start();
    f.app.OnPause();
    recording_host::SetStdioFaults(Fault(&recording_host::StdioFaults::flush));
    f.recording.Stop();
    audio_host::JoinTasks();
    f.app.OnResume();
    RODAK_CHECK_EQ(f.Status(), "Recording failed");
    f.ui.SetThemeName("light");
    RODAK_CHECK(f.app.OnThemeChanged(f.context));
    Pump();
    RODAK_CHECK(HasText(f.app.list_, f.recording.GetState().last_error));
    lv_area_t title{}, status{}, duration{}, size{};
    lv_obj_get_coords(f.app.title_label_, &title);
    lv_obj_get_coords(f.app.status_label_, &status);
    lv_obj_get_coords(f.app.duration_label_, &duration);
    lv_obj_get_coords(f.app.size_label_, &size);
    RODAK_CHECK(title.y2 < status.y1);
    RODAK_CHECK(title.x2 < duration.x1);
    RODAK_CHECK(status.x2 < size.x1);
}
RODAK_TEST("saved recording row retains play stop and active-recording guards") {
    Fixture f;
    f.Create();
    f.Start();
    f.Stop();
    auto* row = lv_obj_get_child(f.app.list_, 0);
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_EQ(f.audio.plays, 1);
    RODAK_CHECK_EQ(f.audio.state.file_path, f.app.displayed_recordings_[0].full_path);
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_EQ(f.audio.stops, 1);
    f.Start();
    f.app.PlayRecording(0);
    RODAK_CHECK_EQ(f.audio.plays, 1);
    f.Stop();
}
RODAK_TEST("missing recording service disables recording and identifies the unavailable list") {
    Fixture f;
    f.services.SetRecording(nullptr);
    f.Create();
    RODAK_CHECK(lv_obj_has_state(f.app.record_button_, LV_STATE_DISABLED));
    RODAK_CHECK(HasText(f.app.list_, "Recording service missing"));
    RODAK_CHECK_FALSE(HasText(f.app.list_, "No recordings"));
}

RODAK_TEST("refresh timer allocation failure rejects creation and cleans the partial UI") {
    Fixture f;
    recorder_ui_test::fail_next_timer = true;
    RODAK_CHECK_FALSE(f.app.OnCreate(f.context));
    f.app.OnDestroy();
    Pump();
    RODAK_CHECK_EQ(lv_obj_get_child_count(lv_screen_active()), 0U);
    f.Create();
    f.Start();
    f.Stop();
    RODAK_CHECK_EQ(f.Status(), "Saved");
}
RODAK_TEST("theme refresh timer allocation failure is reported and remains safely destroyable") {
    Fixture f;
    f.Create();
    f.ui.SetThemeName("light");
    recorder_ui_test::fail_next_timer = true;
    RODAK_CHECK_FALSE(f.app.OnThemeChanged(f.context));
    f.app.OnDestroy();
    f.created = false;
    Pump();
    RODAK_CHECK_EQ(lv_obj_get_child_count(lv_screen_active()), 0U);
}
RODAK_TEST("a result completed between UI ticks still refreshes the recording list") {
    Fixture f;
    f.Create();
    RODAK_CHECK(f.recording.Start());
    Await([&] { return f.recording.GetState().bytes_written > 0; });
    f.recording.Stop();
    audio_host::JoinTasks();
    Pump();
    RODAK_CHECK_EQ(f.Status(), "Saved");
    RODAK_CHECK_EQ(f.app.displayed_recordings_.size(), 1U);
}
