#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "rodakos_adapters/backlight_adapter.h"
#include "settings.h"

#include <esp_log.h>

namespace {
constexpr const char* TAG = "SettingsApp";
}  // namespace

using namespace rodakos_settings;
void SettingsApp::CreateMainPage() {
    Settings display_settings(kDisplayNamespace, false);
    const int brightness = display_settings.GetInt(kBrightnessKey, 75);
    const std::string theme = display_settings.GetString(kThemeKey, "dark");
    const std::string language = display_settings.GetString(kLanguageKey, "en");
    const int selected_theme = ThemeIndexFromId(theme);

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

    CreateSettingIcon(brightness_card, FONT_AWESOME_BRIGHTNESS);

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

    // ===== 主题设置卡片 =====
    auto* theme_card = CreateSettingCard(main_body_, 84);
    CreateSettingIcon(theme_card, FONT_AWESOME_MOON);

    auto* theme_title = CreateSettingLabel(theme_card, "Theme");
    lv_obj_align(theme_title, LV_ALIGN_TOP_LEFT, 28, 0);

    auto* theme_row = lv_obj_create(theme_card);
    lv_obj_remove_style_all(theme_row);
    lv_obj_set_size(theme_row, 188, 30);
    lv_obj_align(theme_row, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_flex_flow(theme_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(theme_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(theme_row, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < kThemeOptions.size(); ++i) {
        auto* btn = lv_btn_create(theme_row);
        theme_buttons_[i] = btn;
        lv_obj_set_user_data(btn, const_cast<ThemeOption*>(&kThemeOptions[i]));
        lv_obj_set_size(btn, 42, 28);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(kThemeOptions[i].swatch), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, i == static_cast<size_t>(selected_theme) ? 2 : 1, 0);
        lv_obj_set_style_border_color(btn,
                                      i == static_cast<size_t>(selected_theme)
                                          ? rodakos_theme_primary()
                                          : rodakos_theme_border(),
                                      0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        auto* label = lv_label_create(btn);
        lv_label_set_text(label, kThemeOptions[i].button_label);
        lv_obj_set_style_text_color(label, lv_color_hex(kThemeOptions[i].label_color), 0);
        lv_obj_set_style_text_font(label, &phone_font_12, 0);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        auto* option = static_cast<const ThemeOption*>(lv_obj_get_user_data(btn));
        if (option != nullptr) {
            Settings settings(kDisplayNamespace, true);
            settings.SetString(kThemeKey, option->id);
            ApplyThemeToRuntime(self->ui_, *option);
            self->ui_->ShowToastUnlocked("Theme changed");
            if (auto* indev = lv_indev_active(); indev != nullptr) {
                lv_indev_wait_release(indev);
            }
            ESP_LOGI(TAG, "Theme changed to %s, reloading settings", option->id);
            lv_async_call(DeferReloadSettings, self);
        }
        }, LV_EVENT_CLICKED, this);
    }

    // ===== 语言设置卡片 =====
    auto* language_card = CreateSettingCard(main_body_, 142);
    CreateSettingIcon(language_card, FONT_AWESOME_GLOBE);

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
    CreateSettingIcon(wifi_card, FONT_AWESOME_WIFI);

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

    // ===== 日期与时间入口 =====
    auto* datetime_card = CreateSettingCard(main_body_, 258);
    lv_obj_add_flag(datetime_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(datetime_card, FONT_AWESOME_CLOCK);

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

    // ===== 按键绑定入口 =====
    auto* buttons_card = CreateSettingCard(main_body_, 316);
    lv_obj_add_flag(buttons_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(buttons_card, FONT_AWESOME_KEY);

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

    // ===== 设备服务入口 =====
    auto* cloud_card = CreateSettingCard(main_body_, 374);
    lv_obj_add_flag(cloud_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(cloud_card, FONT_AWESOME_CLOUD);

    auto* cloud_title = CreateSettingLabel(cloud_card, "Device Services");
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

    // ===== Web 上传入口 =====
    auto* upload_card = CreateSettingCard(main_body_, 432);
    lv_obj_add_flag(upload_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(upload_card, FONT_AWESOME_CLOUD);

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
    auto* usb_card = CreateSettingCard(main_body_, 490);
    lv_obj_add_flag(usb_card, LV_OBJ_FLAG_CLICKABLE);

    CreateSettingIcon(usb_card, FONT_AWESOME_SD_CARD);

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
