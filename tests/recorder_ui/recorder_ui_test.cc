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
#include "phone_ui/phone_fonts.h"
#include "phone_ui/rodakos_theme.h"
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
bool ContainsText(lv_obj_t* root, const std::string& expected) {
    if (root == nullptr || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return false;
    if (lv_obj_check_type(root, &lv_label_class) &&
        std::string(lv_label_get_text(root)).find(expected) != std::string::npos) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        if (ContainsText(lv_obj_get_child(root, i), expected)) return true;
    }
    return false;
}
bool HasText(lv_obj_t* root, const std::string& expected) {
    if (root == nullptr || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return false;
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
    std::string InfoTitle() { return lv_label_get_text(app.info_title_label_); }
    std::string InfoDetail() { return app.info_detail_text_; }
    void OpenList() { lv_obj_send_event(app.list_button_, LV_EVENT_CLICKED, nullptr); Pump(10); }
    lv_obj_t* Row(int index) { return lv_obj_get_child(app.list_, index); }
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
    RODAK_CHECK_EQ(f.Status(), "录音失败");
    RODAK_CHECK_EQ(f.InfoTitle(), "无法完成文件保存");
    RODAK_CHECK_EQ(f.InfoDetail(), "点红钮重试 · " + state.last_error);
    RODAK_CHECK_FALSE(ContainsText(f.app.root_, "已保存"));
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
    RODAK_CHECK_EQ(f.Status(), "录音失败");
    Pump(2000);
    SaveScreenshot("recorder-save-error.ppm");
    recording_host::ResetStdioFaults();
    f.Start();
    f.Stop();
    RODAK_CHECK(f.Status().find("已保存 · ") == 0);
    RODAK_CHECK_EQ(f.app.displayed_recordings_.size(), 1U);
    RODAK_CHECK(f.InfoTitle().find("最近 · ") == 0);
}
RODAK_TEST("missing card appears as a library error instead of no recordings") {
    Fixture f;
    f.files.fail_mount = true;
    f.Create();
    const auto state = f.recording.GetState();
    RODAK_CHECK_EQ(state.status, rodakos::RecordingStatus::kIdle);
    RODAK_CHECK_EQ(f.Status(), "准备就绪 · 列表不可用");
    RODAK_CHECK_EQ(f.InfoDetail(), state.library_error);
    f.OpenList();
    RODAK_CHECK(HasText(f.app.list_status_, "读不到 SD 卡"));
    RODAK_CHECK(HasText(f.app.list_status_, "请插入或重新插拔 SD 卡后重试"));
    RODAK_CHECK_FALSE(HasText(f.app.list_status_, "还没有录音"));
    RODAK_CHECK(HasText(f.app.list_status_, "重试"));
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
    RODAK_CHECK(f.Status().find("已保存 · ") == 0);
    RODAK_CHECK_EQ(f.InfoTitle(), "列表读取失败，录音已正常保存");
    RODAK_CHECK_EQ(f.InfoDetail(), "点右上重试 · Failed to list recordings");
    RODAK_CHECK(std::filesystem::exists(state.full_path));
    RODAK_CHECK(f.app.displayed_recordings_.empty());
    f.ui.SetThemeName("light");
    RODAK_CHECK(f.app.OnThemeChanged(f.context));
    Pump(2000);
    SaveScreenshot("recorder-saved-list-error.ppm");
    f.files.fail_list = false;
    RODAK_CHECK(f.recording.RefreshRecordings());
    Pump();
    RODAK_CHECK(f.Status().find("已保存 · ") == 0);
    RODAK_CHECK_EQ(f.app.displayed_recordings_.size(), 1U);
    RODAK_CHECK(f.InfoTitle().find("最近 · ") == 0);
}
RODAK_TEST("idle directory read failure recovers without a recording-status transition") {
    Fixture f;
    f.files.fail_list = true;
    f.Create();
    f.OpenList();
    RODAK_CHECK(HasText(f.app.list_status_, "无法读取录音列表"));
    RODAK_CHECK(HasText(f.app.list_status_, "Failed to list recordings"));
    f.files.fail_list = false;
    RODAK_CHECK(f.recording.RefreshRecordings());
    Pump();
    RODAK_CHECK(HasText(f.app.list_status_, "还没有录音"));
    RODAK_CHECK(HasText(f.app.list_status_, "返回录音"));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.title_label_)), "录音 · 0");
    RODAK_CHECK(f.app.OnBackRequested());
    Pump(10);
    RODAK_CHECK_EQ(f.Status(), "准备就绪 · 已有 0 条录音");
}
RODAK_TEST("microphone failure reaches the UI asynchronously and allows retry") {
    Fixture f;
    f.Create();
    f.input.fail_read = true;
    f.ClickRecord();
    Await([&] { return f.recording.GetState().status == rodakos::RecordingStatus::kError; });
    audio_host::JoinTasks();
    Pump();
    RODAK_CHECK_EQ(f.Status(), "录音失败");
    RODAK_CHECK_EQ(f.InfoTitle(), "麦克风读取失败");
    RODAK_CHECK(f.InfoDetail().find(f.recording.GetState().last_error) != std::string::npos);
    f.input.fail_read = false;
    f.Start();
    f.Stop();
    RODAK_CHECK(f.Status().find("已保存 · ") == 0);
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
    RODAK_CHECK_EQ(f.Status(), "录音失败");
    f.ui.SetThemeName("light");
    RODAK_CHECK(f.app.OnThemeChanged(f.context));
    Pump();
    RODAK_CHECK(f.InfoDetail().find(f.recording.GetState().last_error) != std::string::npos);
    // Header, status line, timer, stage card and button row never overlap.
    lv_area_t status{}, duration{}, stage{}, button{}, tone{};
    lv_obj_get_coords(f.app.status_label_, &status);
    lv_obj_get_coords(f.app.duration_label_, &duration);
    lv_obj_get_coords(f.app.stage_, &stage);
    lv_obj_get_coords(f.app.record_button_, &button);
    lv_obj_get_coords(f.app.tone_button_, &tone);
    RODAK_CHECK(status.y1 >= 40);
    RODAK_CHECK(status.y2 < duration.y1);
    RODAK_CHECK(duration.y2 < stage.y1);
    RODAK_CHECK(stage.y2 < button.y1);
    RODAK_CHECK(tone.x2 < button.x1);
    RODAK_CHECK(button.y2 < 240);
}
RODAK_TEST("saved recording row retains play stop and active-recording guards") {
    Fixture f;
    f.Create();
    f.Start();
    f.Stop();
    f.OpenList();
    auto* row = f.Row(0);
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_EQ(f.audio.plays, 1);
    RODAK_CHECK_EQ(f.audio.state.file_path, f.app.displayed_recordings_[0].full_path);
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_EQ(f.audio.stops, 1);
    RODAK_CHECK(f.app.OnBackRequested());
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
    RODAK_CHECK_EQ(f.Status(), "录音服务不可用");
    f.OpenList();
    RODAK_CHECK(HasText(f.app.list_status_, "录音服务不可用"));
    RODAK_CHECK_FALSE(HasText(f.app.list_status_, "还没有录音"));
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
    RODAK_CHECK(f.Status().find("已保存 · ") == 0);
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
    RODAK_CHECK(f.Status().find("已保存 · ") == 0);
    RODAK_CHECK_EQ(f.app.displayed_recordings_.size(), 1U);
}
RODAK_TEST("recording playback boost is restored after playback ends or the app closes") {
    Fixture f;
    f.Create();
    f.Start();
    f.Stop();
    f.OpenList();
    auto* row = f.Row(0);
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_EQ(f.audio.configured_volume, 90);
    Pump();
    RODAK_CHECK_EQ(f.audio.configured_volume, 90);
    f.audio.state.status = rodakos::AudioPlaybackStatus::kCompleted;
    Pump();
    RODAK_CHECK_EQ(f.audio.configured_volume, 60);

    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_EQ(f.audio.configured_volume, 90);
    f.app.OnDestroy();
    f.created = false;
    RODAK_CHECK_EQ(f.audio.configured_volume, 60);
}
RODAK_TEST("recording playback keeps a volume changed elsewhere and never lowers a loud setting") {
    Fixture f;
    f.Create();
    f.Start();
    f.Stop();
    f.OpenList();
    auto* row = f.Row(0);
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    f.audio.configured_volume = 45;
    f.audio.state.status = rodakos::AudioPlaybackStatus::kStopped;
    Pump();
    RODAK_CHECK_EQ(f.audio.configured_volume, 45);

    f.audio.configured_volume = 95;
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    f.audio.state.status = rodakos::AudioPlaybackStatus::kCompleted;
    Pump();
    RODAK_CHECK_EQ(f.audio.configured_volume, 95);
}
RODAK_TEST("starting a recording ends a pending playback boost") {
    Fixture f;
    f.Create();
    f.Start();
    f.Stop();
    lv_obj_send_event(f.app.replay_button_, LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_EQ(f.audio.configured_volume, 90);
    f.Start();
    RODAK_CHECK_EQ(f.audio.configured_volume, 60);
    f.Stop();
}
RODAK_TEST("speaker test tone plays at the test volume and restores the previous volume") {
    Fixture f;
    f.Create();
    int volume_during_tone = -1;
    f.output.write_hook = [&]() { volume_during_tone = f.output.configured_volume; };
    lv_obj_send_event(f.app.tone_button_, LV_EVENT_CLICKED, nullptr);
    audio_host::JoinTasks();
    f.output.write_hook = {};
    RODAK_CHECK_EQ(volume_during_tone, 90);
    RODAK_CHECK_EQ(f.output.configured_volume.load(), 60);
    RODAK_CHECK_FALSE(f.output.IsOpenForOwner("recorder-tone"));
}

RODAK_TEST("recording page shows a live level meter, the wake-off mark and a stop button") {
    Fixture f;
    f.Create();
    RODAK_CHECK_EQ(f.Status(), "准备就绪 · 已有 0 条录音");
    RODAK_CHECK_EQ(f.InfoTitle(), "还没有录音");
    RODAK_CHECK(lv_obj_has_state(f.app.replay_button_, LV_STATE_DISABLED));
    RODAK_CHECK(lv_obj_has_flag(f.app.wake_hint_, LV_OBJ_FLAG_HIDDEN));
    SaveScreenshot("recorder-idle-empty.ppm");
    f.Start();
    Pump(800);
    RODAK_CHECK(f.Status().find("正在录音 · ") == 0);
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.wake_hint_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(lv_obj_has_flag(f.app.info_box_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(lv_obj_has_state(f.app.tone_button_, LV_STATE_DISABLED));
    // The host codec reads about -6.5 dBFS (level 86): the newest bars are tall and warning-coloured.
    auto* newest = f.app.level_bars_[RecorderApp::kLevelBars - 1];
    RODAK_CHECK(lv_obj_get_height(newest) >= 28);
    RODAK_CHECK(lv_color_to_u32(lv_obj_get_style_bg_color(newest, 0)) == lv_color_to_u32(rodakos_theme_warning()));
    RODAK_CHECK_EQ(lv_obj_get_width(f.app.record_core_), 20);
    RODAK_CHECK(f.app.fast_refresh_);
    SaveScreenshot("recorder-recording.ppm");
    f.Stop();
    RODAK_CHECK_FALSE(f.app.fast_refresh_);
    RODAK_CHECK(lv_obj_has_flag(f.app.wake_hint_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK_EQ(lv_obj_get_width(f.app.record_core_), 34);
    RODAK_CHECK(f.InfoTitle().find("最近 · ") == 0);
    RODAK_CHECK_FALSE(lv_obj_has_state(f.app.replay_button_, LV_STATE_DISABLED));
    SaveScreenshot("recorder-saved.ppm");
}

RODAK_TEST("replay plays the newest recording on the record page and toggles to stop") {
    Fixture f;
    f.Create();
    f.Start();
    f.Stop();
    lv_obj_send_event(f.app.replay_button_, LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_EQ(f.audio.plays, 1);
    RODAK_CHECK_EQ(f.audio.state.file_path, f.app.displayed_recordings_[0].full_path);
    Pump();
    RODAK_CHECK(f.InfoTitle().find("正在回放 · ") == 0);
    RODAK_CHECK(f.InfoDetail().find("自动提高音量") != std::string::npos);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.replay_label_)), "停止");
    SaveScreenshot("recorder-replay.ppm");
    lv_obj_send_event(f.app.replay_button_, LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_EQ(f.audio.stops, 1);
    Pump();
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.replay_label_)), "回放");
}

RODAK_TEST("list page shows Chinese dates, the playing row and its progress") {
    Fixture f;
    f.Create();
    f.Start();
    f.Stop();
    f.Start();
    f.Stop();
    f.OpenList();
    RODAK_CHECK(f.app.view_ == RecorderApp::View::kList);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.title_label_)), "录音 · 2");
    RODAK_CHECK(lv_obj_has_flag(f.app.list_button_, LV_OBJ_FLAG_HIDDEN));
    // The host clock is fixed at 2027-01-15 08:00:00 UTC, so both recordings share a second; the
    // service adds "_0001" to the second one and the list keeps them apart.
    const std::string first = lv_label_get_text(lv_obj_get_child(f.Row(0), 1));
    const std::string second = lv_label_get_text(lv_obj_get_child(f.Row(1), 1));
    RODAK_CHECK(first != second);
    RODAK_CHECK(first.find("1月15日 ") == 0 || second.find("1月15日 ") == 0);
    RODAK_CHECK(first.find(" (2)") != std::string::npos || second.find(" (2)") != std::string::npos);
    lv_obj_send_event(f.Row(0), LV_EVENT_CLICKED, nullptr);
    f.audio.state.position_ms = 0;
    Pump();
    RODAK_CHECK_EQ(std::string(lv_label_get_text(lv_obj_get_child(f.Row(0), 0))), ICON_STOP);
    RODAK_CHECK(std::string(lv_label_get_text(lv_obj_get_child(f.Row(0), 2))).find(" / ") != std::string::npos);
    RODAK_CHECK_FALSE(lv_obj_has_flag(lv_obj_get_child(f.Row(0), 4), LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(lv_obj_has_flag(lv_obj_get_child(f.Row(1), 4), LV_OBJ_FLAG_HIDDEN));
    SaveScreenshot("recorder-list.ppm");
    RODAK_CHECK(f.app.OnBackRequested());
    RODAK_CHECK(f.app.view_ == RecorderApp::View::kRecord);
}

RODAK_TEST("delete asks first, stops playback of that row and removes the file") {
    Fixture f;
    f.Create();
    f.Start();
    f.Stop();
    f.OpenList();
    const auto path = f.app.displayed_recordings_[0].full_path;
    lv_obj_send_event(f.Row(0), LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_EQ(f.audio.plays, 1);
    auto* trash = lv_obj_get_child(f.Row(0), 3);
    lv_obj_send_event(trash, LV_EVENT_CLICKED, nullptr);
    Pump(10);
    RODAK_CHECK(HasText(lv_layer_top(), "删除这条录音？"));
    RODAK_CHECK(std::filesystem::exists(path));
    SaveScreenshot("recorder-delete.ppm");
    // Back closes the dialog first.
    RODAK_CHECK(f.app.OnBackRequested());
    Pump(10);
    RODAK_CHECK_FALSE(HasText(lv_layer_top(), "删除这条录音？"));
    RODAK_CHECK(f.app.view_ == RecorderApp::View::kList);

    lv_obj_send_event(trash, LV_EVENT_CLICKED, nullptr);
    Pump(10);
    f.app.ConfirmDelete();
    Pump(10);
    RODAK_CHECK_EQ(f.audio.stops, 1);
    RODAK_CHECK_FALSE(std::filesystem::exists(path));
    RODAK_CHECK(f.app.displayed_recordings_.empty());
    RODAK_CHECK(HasText(f.app.list_status_, "还没有录音"));
}

RODAK_TEST("recording file names map to Chinese dates; other names are kept") {
    Fixture f;
    f.Create();
    f.Start();
    f.Stop();
    const auto saved = f.app.displayed_recordings_[0].full_path;
    const auto directory = std::filesystem::path(saved).parent_path();
    std::filesystem::copy_file(saved, directory / "REC_20261127_101508.wav");
    std::filesystem::copy_file(saved, directory / "REC_BOOT_18420.wav");
    RODAK_CHECK(f.recording.RefreshRecordings());
    f.OpenList();
    bool dated = false, boot = false;
    for (int i = 0; i < 3; ++i) {
        const std::string text = lv_label_get_text(lv_obj_get_child(f.Row(i), 1));
        dated = dated || text == "11月27日 10:15";
        boot = boot || text == "REC_BOOT_18420";
    }
    RODAK_CHECK(dated);
    RODAK_CHECK(boot);
}
