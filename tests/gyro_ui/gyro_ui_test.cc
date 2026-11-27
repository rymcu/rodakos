#include "test_framework.h"

#include <fstream>
#include <string>

#define private public
#include "apps/gyro/gyro_app.h"
#undef private
#include "esp_heap_caps.h"
#include "phone_os/motion_service.h"
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
    Pump(300);
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

void Click(lv_obj_t* obj) {
    RODAK_CHECK(obj != nullptr);
    lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
    Pump();
}

// Pixel colour at (x, y) on the test display, as 0xRRGGBB.
uint32_t PixelAt(int x, int y) {
    const auto* frame = lv_display_get_buf_active(lv_display_get_default());
    const auto* p = frame->data + y * frame->header.stride + x * 4;
    return (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[0];
}

class ScriptedSensor final : public rodakos::MotionSensorBackend {
public:
    const char* name() const override { return "QMI8658"; }
    bool Start() override { ++starts; return start_ok; }
    void Stop() override { ++stops; }
    bool Read(rodakos::MotionSample& out) override {
        ++reads;
        if (!read_ok) return false;
        out = sample;
        return true;
    }
    const char* last_error() const override { return read_ok ? "" : "I2C read failed"; }

    rodakos::MotionSample sample;
    bool start_ok = true;
    bool read_ok = true;
    int starts = 0;
    int stops = 0;
    int reads = 0;
};

struct Fixture {
    PhoneUi ui{320, 240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui, navigation, registry, services, settings};
    ScriptedSensor sensor;
    rodakos::MotionService motion{&sensor};
    GyroApp app;
    bool created = false;

    explicit Fixture(const char* theme = "dark") {
        rodakos_theme_init_from_name(theme);
        ui.SyncThemeName(theme);
        gyro_test::fail_malloc = false;
        Tilt(0.3f, -0.4f, 0.0f);
        services.SetMotion(&motion);
    }
    ~Fixture() {
        if (created) app.OnDestroy();
        lv_test_wait(2000);
        lv_obj_clean(lv_screen_active());
        rodakos_theme_init(RODAKOS_THEME_DARK);
    }
    void Tilt(float roll, float pitch, float yaw, rodakos::MotionVector gyro = {0.1f, -0.2f, 0.0f},
              rodakos::MotionVector accel = {0.01f, 0.01f, 1.0f}) {
        sensor.sample.roll_deg = roll;
        sensor.sample.pitch_deg = pitch;
        sensor.sample.yaw_deg = yaw;
        sensor.sample.gyro_dps = gyro;
        sensor.sample.accel_g = accel;
    }
    void Create() {
        RODAK_CHECK(app.OnCreate(context));
        created = true;
        Pump();
    }
    void Tick() {
        app.Refresh();
        Pump();
    }
};

}  // namespace

RODAK_TEST("level page reads 已水平 in green within the tolerance") {
    Fixture f;
    f.Create();
    RODAK_CHECK(f.app.view_ == GyroApp::View::kLevel);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("陀螺仪"));
    RODAK_CHECK_EQ(Text(f.app.state_label_), std::string("已水平"));
    RODAK_CHECK_EQ(Text(f.app.angle_values_[0]), std::string("+0.3°"));
    RODAK_CHECK_EQ(Text(f.app.angle_values_[1]), std::string("-0.4°"));
    RODAK_CHECK_EQ(Text(f.app.angle_values_[2]), std::string("+0°"));
    RODAK_CHECK(lv_color_to_u32(lv_obj_get_style_text_color(f.app.angle_values_[0], 0)) ==
                lv_color_to_u32(rodakos_theme_success()));
    RODAK_CHECK_EQ(Text(f.app.summary_label_), std::string("ω 0.2 °/s · 1.00 g"));
    RODAK_CHECK_FALSE(Visible(f.app.relative_tag_));
    lv_area_t area;
    lv_obj_get_coords(f.app.attitude_canvas_, &area);
    RODAK_CHECK_EQ(lv_area_get_width(&area), GyroApp::kCanvasSize);
    RODAK_CHECK_EQ(lv_area_get_height(&area), GyroApp::kCanvasSize);
    Screenshot("gyro-level.ppm");
    // The bubble sits on the centre: the centre pixel is the bubble's accent (success) colour
    // ring around a background-coloured dot, not the dial background.
    const uint32_t ring = PixelAt(10 + 77 + 8, 48 + 77);
    RODAK_CHECK(ring != PixelAt(10 + 77 + 40, 48 + 77));
}

RODAK_TEST("tilting moves the bubble and reads 倾斜中") {
    Fixture f;
    f.Tilt(18.4f, -9.2f, 42.0f, {14.2f, -8.6f, 20.5f}, {0.16f, 0.32f, 0.95f});
    f.Create();
    RODAK_CHECK_EQ(Text(f.app.state_label_), std::string("倾斜中"));
    RODAK_CHECK_EQ(Text(f.app.angle_values_[0]), std::string("+18.4°"));
    RODAK_CHECK_EQ(Text(f.app.angle_values_[1]), std::string("-9.2°"));
    RODAK_CHECK_EQ(Text(f.app.angle_values_[2]), std::string("+42°"));
    RODAK_CHECK(lv_color_to_u32(lv_obj_get_style_text_color(f.app.angle_values_[0], 0)) ==
                lv_color_to_u32(rodakos_theme_text_primary()));
    Screenshot("gyro-tilted.ppm");
}

RODAK_TEST("归零 makes the current attitude zero and 复位 returns to absolute angles") {
    Fixture f;
    f.Tilt(12.0f, 3.0f, 54.0f);
    f.Create();
    Click(f.app.zero_button_);
    RODAK_CHECK(f.app.zeroed_);
    RODAK_CHECK_EQ(Text(f.app.zero_label_), std::string("复位"));
    RODAK_CHECK(Visible(f.app.relative_tag_));
    RODAK_CHECK_EQ(Text(f.app.state_label_), std::string("已水平"));
    RODAK_CHECK_EQ(Text(f.app.angle_values_[0]), std::string("+0.0°"));

    f.Tilt(24.5f, 5.0f, 54.0f);
    f.Tick();
    RODAK_CHECK_EQ(Text(f.app.angle_values_[0]), std::string("+12.5°"));
    RODAK_CHECK_EQ(Text(f.app.angle_values_[1]), std::string("+2.0°"));
    RODAK_CHECK_EQ(Text(f.app.angle_values_[2]), std::string("+0°"));

    // Yaw wraps across ±180 relative to the zero point.
    f.Tilt(24.5f, 5.0f, -170.0f);
    f.Tick();
    RODAK_CHECK_EQ(Text(f.app.angle_values_[2]), std::string("+136°"));

    Click(f.app.zero_button_);
    RODAK_CHECK_FALSE(f.app.zeroed_);
    RODAK_CHECK_EQ(Text(f.app.zero_label_), std::string("归零"));
    RODAK_CHECK_EQ(Text(f.app.angle_values_[0]), std::string("+24.5°"));
}

RODAK_TEST("定格 holds the reading until 继续") {
    Fixture f;
    f.Tilt(24.5f, 5.0f, 54.0f);
    f.Create();
    Click(f.app.freeze_button_);
    RODAK_CHECK(f.app.frozen_);
    RODAK_CHECK_EQ(Text(f.app.freeze_label_), std::string("继续"));
    RODAK_CHECK_EQ(Text(f.app.state_label_), std::string("已定格"));
    f.Tilt(-30.0f, 10.0f, 0.0f);
    f.Tick();
    RODAK_CHECK_EQ(Text(f.app.angle_values_[0]), std::string("+24.5°"));
    // Even a failing sensor keeps the frozen reading on screen.
    f.sensor.read_ok = false;
    f.Tick();
    RODAK_CHECK(f.app.view_ == GyroApp::View::kLevel);
    RODAK_CHECK_EQ(Text(f.app.angle_values_[0]), std::string("+24.5°"));
    Screenshot("gyro-frozen.ppm");
    f.sensor.read_ok = true;
    Click(f.app.freeze_button_);
    RODAK_CHECK_EQ(Text(f.app.angle_values_[0]), std::string("-30.0°"));
}

RODAK_TEST("six-axis page shows signed values and centred bars, shares the freeze state") {
    Fixture f;
    f.Tilt(18.4f, -9.2f, 42.0f, {14.2f, -8.6f, 120.0f}, {-0.24f, 0.56f, 0.82f});
    f.Create();
    Click(f.app.action_button_);
    RODAK_CHECK(f.app.view_ == GyroApp::View::kAxes);
    RODAK_CHECK_EQ(Text(f.app.title_label_), std::string("六轴数据"));
    RODAK_CHECK_EQ(Text(f.app.gyro_bars_[0].value), std::string("+14.2"));
    RODAK_CHECK_EQ(Text(f.app.gyro_bars_[1].value), std::string("-8.6"));
    RODAK_CHECK_EQ(Text(f.app.accel_bars_[0].value), std::string("-0.24"));
    RODAK_CHECK_EQ(Text(f.app.accel_bars_[2].value), std::string("+0.82"));
    // Negative values grow left from the centre line; a full-scale value is warning-coloured.
    RODAK_CHECK(lv_obj_get_x(f.app.gyro_bars_[1].fill) < 42);
    RODAK_CHECK_EQ(lv_obj_get_x(f.app.gyro_bars_[0].fill), 42);
    RODAK_CHECK_EQ(lv_obj_get_width(f.app.gyro_bars_[2].fill), 42);
    RODAK_CHECK(lv_color_to_u32(lv_obj_get_style_bg_color(f.app.gyro_bars_[2].fill, 0)) ==
                lv_color_to_u32(rodakos_theme_warning()));
    RODAK_CHECK(Text(f.app.axes_info_label_).find("QMI8658 · 10 Hz") == 0);
    Screenshot("gyro-axes.ppm");

    Click(f.app.action_button_);
    RODAK_CHECK(f.app.frozen_);
    RODAK_CHECK(Text(f.app.axes_info_label_).find("已定格") == 0);
    RODAK_CHECK(f.app.OnBackRequested());
    RODAK_CHECK(f.app.view_ == GyroApp::View::kLevel);
    RODAK_CHECK_EQ(Text(f.app.freeze_label_), std::string("继续"));
    RODAK_CHECK(f.app.OnBackRequested());
    Pump();
    RODAK_CHECK_EQ(f.navigation.home_count, 1);
}

RODAK_TEST("a sensor that will not start shows the error page and 重试 recovers") {
    Fixture f;
    f.sensor.start_ok = false;
    f.Create();
    RODAK_CHECK(f.app.view_ == GyroApp::View::kError);
    RODAK_CHECK(Visible(f.app.error_view_));
    RODAK_CHECK_FALSE(Visible(f.app.level_view_));
    RODAK_CHECK_FALSE(Visible(f.app.action_button_));
    RODAK_CHECK_EQ(Text(f.app.error_hint_label_), std::string("未检测到 QMI8658 或读取失败"));
    Screenshot("gyro-error.ppm");

    auto* retry = lv_obj_get_child(f.app.error_view_, -1);
    Click(retry);
    RODAK_CHECK(f.app.view_ == GyroApp::View::kError);
    f.sensor.start_ok = true;
    Click(retry);
    RODAK_CHECK(f.app.view_ == GyroApp::View::kLevel);
    RODAK_CHECK_EQ(f.sensor.starts, 3);
}

RODAK_TEST("a read failure mid-session switches to the error page and back") {
    Fixture f;
    f.Create();
    Click(f.app.action_button_);
    f.sensor.read_ok = false;
    f.Tick();
    RODAK_CHECK(f.app.view_ == GyroApp::View::kError);
    RODAK_CHECK_EQ(Text(f.app.error_hint_label_), std::string("读取失败，请重试"));
    f.sensor.read_ok = true;
    f.Tick();
    RODAK_CHECK(f.app.view_ == GyroApp::View::kAxes);
}

RODAK_TEST("the dial follows the theme and survives a canvas allocation failure") {
    {
        Fixture f("light");
        f.Tilt(18.4f, -9.2f, 42.0f);
        f.Create();
        // Corner of the canvas is painted with the page background, not a fixed dark colour.
        RODAK_CHECK_EQ(PixelAt(12, 50) & 0xF8FCF8, lv_color_to_u32(rodakos_theme_bg_primary()) & 0xF8FCF8);
        Screenshot("gyro-light.ppm");
        rodakos_theme_init_from_name("blue");
        f.ui.SyncThemeName("blue");
        RODAK_CHECK(f.app.OnThemeChanged(f.context));
        Pump();
        RODAK_CHECK_EQ(Text(f.app.angle_values_[0]), std::string("+18.4°"));
    }
    {
        Fixture f;
        gyro_test::fail_malloc = true;
        f.Create();
        RODAK_CHECK(f.app.attitude_canvas_ == nullptr);
        RODAK_CHECK_EQ(Text(f.app.angle_values_[0]), std::string("+0.3°"));
        gyro_test::fail_malloc = false;
    }
}

RODAK_TEST("destroy stops the motion service it started") {
    Fixture f;
    f.Create();
    f.app.OnDestroy();
    f.created = false;
    RODAK_CHECK_EQ(f.sensor.stops, 1);
}
