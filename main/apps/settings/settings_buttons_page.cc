#include "apps/settings/settings_app.h"
#include "apps/settings/settings_app_internal.h"

#include "phone_os/button_binding_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"

#include <utility>
#include <vector>

namespace {
struct ButtonActionOption {
    rodakos::ButtonAction action;
};

bool SameButtonAction(const rodakos::ButtonAction& lhs, const rodakos::ButtonAction& rhs) {
    if (lhs.type != rhs.type) {
        return false;
    }
    if (lhs.type == rodakos::ButtonActionType::kLaunchApp) {
        return lhs.app_id == rhs.app_id;
    }
    return true;
}

std::vector<ButtonActionOption> BuildButtonActionOptions(const PhoneAppRegistry& registry) {
    std::vector<ButtonActionOption> options;
    options.push_back(ButtonActionOption{
        .action = {.type = rodakos::ButtonActionType::kNone, .app_id = ""},
    });
    options.push_back(ButtonActionOption{
        .action = {.type = rodakos::ButtonActionType::kHome, .app_id = ""},
    });
    options.push_back(ButtonActionOption{
        .action = {.type = rodakos::ButtonActionType::kToggleControlCenter, .app_id = ""},
    });
    options.push_back(ButtonActionOption{
        .action = {.type = rodakos::ButtonActionType::kLock, .app_id = ""},
    });

    for (const auto& app : registry.apps()) {
        options.push_back(ButtonActionOption{
            .action = {.type = rodakos::ButtonActionType::kLaunchApp, .app_id = app.id},
        });
    }
    return options;
}
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

void SettingsApp::UpdateButtonBindingsPage() {
    if (buttons_body_ == nullptr) {
        return;
    }

    lv_obj_clean(buttons_body_);

    auto* buttons = context_ != nullptr ? context_->services().buttons() : nullptr;
    if (buttons == nullptr || !buttons->IsAvailable()) {
        auto* empty_card = CreateSettingCard(buttons_body_, 8, 76);
        CreateSettingIcon(empty_card, FONT_AWESOME_KEY);
        auto* title = CreateSettingLabel(empty_card, "No button devices");
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 28, 2);
        auto* detail = CreateSettingLabel(empty_card, "Check board_devices.yaml", true);
        lv_obj_set_style_text_font(detail, &phone_font_12, 0);
        lv_obj_align(detail, LV_ALIGN_BOTTOM_LEFT, 28, -2);
        return;
    }

    lv_coord_t y = 8;
    for (const auto& button : buttons->ListButtons()) {
        auto* title_card = CreateSettingCard(buttons_body_, y, 42);
        CreateSettingIcon(title_card, FONT_AWESOME_KEY);
        auto* title = CreateSettingLabel(title_card, button.title.c_str());
        lv_obj_align(title, LV_ALIGN_LEFT_MID, 28, 0);
        auto* device = CreateSettingLabel(title_card, button.device_name.c_str(), true);
        lv_obj_set_width(device, 110);
        lv_label_set_long_mode(device, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(device, &phone_font_12, 0);
        lv_obj_align(device, LV_ALIGN_RIGHT_MID, 0, 0);
        y += 48;

        for (rodakos::ButtonEvent event : rodakos::kButtonEvents) {
            const auto binding = buttons->GetBinding(button.id, event);

            auto* row = CreateSettingCard(buttons_body_, y, 46);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(row, new rodakos::ButtonBinding(binding));

            auto* event_label = CreateSettingLabel(
                row, rodakos::ButtonBindingService::EventLabel(event), true);
            lv_obj_set_style_text_font(event_label, &phone_font_12, 0);
            lv_obj_align(event_label, LV_ALIGN_TOP_LEFT, 0, -2);

            const std::string action_text =
                rodakos::ButtonBindingService::ActionLabel(binding.action) +
                (binding.custom ? " *" : "");
            auto* action_label = CreateSettingLabel(row, action_text.c_str());
            lv_obj_set_width(action_label, 226);
            lv_label_set_long_mode(action_label, LV_LABEL_LONG_DOT);
            lv_obj_align(action_label, LV_ALIGN_BOTTOM_LEFT, 0, 2);

            auto* arrow = lv_label_create(row);
            lv_label_set_text(arrow, ">");
            lv_obj_set_style_text_color(arrow, rodakos_theme_text_tertiary(), 0);
            lv_obj_set_style_text_font(arrow, &phone_font_18, 0);
            lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);

            lv_obj_add_event_cb(row, [](lv_event_t* e) {
                auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
                auto* row = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
                auto* binding = static_cast<rodakos::ButtonBinding*>(lv_obj_get_user_data(row));
                if (self == nullptr || binding == nullptr || self->context_ == nullptr) {
                    return;
                }
                self->ShowButtonActionDialog(*binding);
            }, LV_EVENT_CLICKED, this);

            lv_obj_add_event_cb(row, [](lv_event_t* e) {
                auto* binding = static_cast<rodakos::ButtonBinding*>(
                    lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(e))));
                delete binding;
            }, LV_EVENT_DELETE, nullptr);

            y += 52;
        }
        y += 4;
    }
}

void SettingsApp::ShowButtonActionDialog(const rodakos::ButtonBinding& binding) {
    if (button_action_dialog_ != nullptr) {
        CloseButtonActionDialog();
    }
    if (context_ == nullptr) {
        return;
    }

    button_action_dialog_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(button_action_dialog_);
    lv_obj_set_size(button_action_dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(button_action_dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(button_action_dialog_, LV_OPA_70, 0);
    lv_obj_clear_flag(button_action_dialog_, LV_OBJ_FLAG_SCROLLABLE);

    auto* dialog_box = lv_obj_create(button_action_dialog_);
    lv_obj_remove_style_all(dialog_box);
    lv_obj_set_size(dialog_box, 288, 184);
    lv_obj_center(dialog_box);
    lv_obj_set_style_bg_color(dialog_box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(dialog_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dialog_box, 8, 0);
    lv_obj_set_style_pad_all(dialog_box, 10, 0);
    lv_obj_clear_flag(dialog_box, LV_OBJ_FLAG_SCROLLABLE);

    auto* title = CreateSettingLabel(dialog_box, "Select action");
    lv_obj_set_style_text_font(title, &phone_font_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    auto* close_btn = lv_btn_create(dialog_box);
    lv_obj_set_size(close_btn, 30, 28);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, -2);
    lv_obj_set_style_bg_color(close_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_radius(close_btn, 6, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);

    auto* close_icon = lv_label_create(close_btn);
    lv_label_set_text(close_icon, FONT_AWESOME_XMARK);
    lv_obj_set_style_text_color(close_icon, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(close_icon, PhoneIconFont(), 0);
    lv_obj_center(close_icon);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
        if (self != nullptr) {
            self->CloseButtonActionDialog();
        }
    }, LV_EVENT_CLICKED, this);

    auto* subtitle = CreateSettingLabel(dialog_box, binding.title.c_str(), true);
    lv_obj_set_width(subtitle, 238);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(subtitle, &phone_font_12, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 0, 24);

    auto* list = lv_obj_create(dialog_box);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 268, 126);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t y = 0;
    for (const auto& option : BuildButtonActionOptions(context_->registry())) {
        auto* row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 268, 34);
        lv_obj_set_pos(row, 0, y);
        lv_obj_set_style_bg_color(row,
                                  SameButtonAction(option.action, binding.action)
                                      ? rodakos_theme_bg_tertiary()
                                      : rodakos_theme_bg_secondary(),
                                  0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_set_user_data(
            row,
            new std::pair<rodakos::ButtonBinding, rodakos::ButtonAction>(binding, option.action));

        auto* label = CreateSettingLabel(
            row, rodakos::ButtonBindingService::ActionLabel(option.action).c_str());
        lv_obj_set_width(label, 218);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        if (SameButtonAction(option.action, binding.action)) {
            auto* check = lv_label_create(row);
            lv_label_set_text(check, FONT_AWESOME_CHECK);
            lv_obj_set_style_text_color(check, rodakos_theme_primary(), 0);
            lv_obj_set_style_text_font(check, PhoneIconFont(), 0);
            lv_obj_align(check, LV_ALIGN_RIGHT_MID, 0, 0);
        }

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            auto* self = static_cast<SettingsApp*>(lv_event_get_user_data(e));
            auto* row = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            auto* payload = static_cast<std::pair<rodakos::ButtonBinding, rodakos::ButtonAction>*>(
                lv_obj_get_user_data(row));
            if (self != nullptr && payload != nullptr) {
                const rodakos::ButtonBinding binding_copy = payload->first;
                const rodakos::ButtonAction action_copy = payload->second;
                self->SaveButtonBindingAction(binding_copy, action_copy);
            }
        }, LV_EVENT_CLICKED, this);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            auto* payload = static_cast<std::pair<rodakos::ButtonBinding, rodakos::ButtonAction>*>(
                lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(e))));
            delete payload;
        }, LV_EVENT_DELETE, nullptr);

        y += 38;
    }
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

    if (!buttons->SetBinding(binding.button_id, binding.event, action)) {
        ui_->ShowToastUnlocked("Button binding failed");
        return;
    }

    CloseButtonActionDialog();
    UpdateButtonBindingsPage();
    ui_->ShowToastUnlocked("Button binding saved");
}
