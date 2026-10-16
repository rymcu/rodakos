#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "rodakos_adapters/backlight_adapter.h"
#include "settings.h"

using namespace rodakos_settings;
void SettingsApp::CreateMainPage() {
    Settings display_settings(kDisplayNamespace, false);
    const int brightness = display_settings.GetInt(kBrightnessKey, 75);
    const std::string language = display_settings.GetString(kLanguageKey, "en");

    lv_obj_add_flag(main_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(main_body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(main_body_, LV_SCROLLBAR_MODE_AUTO);

    // ===== 亮度设置卡片 =====
    auto* brightness_card = lv_obj_create(main_body_);
    lv_obj_remove_style_all(brightness_card);
    lv_obj_set_size(brightness_card, 300, 68);
    lv_obj_align(brightness_card, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(brightness_card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(brightness_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(brightness_card, 8, 0);
    lv_obj_set_style_pad_all(brightness_card, 12, 0);
    lv_obj_clear_flag(brightness_card, LV_OBJ_FLAG_SCROLLABLE);

    CreateSettingIcon(brightness_card, ICON_BRIGHTNESS);

    auto* brightness_title = CreateSettingLabel(brightness_card, "Brightness");
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 28, 0);

    brightness_label_ = CreateSettingLabel(brightness_card, "", true);
    lv_obj_align(brightness_label_, LV_ALIGN_TOP_RIGHT, 0, 0);
    UpdateBrightnessLabel(brightness_label_, brightness);

    brightness_slider_ = lv_slider_create(brightness_card);
    lv_obj_set_size(brightness_slider_, 242, 8);
    lv_obj_align(brightness_slider_, LV_ALIGN_BOTTOM_RIGHT, 0, -4);
    lv_slider_set_range(brightness_slider_, 5, 100);
    lv_slider_set_value(brightness_slider_, brightness, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_primary(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_primary(), LV_PART_KNOB);
    lv_obj_set_style_bg_color(brightness_slider_, rodakos_theme_bg_tertiary(), LV_PART_MAIN);

    lv_obj_add_event_cb(brightness_slider_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const int value = lv_slider_get_value(slider);
        UpdateBrightnessLabel(self->brightness_label_, value);
        if (auto* backlight = self->context_->services().backlight(); backlight != nullptr) {
            backlight->SetBrightness(static_cast<uint8_t>(value), false);
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_add_event_cb(brightness_slider_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const int value = lv_slider_get_value(slider);
        Settings settings(kDisplayNamespace, true);
        settings.SetInt(kBrightnessKey, value);
        if (auto* backlight = self->context_->services().backlight(); backlight != nullptr) {
            backlight->SetBrightness(static_cast<uint8_t>(value), true);
        }
        self->ui_->ShowToastUnlocked("Brightness saved");
    }, LV_EVENT_RELEASED, this);

    // ===== 主题入口 =====
    auto* theme_card = CreateSettingCard(main_body_, 84);
    lv_obj_add_flag(theme_card, LV_OBJ_FLAG_CLICKABLE);
    CreateSettingIcon(theme_card, ICON_PALETTE);

    auto* theme_title = CreateSettingLabel(theme_card, "主题");
    lv_obj_align(theme_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* theme_arrow = lv_label_create(theme_card);
    lv_label_set_text(theme_arrow, ">");
    lv_obj_set_style_text_color(theme_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(theme_arrow, &phone_font_18, 0);
    lv_obj_align(theme_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(theme_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        lv_async_call(DeferOpenThemes, self->context_);
    }, LV_EVENT_CLICKED, this);

    // ===== 语言设置卡片 =====
    auto* language_card = CreateSettingCard(main_body_, 142);
    CreateSettingIcon(language_card, ICON_GLOBE);

    auto* language_title = CreateSettingLabel(language_card, "Chinese language");
    lv_obj_align(language_title, LV_ALIGN_LEFT_MID, 28, 0);

    language_switch_ = lv_switch_create(language_card);
    lv_obj_align(language_switch_, LV_ALIGN_RIGHT_MID, 0, 0);
    if (language == "zh") {
        lv_obj_add_state(language_switch_, LV_STATE_CHECKED);
    }

    const lv_style_selector_t checked_indicator =
        static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
        static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(language_switch_, rodakos_theme_success(), checked_indicator);
    lv_obj_set_style_bg_color(language_switch_, rodakos_theme_bg_tertiary(), LV_PART_INDICATOR);

    lv_obj_add_event_cb(language_switch_, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const bool chinese = lv_obj_has_state(sw, LV_STATE_CHECKED);
        Settings settings(kDisplayNamespace, true);
        settings.SetString(kLanguageKey, chinese ? "zh" : "en");
        self->ui_->ShowToastUnlocked("Language preference saved");
    }, LV_EVENT_VALUE_CHANGED, this);

    // ===== WiFi 设置入口 =====
    auto* wifi_card = CreateSettingCard(main_body_, 200);
    lv_obj_add_flag(wifi_card, LV_OBJ_FLAG_CLICKABLE);
    CreateSettingIcon(wifi_card, ICON_WIFI);

    auto* wifi_title = CreateSettingLabel(wifi_card, "WiFi Settings");
    lv_obj_align(wifi_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* wifi_arrow = lv_label_create(wifi_card);
    lv_label_set_text(wifi_arrow, ">");
    lv_obj_set_style_text_color(wifi_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(wifi_arrow, &phone_font_18, 0);
    lv_obj_align(wifi_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(wifi_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowPage(SettingsPage::kWiFiList);
    }, LV_EVENT_CLICKED, this);

    // ===== Rodak 连接入口 =====
    auto* cloud_card = CreateSettingCard(main_body_, 258);
    lv_obj_add_flag(cloud_card, LV_OBJ_FLAG_CLICKABLE);
    CreateSettingIcon(cloud_card, ICON_CLOUD);

    auto* cloud_title = CreateSettingLabel(cloud_card, "连接 Rodak");
    lv_obj_align(cloud_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* cloud_arrow = lv_label_create(cloud_card);
    lv_label_set_text(cloud_arrow, ">");
    lv_obj_set_style_text_color(cloud_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(cloud_arrow, &phone_font_18, 0);
    lv_obj_align(cloud_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(cloud_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowPage(SettingsPage::kDeviceCloud);
    }, LV_EVENT_CLICKED, this);

    // ===== 日期与时间入口 =====
    auto* datetime_card = CreateSettingCard(main_body_, 316);
    lv_obj_add_flag(datetime_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(datetime_card, ICON_CLOCK);

    auto* datetime_title = CreateSettingLabel(datetime_card, "Date & Time");
    lv_obj_align(datetime_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* datetime_arrow = lv_label_create(datetime_card);
    lv_label_set_text(datetime_arrow, ">");
    lv_obj_set_style_text_color(datetime_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(datetime_arrow, &phone_font_18, 0);
    lv_obj_align(datetime_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(datetime_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowPage(SettingsPage::kDateTime);
    }, LV_EVENT_CLICKED, this);

    // ===== 系统壳入口 =====
    auto* shell_card = CreateSettingCard(main_body_, 374);
    lv_obj_add_flag(shell_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(shell_card, ICON_LOCK);

    auto* shell_title = CreateSettingLabel(shell_card, "System Shell");
    lv_obj_align(shell_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* shell_arrow = lv_label_create(shell_card);
    lv_label_set_text(shell_arrow, ">");
    lv_obj_set_style_text_color(shell_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(shell_arrow, &phone_font_18, 0);
    lv_obj_align(shell_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(shell_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowPage(SettingsPage::kSystemShell);
    }, LV_EVENT_CLICKED, this);

    // ===== 按键绑定入口 =====
    auto* buttons_card = CreateSettingCard(main_body_, 432);
    lv_obj_add_flag(buttons_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(buttons_card, ICON_KEY);

    auto* buttons_title = CreateSettingLabel(buttons_card, "Button Bindings");
    lv_obj_align(buttons_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* buttons_arrow = lv_label_create(buttons_card);
    lv_label_set_text(buttons_arrow, ">");
    lv_obj_set_style_text_color(buttons_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(buttons_arrow, &phone_font_18, 0);
    lv_obj_align(buttons_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(buttons_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowPage(SettingsPage::kButtons);
    }, LV_EVENT_CLICKED, this);

    // ===== Web 上传入口 =====
    auto* upload_card = CreateSettingCard(main_body_, 490);
    lv_obj_add_flag(upload_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(upload_card, ICON_CLOUD);

    auto* upload_title = CreateSettingLabel(upload_card, "Web Files");
    lv_obj_align(upload_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* upload_arrow = lv_label_create(upload_card);
    lv_label_set_text(upload_arrow, ">");
    lv_obj_set_style_text_color(upload_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(upload_arrow, &phone_font_18, 0);
    lv_obj_align(upload_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(upload_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowPage(SettingsPage::kWebFiles);
    }, LV_EVENT_CLICKED, this);

    // ===== USB 磁盘模式入口 =====
    auto* usb_card = CreateSettingCard(main_body_, 548);
    lv_obj_add_flag(usb_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(usb_card, ICON_STORAGE);

    auto* usb_title = CreateSettingLabel(usb_card, "USB Disk Mode");
    lv_obj_align(usb_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* usb_arrow = lv_label_create(usb_card);
    lv_label_set_text(usb_arrow, ">");
    lv_obj_set_style_text_color(usb_arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(usb_arrow, &phone_font_18, 0);
    lv_obj_align(usb_arrow, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(usb_card, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        self->ShowUsbDiskDialog();
    }, LV_EVENT_CLICKED, this);
}
