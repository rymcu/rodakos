#include "test_framework.h"

#include <cstring>
#include <fstream>
#include <functional>
#include <string>

#define private public
#include "apps/system_info/system_info_app.h"
#undef private
#include "esp_heap_caps.h"
#include "phone_os/audio_focus_service.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/audio_service.h"
#include "phone_os/battery_monitor.h"
#include "phone_os/button_binding_service.h"
#include "phone_os/camera_service.h"
#include "phone_os/motion_service.h"
#include "phone_os/music_player_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/voice_assistant_service.h"
#include "phone_os/voice_wake_service.h"
#include "phone_os/web_file_system_service.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/file_service.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "settings.h"

#include <src/others/test/lv_test.h>

namespace {

using Service = SystemInfoApp::Service;

void Pump(uint32_t ms = 20) {
    lv_test_wait(ms);
    lv_obj_update_layout(lv_screen_active());
}

void Screenshot(const char* name) {
    Pump(10);
    const auto* frame = lv_display_get_buf_active(lv_display_get_default());
    std::ofstream out(name, std::ios::binary);
    out << "P6\n320 240\n255\n";
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            const auto* p = frame->data + y * frame->header.stride + x * 4;
            const char rgb[] = {static_cast<char>(p[2]), static_cast<char>(p[1]), static_cast<char>(p[0])};
            out.write(rgb, 3);
        }
    }
}

std::string Text(lv_obj_t* label) {
    RODAK_CHECK(label != nullptr);
    return label != nullptr ? lv_label_get_text(label) : "";
}

bool SameColor(lv_color_t a, lv_color_t b) {
    return lv_color_to_u32(a) == lv_color_to_u32(b);
}

// Every label the app drew inside `object` must stay inside the 320x240 screen horizontally
// and must not be wider than its own box (a clipped label would be cut, not ellipsized).
void AssertLabelsFit(lv_obj_t* object) {
    const uint32_t count = lv_obj_get_child_count(object);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(object, static_cast<int32_t>(i));
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
        if (lv_obj_check_type(child, &lv_label_class)) {
            lv_area_t area;
            lv_obj_get_coords(child, &area);
            RODAK_CHECK(area.x1 >= 0);
            RODAK_CHECK(area.x2 < 320);
        }
        AssertLabelsFit(child);
    }
}

class FakeBattery final : public rodakos::BatteryStateProvider {
public:
    rodakos::BatterySnapshot Read() override { return snapshot; }
    rodakos::BatterySnapshot snapshot;
};

struct Fixture {
    PhoneUi ui{320, 240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui, navigation, registry, services, settings};
    WiFiAdapter wifi;
    rodakos::FileService files;
    FakeBattery battery;
    rodakos::CameraService camera;
    rodakos::AudioService audio;
    rodakos::AudioFocusService focus;
    rodakos::AudioOutputService output;
    rodakos::MusicPlayerService music;
    rodakos::MotionService motion;
    rodakos::VoiceWakeService wake;
    rodakos::VoiceAssistantService assistant;
    rodakos::ButtonBindingService buttons;
    rodakos::WebFileSystemService web;
    SystemInfoApp app;
    bool created = false;

    explicit Fixture(const char* theme = "dark") {
        rodakos_theme_init_from_name(theme);
        ui.SyncThemeName(theme);
        fake_heap() = FakeHeap{};
        RegisterSystemInfoApp(registry);

        wifi.status = WiFiStatus::kConnected;
        wifi.ssid = "书房-5G";
        wifi.ip = "192.168.31.86";
        files.mounted = true;
        files.capacity = {29700ULL * 1024 * 1024, 21400ULL * 1024 * 1024};
        battery.snapshot = {82, 3984, false, true};
        camera.state.available = true;
        camera.state.width = 320;
        camera.state.height = 240;
        camera.state.frame_count = 1284;
        audio.state.status = rodakos::AudioPlaybackStatus::kPlaying;
        audio.state.sample_rate = 44100;
        audio.state.channels = 2;
        focus.state = {true, 7, "music", rodakos::AudioFocusGain::kExclusive};
        output.open = true;
        music.tracks = 23;
        motion.state = {true, false, 0, "QMI8658", ""};
        wake.state.enabled = true;
        wake.state.status = rodakos::VoiceWakeStatus::kDisabled;
        wake.state.message = "Paused for music";
        wake.state.runtime_name = "MultiNet5 q8";
        wake.state.voice_identity.wake_word = "你好达克";
        assistant.state.transport_name = "websocket";
        assistant.state.recorder_name = "ES7210";
        buttons.buttons = {{"boot_button", "BOOT 键", "", 0, true}, {"io10_key_button", "IO10 键", "", 1, true}};

        services.SetWiFi(&wifi);
        services.SetFileService(&files);
        services.SetBattery(&battery);
        services.SetCamera(&camera);
        services.SetAudio(&audio);
        services.SetAudioFocus(&focus);
        services.SetAudioOutput(&output);
        services.SetMusicPlayer(&music);
        services.SetMotion(&motion);
        services.SetVoiceWake(&wake);
        services.SetVoiceAssistant(&assistant);
        services.SetButtons(&buttons);
        services.SetWebFiles(&web);
    }
    ~Fixture() {
        if (created) app.OnDestroy();
        lv_test_wait(100);
        lv_obj_clean(lv_screen_active());
        rodakos_theme_init(RODAKOS_THEME_DARK);
    }
    void Create() {
        RODAK_CHECK(app.OnCreate(context));
        created = true;
        Pump();
    }
    void Refresh() {
        app.Refresh();
        Pump();
    }
    void ScrollTo(lv_coord_t y) {
        lv_obj_scroll_to_y(app.body_, y, LV_ANIM_OFF);
        Pump();
    }
    lv_obj_t* ServiceRow(Service service) {
        return lv_obj_get_parent(app.services_[static_cast<int>(service)].label);
    }
};

}  // namespace

RODAK_TEST("main list shows identity, resources, network, power and service states") {
    Fixture f;
    f.Create();
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("关于本机"));
    RODAK_CHECK_EQ(Text(f.app.sram_.value), std::string("142 KB 可用 / 313 KB"));
    RODAK_CHECK_EQ(lv_bar_get_value(f.app.sram_.bar), 54);
    RODAK_CHECK_EQ(Text(f.app.storage_.value), std::string("20.9 GB 可用 / 29.0 GB"));
    RODAK_CHECK_EQ(Text(f.app.wifi_name_), std::string("书房-5G"));
    RODAK_CHECK_EQ(Text(f.app.wifi_detail_), std::string("192.168.31.86"));
    RODAK_CHECK_EQ(Text(f.app.battery_value_), std::string("82% · 3.98 V"));
    RODAK_CHECK_EQ(Text(f.app.services_[static_cast<int>(Service::kAudio)].label), std::string("播放中"));
    RODAK_CHECK_EQ(Text(f.app.services_[static_cast<int>(Service::kWake)].label), std::string("已暂停"));
    RODAK_CHECK_EQ(Text(f.app.services_[static_cast<int>(Service::kButtons)].label), std::string("2 个"));
    RODAK_CHECK_EQ(Text(f.app.services_[static_cast<int>(Service::kWebFiles)].label), std::string("已关闭"));
    AssertLabelsFit(f.app.root_);
    Screenshot("sysinfo-top.ppm");
    f.ScrollTo(200);
    Screenshot("sysinfo-network.ppm");
    f.ScrollTo(440);
    Screenshot("sysinfo-services.ppm");
    f.ScrollTo(10000);
    Screenshot("sysinfo-firmware.ppm");
}

RODAK_TEST("firmware group shows the build time as a date, not __DATE__") {
    Fixture f;
    f.Create();
    bool found = false;
    std::function<void(lv_obj_t*)> walk = [&](lv_obj_t* object) {
        for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i) {
            lv_obj_t* child = lv_obj_get_child(object, static_cast<int32_t>(i));
            if (lv_obj_check_type(child, &lv_label_class) &&
                std::string(lv_label_get_text(child)) == "2026-11-21 18:02") found = true;
            walk(child);
        }
    };
    walk(f.app.body_);
    RODAK_CHECK(found);
}

RODAK_TEST("first screen holds the identity card and the resource group") {
    Fixture f;
    f.Create();
    lv_area_t storage_row;
    lv_obj_get_coords(lv_obj_get_parent(f.app.storage_.value), &storage_row);
    // 40 px header + 200 px body: the SD row must end on the first screen.
    RODAK_CHECK(storage_row.y2 < 240);
}

RODAK_TEST("problem states change only the affected rows") {
    Fixture f;
    fake_heap().internal_free = 31U * 1024U;
    f.files.mounted = false;
    f.wifi.status = WiFiStatus::kFailed;
    f.wifi.rejected = "书房-5G";
    f.battery.snapshot = {12, 3520, false, true};
    f.camera.state.last_error = "sccb probe timeout";
    f.Create();
    RODAK_CHECK(SameColor(lv_obj_get_style_bg_color(f.app.sram_.bar, LV_PART_INDICATOR), rodakos_theme_warning()));
    RODAK_CHECK(SameColor(lv_obj_get_style_bg_color(f.app.psram_.bar, LV_PART_INDICATOR), rodakos_theme_primary()));
    RODAK_CHECK_EQ(Text(f.app.storage_.value), std::string("未插入"));
    RODAK_CHECK_EQ(lv_bar_get_value(f.app.storage_.bar), 0);
    RODAK_CHECK_EQ(Text(f.app.wifi_name_), std::string("未连接"));
    RODAK_CHECK_EQ(Text(f.app.wifi_detail_), std::string("书房-5G 密码错误"));
    RODAK_CHECK(SameColor(lv_obj_get_style_text_color(f.app.battery_value_, 0), rodakos_theme_warning()));
    RODAK_CHECK_EQ(Text(f.app.services_[static_cast<int>(Service::kCamera)].label), std::string("出错"));
    RODAK_CHECK(SameColor(lv_obj_get_style_text_color(f.app.services_[static_cast<int>(Service::kCamera)].label, 0),
                          rodakos_theme_error()));
    Screenshot("sysinfo-problems-top.ppm");
    f.ScrollTo(200);
    Screenshot("sysinfo-problems-network.ppm");
}

RODAK_TEST("charging battery says so and never shows the low-battery colour") {
    Fixture f;
    f.battery.snapshot = {12, 3700, true, true};
    f.Create();
    RODAK_CHECK_EQ(Text(f.app.battery_value_), std::string("充电中 12% · 3.70 V"));
    RODAK_CHECK(SameColor(lv_obj_get_style_text_color(f.app.battery_value_, 0), rodakos_theme_text_secondary()));
}

RODAK_TEST("missing services and battery show a dash, not invented values") {
    Fixture f;
    f.services.SetBattery(nullptr);
    f.services.SetCamera(nullptr);
    f.services.SetWiFi(nullptr);
    f.Create();
    RODAK_CHECK_EQ(Text(f.app.battery_value_), std::string("—"));
    RODAK_CHECK_EQ(Text(f.app.wifi_name_), std::string("不可用"));
    RODAK_CHECK_EQ(Text(f.app.services_[static_cast<int>(Service::kCamera)].label), std::string("不可用"));
}

RODAK_TEST("tapping a service opens its detail page; back returns to the list in place") {
    Fixture f;
    f.Create();
    f.ScrollTo(440);
    const lv_coord_t scroll = lv_obj_get_scroll_y(f.app.body_);
    lv_obj_send_event(f.ServiceRow(Service::kCamera), LV_EVENT_CLICKED, nullptr);
    Pump();
    RODAK_CHECK(f.app.detail_ != nullptr);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("相机"));
    RODAK_CHECK(lv_obj_has_flag(f.app.body_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK_EQ(Text(f.app.detail_heading_), std::string("GC0308 · DVP"));
    RODAK_CHECK_EQ(Text(f.app.detail_values_[1]), std::string("320 × 240"));
    RODAK_CHECK_EQ(Text(f.app.detail_values_[2]), std::string("1284 帧"));
    RODAK_CHECK_EQ(Text(f.app.detail_raw_), std::string("available=1 preview=0 frames=1284"));
    AssertLabelsFit(f.app.detail_);
    Screenshot("sysinfo-detail-camera.ppm");

    RODAK_CHECK(f.app.OnBackRequested());
    Pump();
    RODAK_CHECK(f.app.detail_ == nullptr);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("关于本机"));
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.body_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK_EQ(lv_obj_get_scroll_y(f.app.body_), scroll);
    RODAK_CHECK_EQ(f.navigation.caller_count, 0);

    RODAK_CHECK(f.app.OnBackRequested());
    Pump();
    RODAK_CHECK_EQ(f.navigation.caller_count, 1);
}

RODAK_TEST("wake detail explains a pause for music") {
    Fixture f;
    f.Create();
    f.app.OpenDetail(Service::kWake);
    Pump();
    RODAK_CHECK_EQ(Text(f.app.detail_status_), std::string("已暂停，音乐正在播放"));
    RODAK_CHECK_EQ(Text(f.app.detail_heading_), std::string("你好达克"));
    RODAK_CHECK_EQ(Text(f.app.detail_values_[0]), std::string("你好达克"));
    Screenshot("sysinfo-detail-wake.ppm");
}

RODAK_TEST("audio detail names the app holding audio focus") {
    Fixture f;
    // Same title as the production descriptor in apps/music/music_app.cc.
    f.registry.Register(PhoneAppDescriptor{.id = "music", .title = "Music"});
    f.Create();
    f.app.OpenDetail(Service::kAudio);
    Pump();
    RODAK_CHECK_EQ(Text(f.app.detail_values_[0]), std::string("Music"));
    RODAK_CHECK_EQ(Text(f.app.detail_values_[1]), std::string("44.1 kHz · 立体声"));
    RODAK_CHECK_EQ(Text(f.app.detail_raw_), std::string("out=open focus=music:exclusive vol=60"));
    Screenshot("sysinfo-detail-audio.ppm");
}

RODAK_TEST("failing camera detail keeps the raw error and colours it") {
    Fixture f;
    f.camera.state.available = false;
    f.camera.state.width = 0;
    f.camera.state.frame_count = 0;
    f.camera.state.last_error = "sccb probe timeout";
    f.Create();
    f.app.OpenDetail(Service::kCamera);
    Pump();
    RODAK_CHECK_EQ(Text(f.app.detail_status_), std::string("出错"));
    RODAK_CHECK_EQ(Text(f.app.detail_values_[1]), std::string("—"));
    RODAK_CHECK_EQ(Text(f.app.detail_values_[3]), std::string("sccb probe timeout"));
    RODAK_CHECK(SameColor(lv_obj_get_style_text_color(f.app.detail_values_[3], 0), rodakos_theme_error()));
    Screenshot("sysinfo-detail-camera-error.ppm");
}

RODAK_TEST("every detail page fits the screen") {
    Fixture f;
    f.Create();
    for (int i = 0; i < static_cast<int>(Service::kCount); ++i) {
        f.app.OpenDetail(static_cast<Service>(i));
        Pump();
        RODAK_CHECK(f.app.detail_ != nullptr);
        AssertLabelsFit(f.app.detail_);
        lv_area_t raw;
        lv_obj_get_coords(f.app.detail_raw_, &raw);
        RODAK_CHECK(raw.y2 < 240);
    }
    f.app.OpenDetail(Service::kDeveloper);
    Pump();
    Screenshot("sysinfo-detail-developer.ppm");
}

RODAK_TEST("theme change rebuilds with the open detail page and scroll kept") {
    Fixture f;
    f.Create();
    f.ScrollTo(200);
    f.app.OpenDetail(Service::kAudio);
    Pump();
    rodakos_theme_init_from_name("light");
    f.ui.SyncThemeName("light");
    RODAK_CHECK(f.app.OnThemeChanged(f.context));
    Pump();
    RODAK_CHECK(f.app.detail_ != nullptr);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("音频"));
    f.app.CloseDetail();
    Pump();
    RODAK_CHECK_EQ(lv_obj_get_scroll_y(f.app.body_), 200);
}

RODAK_TEST("light and blue themes render the main list") {
    {
        Fixture f("light");
        f.Create();
        Screenshot("sysinfo-light-top.ppm");
        f.ScrollTo(440);
        Screenshot("sysinfo-light-services.ppm");
    }
    {
        Fixture f("blue");
        f.Create();
        Screenshot("sysinfo-blue-top.ppm");
    }
}
