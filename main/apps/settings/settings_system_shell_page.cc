#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_navigation.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"

namespace {
void StyleSwitch(lv_obj_t* sw) {
    const auto checked_indicator = static_cast<lv_style_selector_t>(
        static_cast<uint32_t>(LV_PART_INDICATOR) | static_cast<uint32_t>(LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(sw, rodakos_theme_primary(), checked_indicator);
    lv_obj_set_style_bg_color(sw, rodakos_theme_bg_tertiary(), LV_PART_INDICATOR);
}

void RestoreSwitch(lv_obj_t* sw, bool enabled) {
    if (enabled) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
    }
}
}  // namespace

using namespace rodakos_settings;

void SettingsApp::CreateSystemShellPage() {
    system_shell_body_ = lv_obj_create(lv_obj_get_parent(main_body_));
    lv_obj_remove_style_all(system_shell_body_);
    lv_obj_set_size(system_shell_body_, lv_obj_get_width(main_body_), lv_obj_get_height(main_body_));
    lv_obj_set_pos(system_shell_body_, lv_obj_get_x(main_body_), lv_obj_get_y(main_body_));
    lv_obj_set_style_bg_opa(system_shell_body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(system_shell_body_, 0, 0);
    lv_obj_clear_flag(system_shell_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(system_shell_body_, LV_OBJ_FLAG_HIDDEN);

    const PhoneShellPreferences preferences = context_->navigation().GetShellPreferences();

    auto* lock_card = CreateSettingCard(system_shell_body_, 8, 62);
    CreateSettingIcon(lock_card, FONT_AWESOME_LOCK);
    auto* lock_title = CreateSettingLabel(lock_card, "Lock after startup");
    lv_obj_align(lock_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* lock_switch = lv_switch_create(lock_card);
    lv_obj_align(lock_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    RestoreSwitch(lock_switch, preferences.lock_on_boot);
    StyleSwitch(lock_switch);
    lv_obj_add_event_cb(lock_switch, [](lv_event_t* event) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(event));
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
        const bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        if (!self->context_->navigation().SetLockOnBoot(enabled)) {
            RestoreSwitch(sw, !enabled);
            self->ui_->ShowToastUnlocked("Shell setting failed");
            return;
        }
        self->ui_->ShowToastUnlocked("Startup lock saved");
    }, LV_EVENT_VALUE_CHANGED, this);

    auto* gesture_card = CreateSettingCard(system_shell_body_, 80, 62);
    CreateSettingIcon(gesture_card, FONT_AWESOME_ARROW_DOWN);
    auto* gesture_title = CreateSettingLabel(gesture_card, "Control Center swipe");
    lv_obj_align(gesture_title, LV_ALIGN_LEFT_MID, 28, 0);

    auto* gesture_switch = lv_switch_create(gesture_card);
    lv_obj_align(gesture_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    RestoreSwitch(gesture_switch, preferences.control_center_gesture_enabled);
    StyleSwitch(gesture_switch);
    lv_obj_add_event_cb(gesture_switch, [](lv_event_t* event) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(event));
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
        const bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        if (!self->context_->navigation().SetControlCenterGestureEnabled(enabled)) {
            RestoreSwitch(sw, !enabled);
            self->ui_->ShowToastUnlocked("Shell setting failed");
            return;
        }
        self->ui_->ShowToastUnlocked("Control Center setting saved");
    }, LV_EVENT_VALUE_CHANGED, this);
}
