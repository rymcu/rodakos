#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/button_binding_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"

#include <string>
#include <utility>
#include <vector>

namespace {

using namespace rodakos_settings;

constexpr lv_coord_t kGridX = 10;
constexpr lv_coord_t kGridWidth = 300;
constexpr lv_coord_t kRowHeight = 38;
constexpr lv_coord_t kGroupLabelHeight = 20;
constexpr lv_coord_t kGroupGap = 6;
constexpr lv_coord_t kDialogWidth = 288;
constexpr lv_coord_t kDialogHeight = 220;
constexpr lv_coord_t kOptionHeight = 36;

bool SameButtonAction(const rodakos::ButtonAction& lhs, const rodakos::ButtonAction& rhs) {
    if (lhs.type != rhs.type) {
        return false;
    }
    if (lhs.type == rodakos::ButtonActionType::kLaunchApp) {
        return lhs.app_id == rhs.app_id;
    }
    return true;
}

const char* EventText(rodakos::ButtonEvent event) {
    switch (event) {
        case rodakos::ButtonEvent::kSingleClick: return "单击";
        case rodakos::ButtonEvent::kDoubleClick: return "双击";
        case rodakos::ButtonEvent::kLongPressStart: return "长按";
        default: return "按下";
    }
}

std::string ButtonTitle(const rodakos::ButtonState& button) {
    if (button.id == "boot_button") return "BOOT 键";
    if (button.id == "io10_key_button") return "IO10 键";
    return button.title;
}

std::string ActionText(const rodakos::ButtonAction& action, const PhoneAppRegistry* registry) {
    switch (action.type) {
        case rodakos::ButtonActionType::kHome: return "回到桌面";
        case rodakos::ButtonActionType::kLock: return "锁屏";
        case rodakos::ButtonActionType::kToggleControlCenter: return "控制中心";
        case rodakos::ButtonActionType::kLaunchApp: {
            const PhoneAppDescriptor* app =
                registry != nullptr ? registry->FindById(action.app_id) : nullptr;
            return std::string("打开 ") + (app != nullptr ? app->title : action.app_id);
        }
        case rodakos::ButtonActionType::kNone:
        default:
            return "不执行";
    }
}

std::vector<rodakos::ButtonAction> BuildButtonActionOptions(const PhoneAppRegistry& registry) {
    std::vector<rodakos::ButtonAction> options = {
        {.type = rodakos::ButtonActionType::kNone, .app_id = ""},
        {.type = rodakos::ButtonActionType::kHome, .app_id = ""},
        {.type = rodakos::ButtonActionType::kToggleControlCenter, .app_id = ""},
        {.type = rodakos::ButtonActionType::kLock, .app_id = ""},
    };
    for (const auto& app : registry.apps()) {
        if (app.role == PhoneAppRole::kHome) {
            continue;  // Same as 回到桌面.
        }
        options.push_back({.type = rodakos::ButtonActionType::kLaunchApp, .app_id = app.id});
    }
    return options;
}

// One row inside a grouped list, same look as the Settings home page.
lv_obj_t* CreateListRow(lv_obj_t* list, int index, lv_coord_t width, lv_coord_t height) {
    auto* row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, width, height);
    lv_obj_set_pos(row, 0, height * index);
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
    return row;
}

lv_obj_t* CreateGroupList(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                          lv_coord_t width, lv_coord_t height) {
    auto* list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, width, height);
    lv_obj_set_pos(list, x, y);
    lv_obj_set_style_bg_color(list, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, 8, 0);
    lv_obj_set_style_clip_corner(list, true, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    return list;
}

// Payload of one row in the action dialog. `reset` is the 恢复默认 row.
struct ActionChoice {
    rodakos::ButtonBinding binding;
    rodakos::ButtonAction action;
    bool reset = false;
};
}  // namespace

using namespace rodakos_settings;

void SettingsApp::CreateButtonBindingsPage() {
    buttons_body_ = lv_obj_create(lv_obj_get_parent(main_body_));
    lv_obj_remove_style_all(buttons_body_);
    lv_obj_set_size(buttons_body_, lv_obj_get_width(main_body_), lv_obj_get_height(main_body_));
    lv_obj_set_pos(buttons_body_, lv_obj_get_x(main_body_), lv_obj_get_y(main_body_));
    lv_obj_set_style_bg_opa(buttons_body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(buttons_body_, 0, 0);
    lv_obj_add_flag(buttons_body_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(buttons_body_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(buttons_body_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(buttons_body_, LV_OBJ_FLAG_HIDDEN);

    UpdateButtonBindingsPage();
}

// Each key is one group: 单击 / 双击 / 长按 rows of 38 px with the action at the row end.
void SettingsApp::UpdateButtonBindingsPage() {
    if (buttons_body_ == nullptr) {
        return;
    }

    const lv_coord_t scroll_y = lv_obj_get_scroll_y(buttons_body_);
    lv_obj_clean(buttons_body_);

    auto* buttons = context_ != nullptr ? context_->services().buttons() : nullptr;
    if (buttons == nullptr || !buttons->IsAvailable()) {
        auto* empty = CreateSettingLabel(buttons_body_, "没有检测到可设置的按键", true);
        lv_obj_set_pos(empty, kGridX + 4, 12);
        return;
    }

    const PhoneAppRegistry* registry = &context_->registry();
    lv_coord_t y = 4;
    for (const auto& button : buttons->ListButtons()) {
        auto* group_label = CreateSettingLabel(buttons_body_, ButtonTitle(button).c_str(), true);
        lv_obj_set_style_text_font(group_label, &phone_font_12, 0);
        lv_obj_set_pos(group_label, kGridX + 4, y + 3);
        y += kGroupLabelHeight;

        const int rows = static_cast<int>(rodakos::kButtonEvents.size());
        auto* list = CreateGroupList(buttons_body_, kGridX, y, kGridWidth, kRowHeight * rows);
        int index = 0;
        for (rodakos::ButtonEvent event : rodakos::kButtonEvents) {
            const auto binding = buttons->GetBinding(button.id, event);
            auto* row = CreateListRow(list, index++, kGridWidth, kRowHeight);
            lv_obj_set_user_data(row, new rodakos::ButtonBinding(binding));

            auto* event_label = CreateSettingLabel(row, EventText(event));
            lv_obj_align(event_label, LV_ALIGN_LEFT_MID, 0, 0);

            std::string action_text = ActionText(binding.action, registry);
            if (binding.custom) {
                action_text += " · 已自定义";
            }
            auto* action_label = CreateSettingLabel(row, action_text.c_str(), !binding.custom);
            if (binding.custom) {
                lv_obj_set_style_text_color(action_label, rodakos_theme_primary(), 0);
            }
            lv_obj_set_width(action_label, 200);
            lv_label_set_long_mode(action_label, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_align(action_label, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(action_label, LV_ALIGN_RIGHT_MID, -16, 0);

            auto* arrow = lv_label_create(row);
            lv_label_set_text(arrow, ">");
            lv_obj_set_style_text_color(arrow, rodakos_theme_text_tertiary(), 0);
            lv_obj_set_style_text_font(arrow, &phone_font_14, 0);
            lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);

            lv_obj_add_event_cb(row, [](lv_event_t* e) {
                auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
                auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                auto* stored = static_cast<rodakos::ButtonBinding*>(lv_obj_get_user_data(target));
                if (self == nullptr || stored == nullptr || self->context_ == nullptr) {
                    return;
                }
                self->ShowButtonActionDialog(*stored);
            }, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(row, [](lv_event_t* e) {
                delete static_cast<rodakos::ButtonBinding*>(
                    lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(e))));
            }, LV_EVENT_DELETE, nullptr);
        }
        y += kRowHeight * rows + kGroupGap;
    }

    lv_obj_update_layout(buttons_body_);
    lv_obj_scroll_to_y(buttons_body_, scroll_y, LV_ANIM_OFF);
}

// Action picker: 恢复默认 first (naming the default), current choice ticked, 取消 at the bottom.
void SettingsApp::ShowButtonActionDialog(const rodakos::ButtonBinding& binding) {
    if (button_action_dialog_ != nullptr) {
        CloseButtonActionDialog();
    }
    auto* buttons = context_ != nullptr ? context_->services().buttons() : nullptr;
    if (buttons == nullptr) {
        return;
    }
    const PhoneAppRegistry* registry = &context_->registry();

    button_action_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(button_action_dialog_);
    lv_obj_set_size(button_action_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(button_action_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(button_action_dialog_, LV_OPA_70, 0);
    lv_obj_clear_flag(button_action_dialog_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button_action_dialog_, LV_OBJ_FLAG_CLICKABLE);

    auto* dialog_box = lv_obj_create(button_action_dialog_);
    lv_obj_remove_style_all(dialog_box);
    lv_obj_set_size(dialog_box, kDialogWidth, kDialogHeight);
    lv_obj_center(dialog_box);
    lv_obj_set_style_bg_color(dialog_box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(dialog_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dialog_box, 10, 0);
    lv_obj_clear_flag(dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    const rodakos::ButtonState* state = buttons->GetButton(binding.button_id);
    const std::string heading = (state != nullptr ? ButtonTitle(*state) : binding.title) +
                                " · " + EventText(binding.event);
    auto* title = CreateSettingLabel(dialog_box, heading.c_str());
    lv_obj_set_width(title, kDialogWidth - 24);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(title, 12, 10);

    constexpr lv_coord_t kListY = 34;
    constexpr lv_coord_t kCancelHeight = 36;
    constexpr lv_coord_t kListHeight = kDialogHeight - kListY - kCancelHeight - 16;
    constexpr lv_coord_t kListWidth = kDialogWidth - 16;

    auto* scroller = lv_obj_create(dialog_box);
    lv_obj_remove_style_all(scroller);
    lv_obj_set_size(scroller, kListWidth, kListHeight);
    lv_obj_set_pos(scroller, 8, kListY);
    lv_obj_set_scroll_dir(scroller, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroller, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(scroller, LV_OBJ_FLAG_SCROLLABLE);

    const rodakos::ButtonAction default_action =
        buttons->DefaultAction(binding.button_id, binding.event);
    std::vector<ActionChoice> choices;
    choices.push_back({binding, default_action, true});
    for (const auto& action : BuildButtonActionOptions(context_->registry())) {
        choices.push_back({binding, action, false});
    }

    auto* list = CreateGroupList(scroller, 0, 0, kListWidth,
                                 kOptionHeight * static_cast<lv_coord_t>(choices.size()));
    lv_obj_set_style_bg_color(list, rodakos_theme_bg_primary(), 0);
    lv_obj_t* selected_row = nullptr;
    int index = 0;
    for (const auto& choice : choices) {
        auto* row = CreateListRow(list, index++, kListWidth, kOptionHeight);
        const bool selected = choice.reset
            ? !binding.custom
            : binding.custom && SameButtonAction(choice.action, binding.action);
        const std::string text = choice.reset
            ? "恢复默认（" + ActionText(default_action, registry) + "）"
            : ActionText(choice.action, registry);
        auto* label = CreateSettingLabel(row, text.c_str(), choice.reset);
        lv_obj_set_width(label, kListWidth - 56);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        if (selected) {
            selected_row = row;
            auto* check = lv_label_create(row);
            lv_label_set_text(check, ICON_CHECK);
            lv_obj_set_style_text_color(check, rodakos_theme_primary(), 0);
            lv_obj_set_style_text_font(check, PhoneIconFont(), 0);
            lv_obj_align(check, LV_ALIGN_RIGHT_MID, 0, 0);
        }
        lv_obj_set_user_data(row, new ActionChoice(choice));
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
            auto* payload = static_cast<ActionChoice*>(
                lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(e))));
            if (self == nullptr || payload == nullptr) {
                return;
            }
            const ActionChoice copy = *payload;  // Saving deletes the dialog and this payload.
            if (copy.reset) {
                self->ResetButtonBinding(copy.binding);
            } else {
                self->SaveButtonBindingAction(copy.binding, copy.action);
            }
        }, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            delete static_cast<ActionChoice*>(
                lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(e))));
        }, LV_EVENT_DELETE, nullptr);
    }
    if (selected_row != nullptr) {
        lv_obj_update_layout(scroller);
        lv_obj_scroll_to_view(selected_row, LV_ANIM_OFF);
    }

    auto* cancel = lv_btn_create(dialog_box);
    lv_obj_set_size(cancel, kListWidth, kCancelHeight);
    lv_obj_set_pos(cancel, 8, kDialogHeight - kCancelHeight - 8);
    lv_obj_set_style_bg_color(cancel, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(cancel, 8, 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    auto* cancel_label = CreateSettingLabel(cancel, "取消");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        if (self != nullptr) {
            self->CloseButtonActionDialog();
        }
    }, LV_EVENT_CLICKED, this);
}

void SettingsApp::CloseButtonActionDialog() {
    if (button_action_dialog_ != nullptr && lv_obj_is_valid(button_action_dialog_)) {
        lv_obj_delete(button_action_dialog_);
    }
    button_action_dialog_ = nullptr;
}

void SettingsApp::SaveButtonBindingAction(const rodakos::ButtonBinding& binding,
                                          const rodakos::ButtonAction& action) {
    auto* buttons = context_ != nullptr ? context_->services().buttons() : nullptr;
    if (buttons == nullptr) {
        return;
    }
    // Picking the default action by hand is the same as 恢复默认: keep following the firmware.
    if (SameButtonAction(action, buttons->DefaultAction(binding.button_id, binding.event))) {
        ResetButtonBinding(binding);
        return;
    }
    if (!buttons->SetBinding(binding.button_id, binding.event, action)) {
        ui_->ShowToastUnlocked("保存失败");
        return;
    }
    CloseButtonActionDialog();
    UpdateButtonBindingsPage();
    ui_->ShowToastUnlocked("已保存");
}

void SettingsApp::ResetButtonBinding(const rodakos::ButtonBinding& binding) {
    auto* buttons = context_ != nullptr ? context_->services().buttons() : nullptr;
    if (buttons == nullptr) {
        return;
    }
    if (binding.custom && !buttons->ResetBinding(binding.button_id, binding.event)) {
        ui_->ShowToastUnlocked("恢复失败");
        return;
    }
    CloseButtonActionDialog();
    UpdateButtonBindingsPage();
    ui_->ShowToastUnlocked("已恢复默认");
}
