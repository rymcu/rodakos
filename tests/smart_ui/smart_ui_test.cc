#include "test_framework.h"

#include <fstream>
#include <string>

#define private public
#include "apps/smart/smart_app.h"
#undef private
#include "phone_os/light_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "settings.h"

#include <src/others/test/lv_test.h>

namespace {

void Pump(uint32_t ms = 20) {
    lv_test_wait(ms);
    lv_obj_update_layout(lv_screen_active());
}

void Screenshot(const char* name) {
    // Let the switch/state style transitions finish so the capture shows the resting colours.
    Pump(400);
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

bool Visible(lv_obj_t* obj) {
    return obj != nullptr && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

// The toast is the last child the UI adds to the screen.
std::string LastToast() {
    auto* screen = lv_screen_active();
    const uint32_t count = lv_obj_get_child_count(screen);
    for (int32_t i = static_cast<int32_t>(count) - 1; i >= 0; --i) {
        auto* child = lv_obj_get_child(screen, i);
        if (lv_obj_check_type(child, &lv_label_class)) return lv_label_get_text(child);
        if (lv_obj_get_child_count(child) == 1 &&
            lv_obj_check_type(lv_obj_get_child(child, 0), &lv_label_class)) {
            return lv_label_get_text(lv_obj_get_child(child, 0));
        }
    }
    return "";
}

void Click(lv_obj_t* obj) {
    RODAK_CHECK(obj != nullptr);
    lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
    Pump();
}

rodakos::LightState BoardRgb(bool enabled = true) {
    rodakos::LightState state;
    state.id = "board_rgb";
    state.title = "Board RGB";
    state.enabled = enabled;
    state.brightness_percent = 60;
    state.color = {255, 220, 169};
    return state;
}

struct Fixture {
    PhoneUi ui{320, 240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui, navigation, registry, services, settings};
    rodakos::LightService lights;
    SmartApp app;
    bool created = false;

    explicit Fixture(const char* theme = "dark") {
        rodakos_theme_init_from_name(theme);
        ui.SyncThemeName(theme);
        lights.lights = {BoardRgb()};
        lights.overlay = {false};
        services.SetLights(&lights);
    }
    ~Fixture() {
        if (created) app.OnDestroy();
        lv_test_wait(2000);
        lv_obj_clean(lv_screen_active());
        rodakos_theme_init(RODAKOS_THEME_DARK);
    }
    void Create() {
        RODAK_CHECK(app.OnCreate(context));
        created = true;
        Pump();
    }
    void AddLights() {
        rodakos::LightState desk;
        desk.id = "desk";
        desk.title = "桌面灯带";
        desk.enabled = true;
        desk.brightness_percent = 80;
        desk.color = {85, 184, 255};
        rodakos::LightState bed;
        bed.id = "bed";
        bed.title = "床头氛围灯";
        bed.enabled = false;
        bed.color = {200, 85, 255};
        lights.lights = {BoardRgb(), desk, bed};
        lights.overlay = {false, false, false};
    }
};

}  // namespace

RODAK_TEST("single board light opens the control page with a Chinese name and live state") {
    Fixture f;
    f.Create();
    RODAK_CHECK(f.app.view_ == SmartApp::View::kControl);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("智能家居"));
    RODAK_CHECK_EQ(Text(f.app.name_label_), std::string("板载彩灯"));
    RODAK_CHECK_EQ(Text(f.app.status_label_), std::string("已开启 · 60%"));
    RODAK_CHECK_EQ(Text(f.app.brightness_value_), std::string("60%"));
    RODAK_CHECK_EQ(Text(f.app.color_name_label_), std::string("暖白"));
    RODAK_CHECK(lv_obj_has_state(f.app.power_switch_, LV_STATE_CHECKED));
    RODAK_CHECK(Visible(f.app.preset_checks_[0]));
    for (size_t i = 1; i < SmartApp::kPresetCount; ++i) RODAK_CHECK_FALSE(Visible(f.app.preset_checks_[i]));
    Screenshot("smart-on.ppm");
}

RODAK_TEST("tapping the hero row toggles power through a single enabled patch") {
    Fixture f;
    f.Create();
    Click(f.app.hero_);
    RODAK_CHECK_EQ(f.lights.patches.size(), 1u);
    const auto& patch = f.lights.patches.back();
    RODAK_CHECK_EQ(patch.id, std::string("board_rgb"));
    RODAK_CHECK(patch.patch.enabled.has_value());
    RODAK_CHECK_FALSE(*patch.patch.enabled);
    RODAK_CHECK_FALSE(patch.patch.brightness_percent.has_value());
    RODAK_CHECK_FALSE(patch.patch.color.has_value());
    RODAK_CHECK_EQ(Text(f.app.status_label_), std::string("已关闭"));
    RODAK_CHECK_FALSE(lv_obj_has_state(f.app.power_switch_, LV_STATE_CHECKED));
    Screenshot("smart-off.ppm");
}

RODAK_TEST("choosing a colour while the light is off saves the colour and keeps it off") {
    Fixture f;
    f.lights.lights[0].enabled = false;
    f.Create();
    Click(f.app.presets_[1]);
    RODAK_CHECK_EQ(f.lights.patches.size(), 1u);
    const auto& patch = f.lights.patches.back().patch;
    RODAK_CHECK(patch.color.has_value());
    RODAK_CHECK_FALSE(patch.enabled.has_value());
    RODAK_CHECK_FALSE(f.lights.lights[0].enabled);
    RODAK_CHECK_EQ(f.lights.lights[0].color.red, 85);
    RODAK_CHECK_EQ(Text(f.app.color_name_label_), std::string("冰蓝"));
    RODAK_CHECK(Visible(f.app.preset_checks_[1]));
    RODAK_CHECK_EQ(LastToast(), std::string("已保存颜色，开灯后生效"));
}

RODAK_TEST("choosing a colour while the light is on changes colour without a toast") {
    Fixture f;
    f.Create();
    Click(f.app.presets_[4]);
    RODAK_CHECK_EQ(f.lights.patches.size(), 1u);
    RODAK_CHECK_FALSE(f.lights.patches.back().patch.enabled.has_value());
    RODAK_CHECK(f.lights.lights[0].enabled);
    RODAK_CHECK_EQ(Text(f.app.color_name_label_), std::string("珊瑚红"));
}

RODAK_TEST("dragging brightness previews the value and commits once on release") {
    Fixture f;
    f.Create();
    for (int value : {40, 30, 25}) {
        lv_slider_set_value(f.app.brightness_slider_, value, LV_ANIM_OFF);
        lv_obj_send_event(f.app.brightness_slider_, LV_EVENT_VALUE_CHANGED, nullptr);
        // The 500 ms poll must not snap the slider back while the finger is down.
        f.app.Refresh();
    }
    RODAK_CHECK(f.lights.patches.empty());
    RODAK_CHECK_EQ(Text(f.app.brightness_value_), std::string("25%"));
    RODAK_CHECK_EQ(lv_slider_get_value(f.app.brightness_slider_), 25);

    lv_obj_send_event(f.app.brightness_slider_, LV_EVENT_RELEASED, nullptr);
    Pump();
    RODAK_CHECK_EQ(f.lights.patches.size(), 1u);
    const auto& patch = f.lights.patches.back();
    RODAK_CHECK_EQ(patch.id, std::string("board_rgb"));
    RODAK_CHECK_EQ(*patch.patch.brightness_percent, 25);
    RODAK_CHECK_FALSE(patch.patch.enabled.has_value());
    RODAK_CHECK_EQ(Text(f.app.status_label_), std::string("已开启 · 25%"));
}

RODAK_TEST("brightness commit targets the light the drag started on") {
    Fixture f;
    f.AddLights();
    f.Create();
    Click(f.app.tiles_[1].card);
    lv_slider_set_value(f.app.brightness_slider_, 33, LV_ANIM_OFF);
    lv_obj_send_event(f.app.brightness_slider_, LV_EVENT_VALUE_CHANGED, nullptr);
    f.app.current_index_ = 2;
    lv_obj_send_event(f.app.brightness_slider_, LV_EVENT_RELEASED, nullptr);
    Pump();
    RODAK_CHECK_EQ(f.lights.patches.size(), 1u);
    RODAK_CHECK_EQ(f.lights.patches.back().id, std::string("desk"));
}

RODAK_TEST("refresh picks up changes made outside the app and shows the voice overlay") {
    Fixture f;
    f.Create();
    f.lights.lights[0].brightness_percent = 15;
    f.lights.lights[0].color = {200, 85, 255};
    f.lights.overlay[0] = true;
    f.app.Refresh();
    Pump();
    RODAK_CHECK_EQ(Text(f.app.status_label_), std::string("语音状态灯使用中"));
    RODAK_CHECK_EQ(Text(f.app.brightness_value_), std::string("15%"));
    RODAK_CHECK_EQ(Text(f.app.color_name_label_), std::string("紫罗兰"));
    RODAK_CHECK_EQ(lv_obj_get_style_border_width(f.app.orb_, 0), 2);
    Screenshot("smart-overlay.ppm");
}

RODAK_TEST("a rejected write keeps the accepted state and says it is unconfirmed") {
    Fixture f;
    f.Create();
    f.lights.fail_writes = true;
    Click(f.app.hero_);
    RODAK_CHECK(f.lights.lights[0].enabled);
    RODAK_CHECK_EQ(Text(f.app.status_label_), std::string("驱动异常，点按重试"));
    RODAK_CHECK_EQ(LastToast(), std::string("灯光状态未确认"));
    Screenshot("smart-error.ppm");
}

RODAK_TEST("several lights open the overview; back from a light returns to it") {
    Fixture f;
    f.AddLights();
    f.Create();
    RODAK_CHECK(f.app.view_ == SmartApp::View::kOverview);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("智能家居 · 3"));
    RODAK_CHECK_EQ(Text(f.app.tiles_[0].name), std::string("板载彩灯"));
    RODAK_CHECK_EQ(Text(f.app.tiles_[1].status), std::string("已开启 · 80%"));
    RODAK_CHECK_EQ(Text(f.app.tiles_[2].status), std::string("已关闭"));
    RODAK_CHECK_FALSE(Visible(f.app.tiles_[3].card));
    Screenshot("smart-overview.ppm");

    Click(f.app.tiles_[2].power);
    RODAK_CHECK_EQ(f.lights.patches.back().id, std::string("bed"));
    RODAK_CHECK(*f.lights.patches.back().patch.enabled);
    RODAK_CHECK(f.app.view_ == SmartApp::View::kOverview);

    Click(f.app.tiles_[1].card);
    RODAK_CHECK(f.app.view_ == SmartApp::View::kControl);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("桌面灯带"));
    RODAK_CHECK(f.app.OnBackRequested());
    RODAK_CHECK(f.app.view_ == SmartApp::View::kOverview);
    RODAK_CHECK_FALSE(f.app.OnBackRequested());
}

RODAK_TEST("no lights shows the empty state") {
    Fixture f;
    f.lights.lights.clear();
    f.lights.overlay.clear();
    f.Create();
    RODAK_CHECK(f.app.view_ == SmartApp::View::kEmpty);
    RODAK_CHECK(Visible(f.app.empty_));
    RODAK_CHECK_FALSE(Visible(f.app.control_));
    Screenshot("smart-empty.ppm");
}

RODAK_TEST("light theme and theme change rebuild the page with the same state") {
    Fixture f("light");
    f.Create();
    Screenshot("smart-light-theme.ppm");
    rodakos_theme_init_from_name("blue");
    f.ui.SyncThemeName("blue");
    RODAK_CHECK(f.app.OnThemeChanged(f.context));
    Pump();
    RODAK_CHECK_EQ(Text(f.app.name_label_), std::string("板载彩灯"));
    RODAK_CHECK_EQ(Text(f.app.status_label_), std::string("已开启 · 60%"));
    Screenshot("smart-blue-theme.ppm");
}
