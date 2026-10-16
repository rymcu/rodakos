#include "test_framework.h"
#include "framebuffer_test_helpers.h"

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#define private public
#include "apps/themes/themes_app.h"
#undef private
#include "phone_os/appearance_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "phone_ui/theme_presets.h"
#include "settings.h"

#include <src/others/test/lv_test.h>

namespace {

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
    return lv_label_get_text(label);
}

void AssertInsideScreen(lv_obj_t* object) {
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    RODAK_CHECK(area.x1 >= 0 && area.y1 >= 0);
    RODAK_CHECK(area.x2 < 320 && area.y2 < 240);
}

// Holds a 320x240 RGB565 wallpaper in a solid color, like the PhoneUi boot wallpaper.
std::shared_ptr<uint8_t> SolidWallpaper(uint16_t rgb565) {
    std::shared_ptr<uint8_t> data(new uint8_t[320 * 240 * 2], std::default_delete<uint8_t[]>());
    for (size_t i = 0; i < 320 * 240; ++i) {
        data.get()[i * 2] = static_cast<uint8_t>(rgb565 & 0xff);
        data.get()[i * 2 + 1] = static_cast<uint8_t>(rgb565 >> 8);
    }
    return data;
}

struct Fixture {
    PhoneUi ui{320, 240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui, navigation, registry, services, settings};
    rodakos::AppearanceService appearance;
    ThemesApp app;
    bool created = false;
    bool remote_input = false;

    explicit Fixture(bool with_service = true) {
        rodakos_theme_init(RODAKOS_THEME_DARK);
        ui.SyncThemeName("dark");
        if (with_service) services.SetAppearance(&appearance);
        // Mirrors main.cc: physical input is the touch indev while remote input is disabled.
        ui.SetPrimaryInput(lv_test_indev_get_indev(LV_INDEV_TYPE_POINTER));
        ui.SetPhysicalInputCallback([](void* data) {
            return !*static_cast<bool*>(data);
        }, &remote_input);
        lv_test_mouse_release();
        lv_test_wait(60);
    }
    ~Fixture() {
        if (created) app.OnDestroy();
        ui.SetWallpaper({}, 0, 0);
        // Drain toasts and async callbacks while the fixture is alive.
        lv_test_wait(1900);
        lv_obj_clean(lv_screen_active());
        rodakos_theme_init(RODAKOS_THEME_DARK);
    }
    void Create() {
        RODAK_CHECK(app.OnCreate(context));
        created = true;
        Pump();
    }
    void Refresh() {
        app.RefreshStatus();
        Pump();
    }
    void Click(lv_obj_t* object) {
        RODAK_CHECK(object != nullptr);
        lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
        Pump();
    }
    // Drives the real LVGL pointer, so PRESSED/CLICKED carry lv_indev_active().
    void Touch(lv_obj_t* object) {
        RODAK_CHECK(object != nullptr);
        lv_obj_update_layout(lv_screen_active());
        lv_area_t area;
        lv_obj_get_coords(object, &area);
        lv_test_mouse_click_at((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
        Pump();
    }
    void OpenTrust() {
        Click(lv_obj_get_parent(app.trust_label_));
        RODAK_CHECK(app.current_page_ == ThemesPage::kTrust);
        RODAK_CHECK(app.trust_body_ != nullptr);
    }
    void SetPublisher(const char* key, const char* fingerprint, bool trusted = false) {
        appearance.publisher.key_id = key;
        appearance.publisher.fingerprint = fingerprint;
        appearance.publisher.public_key = "-----BEGIN PUBLIC KEY-----";
        appearance.publisher.trusted = trusted;
        appearance.publisher.loading = false;
        appearance.publisher.error.clear();
    }
};

constexpr const char* kKeyA = "a3c1f0e2b4d6a8c0e2f4a6b8c0d2e4f6a8b0c2d4e6f8a0b2c4d6e8f0a2b4c6d8";
constexpr const char* kKeyB = "b7e5d3c1a9f7e5d3c1b9a7f5e3d1c9b7a5f3e1d9c7b5a3f1e9d7c5b3a1f9e7d5";
constexpr const char* kFingerprintA = "A3C1 F0E2 B4D6 A8C0 E2F4 A6B8 C0D2 E4F6";
constexpr const char* kFingerprintB = "B7E5 D3C1 A9F7 E5D3 C1B9 A7F5 E3D1 C9B7";

}  // namespace

RODAK_TEST("status text covers every AppearanceService status") {
    rodakos::AppearanceStatusSnapshot s;
    s.status = "idle";
    RODAK_CHECK_EQ(ThemesStatusText(s), std::string("已生效"));
    s.status = "applied";
    RODAK_CHECK_EQ(ThemesStatusText(s), std::string("已生效"));
    s.status = "awaiting_trust";
    RODAK_CHECK_EQ(ThemesStatusText(s), std::string("需要确认发布端"));
    s.status = "waiting_device";
    RODAK_CHECK_EQ(ThemesStatusText(s), std::string("等待下载"));
    s.error = "ticket_failed";
    RODAK_CHECK_EQ(ThemesStatusText(s), std::string("等待重试：ticket_failed"));
    s.error.clear();
    s.status = "deferred_busy";
    RODAK_CHECK_EQ(ThemesStatusText(s), std::string("设备忙，稍后下载"));
    s.status = "downloading";
    s.progress = 42;
    RODAK_CHECK_EQ(ThemesStatusText(s), std::string("下载中 42%"));
    s.status = "pending_reboot";
    RODAK_CHECK_EQ(ThemesStatusText(s), std::string("重启后生效"));
    s.status = "failed";
    RODAK_CHECK_EQ(ThemesStatusText(s), std::string("失败：unknown"));
    s.error = "sd_full";
    RODAK_CHECK_EQ(ThemesStatusText(s), std::string("失败：sd_full"));
    s.status = "fallback";
    s.fallback_reason = "pending_package_invalid";
    RODAK_CHECK_EQ(ThemesStatusText(s), std::string("已回退：pending_package_invalid"));
    s.status = "superseded";
    RODAK_CHECK_EQ(ThemesStatusText(s), std::string("已被新版本取代"));
}

RODAK_TEST("theme presets resolve ids and default primaries without changing the theme") {
    rodakos_theme_init(RODAKOS_THEME_DARK);
    RODAK_CHECK_EQ(ThemePresetIndex("light"), 0);
    RODAK_CHECK_EQ(ThemePresetIndex("green"), 3);
    RODAK_CHECK_EQ(ThemePresetIndex("custom"), -1);
    RODAK_CHECK_EQ(rodakos_theme_preset_primary(RODAKOS_THEME_DARK), 0x79CBFFU);
    RODAK_CHECK_EQ(rodakos_theme_preset_primary(RODAKOS_THEME_LIGHT), 0x2196F3U);
    RODAK_CHECK_EQ(rodakos_theme_preset_primary(RODAKOS_THEME_BLUE), 0x1976D2U);
    RODAK_CHECK_EQ(rodakos_theme_preset_primary(RODAKOS_THEME_GREEN), 0x388E3CU);
    RODAK_CHECK_EQ(rodakos_theme_preset_primary(RODAKOS_THEME_CUSTOM), 0x79CBFFU);
    RODAK_CHECK_EQ(rodakos_theme_get()->primary, 0x79CBFFU);
}

RODAK_TEST("built-in appearance shows local colors, applied state and trust row") {
    Fixture f;
    f.appearance.snapshot.status = "idle";
    f.appearance.snapshot.theme_local = true;
    f.Create();
    RODAK_CHECK_EQ(Text(f.app.source_label_), std::string("配色：本地"));
    RODAK_CHECK_EQ(Text(f.app.revision_label_), std::string("内置外观"));
    RODAK_CHECK_EQ(Text(f.app.status_label_), std::string("已生效"));
    RODAK_CHECK_EQ(Text(f.app.trust_label_), std::string("发布端：未信任"));
    RODAK_CHECK_EQ(lv_obj_get_style_border_width(f.app.preset_buttons_[1], 0), 2);
    RODAK_CHECK_EQ(lv_obj_get_style_border_width(f.app.preset_buttons_[0], 0), 1);
    AssertInsideScreen(f.app.trust_label_);
    for (auto* button : f.app.preset_buttons_) AssertInsideScreen(button);
    Screenshot("themes-builtin.ppm");
}

RODAK_TEST("remote package with a pending revision shows both revisions and no selected preset") {
    Fixture f;
    f.appearance.snapshot.status = "pending_reboot";
    f.appearance.snapshot.active_revision = 4;
    f.appearance.snapshot.pending_revision = 5;
    f.appearance.snapshot.theme_local = false;
    f.appearance.snapshot.publisher_trusted = true;
    f.Create();
    RODAK_CHECK_EQ(Text(f.app.source_label_), std::string("配色：外观包"));
    RODAK_CHECK_EQ(Text(f.app.revision_label_), std::string("外观包 r4 → r5"));
    RODAK_CHECK_EQ(Text(f.app.status_label_), std::string("重启后生效"));
    RODAK_CHECK_EQ(Text(f.app.trust_label_), std::string("发布端：已信任"));
    for (auto* button : f.app.preset_buttons_) {
        RODAK_CHECK_EQ(lv_obj_get_style_border_width(button, 0), 1);
    }
    Screenshot("themes-pending.ppm");
}

RODAK_TEST("status timer picks up download progress and failure without reopening the app") {
    Fixture f;
    f.appearance.snapshot.status = "downloading";
    f.appearance.snapshot.progress = 10;
    f.appearance.snapshot.active_revision = 2;
    f.Create();
    RODAK_CHECK_EQ(Text(f.app.status_label_), std::string("下载中 10%"));
    f.appearance.snapshot.progress = 70;
    lv_test_wait(800);
    RODAK_CHECK_EQ(Text(f.app.status_label_), std::string("下载中 70%"));
    f.appearance.snapshot.status = "failed";
    f.appearance.snapshot.error = "package_hash_invalid";
    lv_test_wait(800);
    RODAK_CHECK_EQ(Text(f.app.status_label_), std::string("失败：package_hash_invalid"));
    RODAK_CHECK(lv_color_eq(lv_obj_get_style_text_color(f.app.status_label_, 0), rodakos_theme_warning()));

    // The longest code the service emits stays inside the preview card.
    f.appearance.snapshot.status = "waiting_device";
    f.appearance.snapshot.error = "partial_metadata_failed_retry_exhausted";
    f.Refresh();
    lv_area_t status_area;
    lv_area_t card_area;
    lv_obj_get_coords(f.app.status_label_, &status_area);
    lv_obj_get_coords(lv_obj_get_parent(f.app.status_label_), &card_area);
    RODAK_CHECK(status_area.y2 <= card_area.y2);
    RODAK_CHECK(status_area.x2 <= card_area.x2);
    f.appearance.snapshot.status = "failed";
    f.appearance.snapshot.error = "package_hash_invalid";
    f.Refresh();
    Screenshot("themes-failed.ppm");
}

RODAK_TEST("selecting a preset saves the override, applies colors and refreshes asynchronously") {
    Fixture f;
    f.appearance.snapshot.status = "applied";
    f.appearance.snapshot.active_revision = 3;
    f.Create();
    f.Click(f.app.preset_buttons_[2]);
    RODAK_CHECK_EQ(f.appearance.set_calls, 1);
    RODAK_CHECK_EQ(f.appearance.local_preset, std::string("blue"));
    RODAK_CHECK_EQ(f.appearance.local_primary, 0x1976D2U);
    RODAK_CHECK_EQ(f.ui.theme_name(), std::string("blue"));
    RODAK_CHECK_EQ(rodakos_theme_get()->bg_primary, 0x0D1B2AU);
    RODAK_CHECK_EQ(f.navigation.refresh_count, 1);

    const uint32_t revision = f.ui.theme_revision();
    RODAK_CHECK(f.app.OnThemeChanged(f.context));
    Pump();
    RODAK_CHECK_EQ(f.ui.theme_revision(), revision);
    RODAK_CHECK_EQ(Text(f.app.source_label_), std::string("配色：本地"));
    RODAK_CHECK_EQ(lv_obj_get_style_border_width(f.app.preset_buttons_[2], 0), 2);
    // The selection ring must stay visible on a swatch that equals the theme primary.
    const lv_color_t ring = lv_obj_get_style_border_color(f.app.preset_buttons_[2], 0);
    RODAK_CHECK_FALSE(lv_color_eq(ring, lv_color_hex(kThemePresetOptions[2].swatch)));
    RODAK_CHECK(lv_color_eq(ring, rodakos_theme_text_primary()));
    RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_color(f.app.root_, 0), lv_color_hex(0x0D1B2A)));
    Screenshot("themes-blue.ppm");
}

RODAK_TEST("light preset rebuild keeps readable text on the light background") {
    Fixture f;
    f.appearance.snapshot.status = "idle";
    f.Create();
    f.Click(f.app.preset_buttons_[0]);
    RODAK_CHECK(f.app.OnThemeChanged(f.context));
    Pump();
    RODAK_CHECK_EQ(f.ui.theme_name(), std::string("light"));
    RODAK_CHECK(lv_color_eq(lv_obj_get_style_text_color(f.app.source_label_, 0), lv_color_hex(0x212121)));
    Screenshot("themes-light.ppm");
}

RODAK_TEST("failed save keeps the runtime theme and does not refresh") {
    Fixture f;
    f.appearance.save_ok = false;
    f.Create();
    f.Click(f.app.preset_buttons_[3]);
    RODAK_CHECK_EQ(f.appearance.set_calls, 1);
    RODAK_CHECK_EQ(f.ui.theme_name(), std::string("dark"));
    RODAK_CHECK_EQ(rodakos_theme_get()->primary, 0x79CBFFU);
    RODAK_CHECK_EQ(f.navigation.refresh_count, 0);
}

RODAK_TEST("trust row opens the in-app trust page; back returns to the main page, then Home") {
    Fixture f;
    f.SetPublisher(kKeyA, kFingerprintA);
    f.Create();
    f.OpenTrust();
    RODAK_CHECK(f.navigation.launches.empty());
    RODAK_CHECK_EQ(f.appearance.publisher_requests, 1);
    RODAK_CHECK(lv_obj_has_flag(f.app.main_body_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK_EQ(Text(f.app.header_title_label_), std::string("发布端信任"));
    RODAK_CHECK_EQ(Text(f.app.fingerprint_label_), std::string("A3C1 F0E2 B4D6 A8C0\nE2F4 A6B8 C0D2 E4F6"));
    RODAK_CHECK_EQ(Text(f.app.publisher_status_label_), std::string("尚未信任"));
    RODAK_CHECK_FALSE(lv_obj_has_state(f.app.confirm_button_, LV_STATE_DISABLED));
    AssertInsideScreen(f.app.confirm_button_);
    AssertInsideScreen(f.app.refresh_button_);
    Screenshot("themes-trust.ppm");

    // System back (IO10 / gesture) is handled in-app on the trust page.
    RODAK_CHECK(f.app.OnBackRequested());
    Pump();
    RODAK_CHECK(f.app.current_page_ == ThemesPage::kMain);
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.main_body_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(lv_obj_has_flag(f.app.trust_body_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK_EQ(Text(f.app.header_title_label_), std::string("主题"));
    RODAK_CHECK_EQ(f.navigation.home_count, 0);

    RODAK_CHECK(f.app.OnBackRequested());
    Pump();
    RODAK_CHECK_EQ(f.navigation.home_count, 1);
}

RODAK_TEST("physical touch on the confirm button trusts exactly the displayed key") {
    Fixture f;
    f.SetPublisher(kKeyA, kFingerprintA);
    f.Create();
    f.OpenTrust();
    f.Touch(f.app.confirm_button_);
    RODAK_CHECK_EQ(f.appearance.confirmed_keys.size(), 1U);
    RODAK_CHECK_EQ(f.appearance.confirmed_keys[0], std::string(kKeyA));
    RODAK_CHECK_EQ(Text(f.app.publisher_status_label_), std::string("已信任此发布端"));
    RODAK_CHECK(lv_obj_has_state(f.app.confirm_button_, LV_STATE_DISABLED));
    RODAK_CHECK(f.app.pressed_key_id_.empty());

    RODAK_CHECK(f.app.OnBackRequested());
    Pump();
    RODAK_CHECK_EQ(Text(f.app.trust_label_), std::string("发布端：已信任"));
}

RODAK_TEST("remote input and synthetic clicks never confirm a publisher") {
    Fixture f;
    f.SetPublisher(kKeyA, kFingerprintA);
    f.Create();
    f.OpenTrust();

    // Remote control holds the pointer: the press records no key, so the click is refused.
    f.remote_input = true;
    f.Touch(f.app.confirm_button_);
    RODAK_CHECK(f.appearance.confirmed_keys.empty());

    // An event sent without the touch indev (serial or remote command path) is refused too.
    f.remote_input = false;
    f.Click(f.app.confirm_button_);
    RODAK_CHECK(f.appearance.confirmed_keys.empty());
    RODAK_CHECK_EQ(Text(f.app.publisher_status_label_), std::string("尚未信任"));
}

RODAK_TEST("a key change between press and release is refused") {
    Fixture f;
    f.SetPublisher(kKeyA, kFingerprintA);
    f.Create();
    f.OpenTrust();

    lv_obj_update_layout(lv_screen_active());
    lv_area_t area;
    lv_obj_get_coords(f.app.confirm_button_, &area);
    lv_test_mouse_move_to((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
    lv_test_mouse_press();
    lv_test_wait(60);
    RODAK_CHECK_EQ(f.app.pressed_key_id_, std::string(kKeyA));

    // The worker replaces the publisher and the timer redraws the new key mid-press.
    f.SetPublisher(kKeyB, kFingerprintB);
    lv_test_wait(800);
    RODAK_CHECK_EQ(Text(f.app.fingerprint_label_), std::string("B7E5 D3C1 A9F7 E5D3\nC1B9 A7F5 E3D1 C9B7"));
    lv_test_mouse_release();
    lv_test_wait(60);
    Pump();
    RODAK_CHECK(f.appearance.confirmed_keys.empty());
    RODAK_CHECK(f.app.pressed_key_id_.empty());
}

RODAK_TEST("loading, errors and an already trusted key disable confirmation") {
    Fixture f;
    f.SetPublisher(kKeyA, kFingerprintA);
    f.appearance.publisher.loading = true;
    f.Create();
    f.OpenTrust();
    RODAK_CHECK_EQ(Text(f.app.publisher_status_label_), std::string("正在读取发布端..."));
    RODAK_CHECK(lv_obj_has_state(f.app.confirm_button_, LV_STATE_DISABLED));
    RODAK_CHECK(lv_obj_has_state(f.app.refresh_button_, LV_STATE_DISABLED));

    f.appearance.publisher.loading = false;
    f.appearance.publisher.error = "publisher_unavailable";
    lv_test_wait(800);
    RODAK_CHECK_EQ(Text(f.app.publisher_error_label_), std::string("publisher_unavailable"));
    RODAK_CHECK(lv_obj_has_state(f.app.confirm_button_, LV_STATE_DISABLED));
    RODAK_CHECK_FALSE(lv_obj_has_state(f.app.refresh_button_, LV_STATE_DISABLED));
    f.Touch(f.app.confirm_button_);
    RODAK_CHECK(f.appearance.confirmed_keys.empty());

    f.Click(f.app.refresh_button_);
    RODAK_CHECK_EQ(f.appearance.publisher_requests, 2);

    f.SetPublisher(kKeyA, kFingerprintA, true);
    lv_test_wait(800);
    RODAK_CHECK_EQ(Text(f.app.publisher_status_label_), std::string("已信任此发布端"));
    RODAK_CHECK(lv_obj_has_state(f.app.confirm_button_, LV_STATE_DISABLED));
}

RODAK_TEST("failed trust save reports failure and leaves the key untrusted") {
    Fixture f;
    f.SetPublisher(kKeyA, kFingerprintA);
    f.appearance.confirm_ok = false;
    f.Create();
    f.OpenTrust();
    f.Touch(f.app.confirm_button_);
    RODAK_CHECK_EQ(f.appearance.confirmed_keys.size(), 1U);
    RODAK_CHECK_EQ(Text(f.app.publisher_status_label_), std::string("尚未信任"));
    RODAK_CHECK_FALSE(lv_obj_has_state(f.app.confirm_button_, LV_STATE_DISABLED));
}

RODAK_TEST("theme rebuild keeps the trust page open") {
    Fixture f;
    f.SetPublisher(kKeyA, kFingerprintA);
    f.Create();
    f.OpenTrust();
    f.Click(f.app.preset_buttons_[0]);
    RODAK_CHECK(f.app.current_page_ == ThemesPage::kTrust);
    RODAK_CHECK(f.app.OnThemeChanged(f.context));
    Pump();
    RODAK_CHECK(f.app.current_page_ == ThemesPage::kTrust);
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.trust_body_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(lv_obj_has_flag(f.app.main_body_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK_EQ(Text(f.app.header_title_label_), std::string("发布端信任"));
    RODAK_CHECK_EQ(Text(f.app.fingerprint_label_), std::string("A3C1 F0E2 B4D6 A8C0\nE2F4 A6B8 C0D2 E4F6"));
    Screenshot("themes-trust-light.ppm");
}

RODAK_TEST("missing appearance service disables the trust page") {
    Fixture f(false);
    f.Create();
    f.OpenTrust();
    RODAK_CHECK_EQ(Text(f.app.publisher_status_label_), std::string("外观服务不可用"));
    RODAK_CHECK(lv_obj_has_state(f.app.confirm_button_, LV_STATE_DISABLED));
    RODAK_CHECK(lv_obj_has_state(f.app.refresh_button_, LV_STATE_DISABLED));
}

RODAK_TEST("missing appearance service disables presets") {
    Fixture f(false);
    f.Create();
    RODAK_CHECK_EQ(Text(f.app.source_label_), std::string("外观服务不可用"));
    for (auto* button : f.app.preset_buttons_) {
        RODAK_CHECK(lv_obj_has_state(button, LV_STATE_DISABLED));
    }
}

RODAK_TEST("pause stops status polling and hides the root; resume refreshes immediately") {
    Fixture f;
    f.appearance.snapshot.status = "waiting_device";
    f.Create();
    f.app.OnPause();
    RODAK_CHECK(lv_obj_has_flag(f.app.root_, LV_OBJ_FLAG_HIDDEN));
    const int calls = f.appearance.snapshot_calls;
    lv_test_wait(1600);
    RODAK_CHECK_EQ(f.appearance.snapshot_calls, calls);

    f.appearance.snapshot.status = "applied";
    f.app.OnResume();
    Pump();
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.root_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK_EQ(Text(f.app.status_label_), std::string("已生效"));

    f.app.OnPause();
    RODAK_CHECK(f.app.OnThemeChanged(f.context));
    RODAK_CHECK(lv_obj_has_flag(f.app.root_, LV_OBJ_FLAG_HIDDEN));
    const int hidden_calls = f.appearance.snapshot_calls;
    lv_test_wait(1600);
    RODAK_CHECK_EQ(f.appearance.snapshot_calls, hidden_calls);
}

RODAK_TEST("wallpaper preview scales the shared buffer into the preview frame") {
    Fixture f;
    f.ui.SetWallpaper(SolidWallpaper(0xF800), 320, 240);
    f.appearance.snapshot.status = "applied";
    f.appearance.snapshot.active_revision = 7;
    f.Create();
    Pump(30);
    // Preview frame starts 4px below the header at x=16; sample below the mock status bar.
    const uint32_t pixel = rodakos_home_ui_test::FramebufferPixel(16 + 100, 40 + 4 + 6 + 80);
    RODAK_CHECK(((pixel >> 16) & 0xff) > 200);
    RODAK_CHECK(((pixel >> 8) & 0xff) < 40);
    Screenshot("themes-wallpaper.ppm");
}

RODAK_TEST("descriptor registers as a visible system app with unique aliases") {
    PhoneAppRegistry registry;
    RegisterThemesApp(registry);
    const auto* descriptor = registry.FindById("themes");
    RODAK_CHECK(descriptor != nullptr);
    RODAK_CHECK(descriptor->show_on_home);
    RODAK_CHECK(descriptor->category == PhoneAppCategory::kSystem);
    RODAK_CHECK_EQ(registry.ResolveAlias("主题"), descriptor);
    RODAK_CHECK_EQ(registry.ResolveAlias("wallpaper"), descriptor);
    RODAK_CHECK(descriptor->create() != nullptr);
}
