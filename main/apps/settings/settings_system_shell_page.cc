#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_navigation.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"

// 锁屏与控制中心: two 52 px rows in one group. The whole row toggles its switch, and each row
// says in one line what the setting does.
namespace {

using namespace rodakos_settings;

constexpr lv_coord_t kGridX = 10;
constexpr lv_coord_t kGridWidth = 300;
constexpr lv_coord_t kRowHeight = 52;

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

// Returns the switch. Taps anywhere on the row are forwarded to it.
lv_obj_t* CreateToggleRow(lv_obj_t* list, int index, const char* icon, const char* title,
                          const char* detail, bool enabled) {
    auto* row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, kGridWidth, kRowHeight);
    lv_obj_set_pos(row, 0, kRowHeight * index);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(row, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    if (index > 0) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, rodakos_theme_bg_tertiary(), 0);
    }

    CreateSettingIcon(row, icon);
    auto* title_label = CreateSettingLabel(row, title);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 28, 8);
    auto* detail_label = CreateSettingLabel(row, detail, true);
    lv_obj_set_style_text_font(detail_label, &phone_font_12, 0);
    lv_obj_set_width(detail_label, kGridWidth - 24 - 28 - 56);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
    lv_obj_align(detail_label, LV_ALIGN_BOTTOM_LEFT, 28, -7);

    auto* sw = lv_switch_create(row);
    lv_obj_set_size(sw, 44, 24);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    RestoreSwitch(sw, enabled);
    StyleSwitch(sw);

    lv_obj_add_event_cb(row, [](lv_event_t* event) {
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
        RestoreSwitch(sw, !lv_obj_has_state(sw, LV_STATE_CHECKED));
        lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr);
    }, LV_EVENT_CLICKED, sw);
    return sw;
}
}  // namespace

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

    auto* list = lv_obj_create(system_shell_body_);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, kGridWidth, kRowHeight * 2);
    lv_obj_set_pos(list, kGridX, 8);
    lv_obj_set_style_bg_color(list, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, 8, 0);
    lv_obj_set_style_clip_corner(list, true, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    auto* lock_switch = CreateToggleRow(list, 0, ICON_LOCK, "开机后锁屏",
                                        "开机后先显示锁屏，上滑解锁", preferences.lock_on_boot);
    lv_obj_add_event_cb(lock_switch, [](lv_event_t* event) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(event));
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
        const bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        if (!self->context_->navigation().SetLockOnBoot(enabled)) {
            RestoreSwitch(sw, !enabled);
            self->ui_->ShowToastUnlocked("保存失败");
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    auto* gesture_switch = CreateToggleRow(list, 1, ICON_ARROW_DOWN, "下滑打开控制中心",
                                           "从屏幕顶部下滑；关闭后可用按键打开",
                                           preferences.control_center_gesture_enabled);
    lv_obj_add_event_cb(gesture_switch, [](lv_event_t* event) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(event));
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
        const bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        if (!self->context_->navigation().SetControlCenterGestureEnabled(enabled)) {
            RestoreSwitch(sw, !enabled);
            self->ui_->ShowToastUnlocked("保存失败");
        }
    }, LV_EVENT_VALUE_CHANGED, this);
}
