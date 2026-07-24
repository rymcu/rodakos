#include "phone_os/phone_shell.h"

#include "phone_os/phone_services.h"
#include "phone_os/phone_system.h"
#include "phone_os/time_service.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/backlight_adapter.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

#include <esp_log.h>

namespace {
constexpr const char* TAG = "PhoneShell";
constexpr int kTopGestureBand = 20;
constexpr int kBottomGestureBand = 64;
constexpr int kGestureDistance = 48;

lv_obj_t* CreateLabel(lv_obj_t* parent,
                      const char* text,
                      const lv_font_t* font,
                      lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* CreateActionButton(lv_obj_t* parent, const char* icon, const char* title) {
    auto* button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 136, 48);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, rodakos_theme_primary(), LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(button, 1, LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    std::string text = icon != nullptr ? icon : "";
    text.append("  ");
    text.append(title != nullptr ? title : "");
    auto* label = CreateLabel(button, text.c_str(), &phone_font_14, rodakos_theme_text_primary());
    lv_obj_center(label);
    return button;
}

const char* WiFiStatusText(WiFiStatus status) {
    switch (status) {
        case WiFiStatus::kConnected:
            return "Wi-Fi connected";
        case WiFiStatus::kConnecting:
            return "Wi-Fi connecting";
        case WiFiStatus::kFailed:
            return "Wi-Fi unavailable";
        case WiFiStatus::kDisconnected:
        default:
            return "Wi-Fi disconnected";
    }
}
}  // namespace

PhoneShell::PhoneShell(PhoneSystem& system, PhoneUi& ui, PhoneServices& services)
    : system_(system), ui_(ui), services_(services) {}

bool PhoneShell::Initialize() {
    if (initialized_) {
        return true;
    }

    PhoneUiLock lock(ui_);
    if (!lock.locked()) {
        ESP_LOGE(TAG, "Failed to lock LVGL while initializing system shell");
        return false;
    }

    Settings settings("shell", false);
    preferences_.lock_on_boot = settings.GetBool("lock_boot", false);
    preferences_.control_center_gesture_enabled = settings.GetBool("cc_gesture", true);

    lv_indev_t* indev = ui_.primary_input();
    if (indev == nullptr) {
        ESP_LOGW(TAG, "Primary touch input unavailable; shell overlays disabled");
        initialized_ = true;
        return true;
    }

    lv_indev_add_event_cb(indev, IndevEventCallback, LV_EVENT_ALL, this);
    gestures_available_ = true;
    TimeServiceApplySavedTimeZone();
    initialized_ = true;

    if (preferences_.lock_on_boot) {
        ApplyMode(PhoneShellMode::kLocked);
    }

    ESP_LOGI(TAG, "System shell initialized");
    return true;
}

bool PhoneShell::PrepareNavigation() {
    if (locked() || (mode_change_queued_ && pending_mode_ == PhoneShellMode::kLocked)) {
        ESP_LOGW(TAG, "Navigation blocked while device is locked");
        return false;
    }

    pending_mode_ = PhoneShellMode::kNone;
    if (control_center_open()) {
        ApplyMode(PhoneShellMode::kNone);
    }
    return true;
}

bool PhoneShell::Lock() {
    if (!initialized_ || !gestures_available_) {
        return false;
    }
    PhoneUiLock lock(ui_);
    if (!lock.locked()) {
        return false;
    }
    return RequestMode(PhoneShellMode::kLocked);
}

bool PhoneShell::ToggleControlCenter() {
    if (!initialized_ || !gestures_available_) {
        return false;
    }
    PhoneUiLock lock(ui_);
    if (!lock.locked() || locked()) {
        return false;
    }
    return RequestMode(control_center_open() ? PhoneShellMode::kNone
                                             : PhoneShellMode::kControlCenter);
}

bool PhoneShell::SetLockOnBoot(bool enabled) {
    Settings settings("shell", true);
    if (!settings.SetBool("lock_boot", enabled) || !settings.Commit()) {
        ESP_LOGE(TAG, "Failed to persist lock-on-boot preference");
        return false;
    }
    preferences_.lock_on_boot = enabled;
    return true;
}

bool PhoneShell::SetControlCenterGestureEnabled(bool enabled) {
    Settings settings("shell", true);
    if (!settings.SetBool("cc_gesture", enabled) || !settings.Commit()) {
        ESP_LOGE(TAG, "Failed to persist Control Center gesture preference");
        return false;
    }
    preferences_.control_center_gesture_enabled = enabled;
    return true;
}

void PhoneShell::IndevEventCallback(lv_event_t* event) {
    auto* self = static_cast<PhoneShell*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->HandleIndevEvent(event);
    }
}

void PhoneShell::HandleIndevEvent(lv_event_t* event) {
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_current_target(event));
    if (indev == nullptr) {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(event);
    if ((code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_CLICKED) && suppress_clicks_) {
        lv_indev_stop_processing(indev);
        if (code == LV_EVENT_CLICKED) {
            suppress_clicks_ = false;
        }
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        suppress_clicks_ = false;
        lv_indev_get_point(indev, &gesture_start_);
        const bool unlock_region = locked() &&
                                   gesture_start_.y >= ui_.height() - kBottomGestureBand;
        gesture_tracking_ = unlock_region || control_center_open() ||
                            (!locked() && preferences_.control_center_gesture_enabled &&
                             gesture_start_.y <= kTopGestureBand);
        return;
    }
    if (code != LV_EVENT_RELEASED || !gesture_tracking_) {
        return;
    }

    gesture_tracking_ = false;
    lv_point_t end = {0, 0};
    lv_indev_get_point(indev, &end);
    const int dx = static_cast<int>(end.x) - static_cast<int>(gesture_start_.x);
    const int dy = static_cast<int>(end.y) - static_cast<int>(gesture_start_.y);
    const bool vertical = std::abs(dy) > std::abs(dx);

    PhoneShellMode next = mode_;
    if (mode_ == PhoneShellMode::kNone && preferences_.control_center_gesture_enabled &&
        vertical && dy >= kGestureDistance) {
        next = PhoneShellMode::kControlCenter;
    } else if (mode_ == PhoneShellMode::kControlCenter && vertical && dy <= -kGestureDistance) {
        next = PhoneShellMode::kNone;
    } else if (mode_ == PhoneShellMode::kLocked && vertical && dy <= -kGestureDistance) {
        next = PhoneShellMode::kNone;
    }

    if (next != mode_) {
        suppress_clicks_ = true;
        lv_indev_reset(indev, nullptr);
        RequestMode(next);
    }
}

bool PhoneShell::RequestMode(PhoneShellMode mode) {
    if (mode != PhoneShellMode::kNone && !gestures_available_) {
        ESP_LOGW(TAG, "Ignoring shell overlay request without an unlock input path");
        return false;
    }
    pending_mode_ = mode;
    if (mode_change_queued_) {
        return true;
    }
    if (lv_async_call(ApplyPendingModeAsync, this) == LV_RESULT_OK) {
        mode_change_queued_ = true;
        return true;
    } else {
        ESP_LOGW(TAG, "Failed to queue shell mode change");
        return false;
    }
}

void PhoneShell::ApplyPendingModeAsync(void* user_data) {
    auto* self = static_cast<PhoneShell*>(user_data);
    if (self == nullptr) {
        return;
    }
    self->mode_change_queued_ = false;
    self->ApplyMode(self->pending_mode_);
}

void PhoneShell::ApplyMode(PhoneShellMode mode) {
    if (mode_ == mode && ((mode == PhoneShellMode::kNone) || overlay_ != nullptr)) {
        return;
    }

    DestroyOverlay();
    mode_ = mode;
    if (mode_ == PhoneShellMode::kControlCenter) {
        CreateControlCenter();
    } else if (mode_ == PhoneShellMode::kLocked) {
        CreateLockScreen();
    }
    ui_.ResetInputState();
    ESP_LOGI(TAG, "Shell mode changed to %u", static_cast<unsigned>(mode_));
}

void PhoneShell::DestroyOverlay() {
    if (clock_timer_ != nullptr) {
        lv_timer_delete(clock_timer_);
        clock_timer_ = nullptr;
    }
    if (overlay_ != nullptr && lv_obj_is_valid(overlay_)) {
        lv_obj_delete(overlay_);
    }
    overlay_ = nullptr;
    clock_label_ = nullptr;
    date_label_ = nullptr;
    lock_wifi_label_ = nullptr;
}

void PhoneShell::CreateControlCenter() {
    overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay_);
    lv_obj_set_size(overlay_, ui_.width(), ui_.height());
    lv_obj_set_pos(overlay_, 0, 0);
    lv_obj_set_style_bg_color(overlay_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateLabel(overlay_, "Control Center", &phone_font_18,
                              rodakos_theme_text_primary());
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 10);

    auto* close_button = lv_btn_create(overlay_);
    lv_obj_remove_style_all(close_button);
    lv_obj_set_size(close_button, 36, 28);
    lv_obj_align(close_button, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_set_style_radius(close_button, 6, 0);
    lv_obj_set_style_bg_color(close_button, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(close_button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(close_button, rodakos_theme_primary(), LV_STATE_PRESSED);
    lv_obj_clear_flag(close_button, LV_OBJ_FLAG_SCROLLABLE);
    auto* close_icon = CreateLabel(close_button, FONT_AWESOME_XMARK, PhoneIconFont(),
                                   rodakos_theme_text_primary());
    lv_obj_center(close_icon);
    lv_obj_add_event_cb(close_button, CloseCallback, LV_EVENT_SHORT_CLICKED, this);

    auto* wifi_panel = lv_obj_create(overlay_);
    lv_obj_remove_style_all(wifi_panel);
    lv_obj_set_size(wifi_panel, 292, 42);
    lv_obj_align(wifi_panel, LV_ALIGN_TOP_MID, 0, 43);
    lv_obj_set_style_radius(wifi_panel, 8, 0);
    lv_obj_set_style_bg_color(wifi_panel, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(wifi_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(wifi_panel, LV_OBJ_FLAG_SCROLLABLE);

    WiFiStatus wifi_status = WiFiStatus::kDisconnected;
    if (services_.wifi() != nullptr) {
        wifi_status = services_.wifi()->GetStatus();
    }
    const char* wifi_icon = wifi_status == WiFiStatus::kConnected
                                ? FONT_AWESOME_WIFI
                                : FONT_AWESOME_WIFI_SLASH;
    auto* wifi_icon_label = CreateLabel(wifi_panel, wifi_icon, PhoneIconFont(),
                                        rodakos_theme_text_primary());
    lv_obj_align(wifi_icon_label, LV_ALIGN_LEFT_MID, 12, 0);
    auto* wifi_label = CreateLabel(wifi_panel, WiFiStatusText(wifi_status), &phone_font_14,
                                   rodakos_theme_text_primary());
    lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, 42, 0);

    auto* brightness_panel = lv_obj_create(overlay_);
    lv_obj_remove_style_all(brightness_panel);
    lv_obj_set_size(brightness_panel, 292, 68);
    lv_obj_align(brightness_panel, LV_ALIGN_TOP_MID, 0, 93);
    lv_obj_set_style_radius(brightness_panel, 8, 0);
    lv_obj_set_style_bg_color(brightness_panel, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(brightness_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(brightness_panel, LV_OBJ_FLAG_SCROLLABLE);

    auto* brightness_title = CreateLabel(brightness_panel,
                                         FONT_AWESOME_BRIGHTNESS "  Brightness",
                                         &phone_font_14,
                                         rodakos_theme_text_primary());
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 12, 8);

    auto* slider = lv_slider_create(brightness_panel);
    lv_obj_set_size(slider, 264, 8);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -13);
    lv_slider_set_range(slider, 5, 100);
    int brightness = 75;
    if (services_.backlight() != nullptr) {
        brightness = services_.backlight()->GetBrightness();
    }
    lv_slider_set_value(slider, brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, rodakos_theme_primary(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, rodakos_theme_primary(), LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    lv_obj_add_event_cb(slider, BrightnessChangedCallback, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(slider, BrightnessReleasedCallback, LV_EVENT_RELEASED, this);

    auto* settings_button = CreateActionButton(overlay_, FONT_AWESOME_GEAR, "Settings");
    lv_obj_align(settings_button, LV_ALIGN_BOTTOM_LEFT, 14, -18);
    lv_obj_add_event_cb(settings_button, OpenSettingsCallback, LV_EVENT_SHORT_CLICKED, this);

    auto* lock_button = CreateActionButton(overlay_, FONT_AWESOME_LOCK, "Lock");
    lv_obj_align(lock_button, LV_ALIGN_BOTTOM_RIGHT, -14, -18);
    lv_obj_add_event_cb(lock_button, LockCallback, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_move_foreground(overlay_);
}

void PhoneShell::CreateLockScreen() {
    overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay_);
    lv_obj_set_size(overlay_, ui_.width(), ui_.height());
    lv_obj_set_pos(overlay_, 0, 0);
    lv_obj_set_style_bg_color(overlay_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);

    auto* lock_icon = CreateLabel(overlay_, FONT_AWESOME_LOCK, PhoneIconFont(),
                                  rodakos_theme_text_secondary());
    lv_obj_align(lock_icon, LV_ALIGN_TOP_LEFT, 14, 12);

    lock_wifi_label_ = CreateLabel(overlay_, FONT_AWESOME_WIFI_SLASH, PhoneIconFont(),
                                   rodakos_theme_text_secondary());
    lv_obj_align(lock_wifi_label_, LV_ALIGN_TOP_RIGHT, -14, 12);

    clock_label_ = CreateLabel(overlay_, "--:--", &phone_font_18,
                               rodakos_theme_text_primary());
    lv_obj_align(clock_label_, LV_ALIGN_CENTER, 0, -35);

    date_label_ = CreateLabel(overlay_, "", &phone_font_14,
                              rodakos_theme_text_secondary());
    lv_obj_align(date_label_, LV_ALIGN_CENTER, 0, -8);

    auto* unlock_icon = CreateLabel(overlay_, FONT_AWESOME_ARROW_UP, PhoneIconFont(),
                                    rodakos_theme_text_primary());
    lv_obj_align(unlock_icon, LV_ALIGN_BOTTOM_MID, 0, -28);
    auto* unlock_label = CreateLabel(overlay_, "Unlock", &phone_font_12,
                                     rodakos_theme_text_secondary());
    lv_obj_align(unlock_label, LV_ALIGN_BOTTOM_MID, 0, -9);

    UpdateLockScreen();
    clock_timer_ = lv_timer_create(ClockTimerCallback, 1000, this);
    lv_obj_move_foreground(overlay_);
}

void PhoneShell::UpdateLockScreen() {
    if (clock_label_ == nullptr || date_label_ == nullptr) {
        return;
    }

    if (TimeServiceTimeIsValid()) {
        const std::time_t now = std::time(nullptr);
        std::tm local_time = {};
        localtime_r(&now, &local_time);
        char time_text[16] = {};
        char date_text[32] = {};
        std::strftime(time_text, sizeof(time_text), "%H:%M", &local_time);
        std::strftime(date_text, sizeof(date_text), "%a, %Y-%m-%d", &local_time);
        lv_label_set_text(clock_label_, time_text);
        lv_label_set_text(date_label_, date_text);
    } else {
        lv_label_set_text(clock_label_, "--:--");
        lv_label_set_text(date_label_, "Time not synchronized");
    }

    if (lock_wifi_label_ != nullptr) {
        const bool connected = services_.wifi() != nullptr &&
                               services_.wifi()->GetStatus() == WiFiStatus::kConnected;
        lv_label_set_text(lock_wifi_label_, connected ? FONT_AWESOME_WIFI
                                                      : FONT_AWESOME_WIFI_SLASH);
    }
}

void PhoneShell::ClockTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<PhoneShell*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->UpdateLockScreen();
    }
}

void PhoneShell::OpenSettingsCallback(lv_event_t* event) {
    auto* self = static_cast<PhoneShell*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->QueueOpenSettings();
    }
}

void PhoneShell::QueueOpenSettings() {
    if (navigation_queued_) {
        return;
    }
    if (lv_async_call(OpenSettingsAsync, this) == LV_RESULT_OK) {
        navigation_queued_ = true;
    } else {
        ESP_LOGW(TAG, "Failed to queue Settings launch");
    }
}

void PhoneShell::OpenSettingsAsync(void* user_data) {
    auto* self = static_cast<PhoneShell*>(user_data);
    if (self == nullptr) {
        return;
    }
    self->navigation_queued_ = false;
    self->ApplyMode(PhoneShellMode::kNone);
    if (!self->system_.LaunchApp("settings")) {
        ESP_LOGW(TAG, "Settings launch from Control Center failed");
    }
}

void PhoneShell::CloseCallback(lv_event_t* event) {
    auto* self = static_cast<PhoneShell*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->RequestMode(PhoneShellMode::kNone);
    }
}

void PhoneShell::LockCallback(lv_event_t* event) {
    auto* self = static_cast<PhoneShell*>(lv_event_get_user_data(event));
    if (self != nullptr) {
        self->RequestMode(PhoneShellMode::kLocked);
    }
}

void PhoneShell::BrightnessChangedCallback(lv_event_t* event) {
    auto* self = static_cast<PhoneShell*>(lv_event_get_user_data(event));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (self == nullptr || slider == nullptr || self->services_.backlight() == nullptr) {
        return;
    }
    const int value = lv_slider_get_value(slider);
    self->services_.backlight()->SetBrightness(static_cast<uint8_t>(value), false);
}

void PhoneShell::BrightnessReleasedCallback(lv_event_t* event) {
    auto* self = static_cast<PhoneShell*>(lv_event_get_user_data(event));
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (self == nullptr || slider == nullptr || self->services_.backlight() == nullptr) {
        return;
    }
    const int value = lv_slider_get_value(slider);
    self->services_.backlight()->SetBrightness(static_cast<uint8_t>(value), true);
}
