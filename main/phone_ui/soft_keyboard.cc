#include "phone_ui/soft_keyboard.h"

#include "phone_ui/phone_fonts.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_log.h>
#include <utility>

static const char* TAG = "SoftKeyboard";

SoftKeyboard::~SoftKeyboard() {
    Hide();
}

void SoftKeyboard::Show(lv_obj_t* textarea, std::function<void()> on_ready) {
    if (textarea == nullptr || !lv_obj_is_valid(textarea)) {
        return;
    }
    if (keyboard_ != nullptr || target_textarea_ != textarea) {
        Hide();
    }

    target_textarea_ = textarea;
    on_ready_callback_ = std::move(on_ready);

    keyboard_ = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_textarea(keyboard_, textarea);
    lv_obj_set_size(keyboard_, 320, 120);
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_set_style_bg_color(keyboard_, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_text_color(keyboard_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_bg_color(keyboard_, rodakos_theme_bg_tertiary(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard_, rodakos_theme_text_primary(), LV_PART_ITEMS);
    const auto pressed_items = static_cast<lv_style_selector_t>(
        static_cast<uint32_t>(LV_PART_ITEMS) | static_cast<uint32_t>(LV_STATE_PRESSED));
    lv_obj_set_style_bg_color(keyboard_, rodakos_theme_primary(), pressed_items);
    lv_obj_set_style_text_color(keyboard_, rodakos_theme_bg_primary(), pressed_items);
    lv_keyboard_set_mode(keyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(keyboard_, KeyboardEventHandler, LV_EVENT_ALL, this);
    if (!textarea_handler_registered_) {
        lv_obj_add_event_cb(textarea, TextareaEventHandler, LV_EVENT_CLICKED, this);
        textarea_handler_registered_ = true;
    }

    // Keep a visible text action above the 120px keyboard on the 320x240 display.
    hide_button_ = lv_btn_create(lv_scr_act());
    lv_obj_set_size(hide_button_, 52, 22);
    lv_obj_align(hide_button_, LV_ALIGN_BOTTOM_RIGHT, -4, -122);
    lv_obj_set_style_bg_color(hide_button_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(hide_button_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hide_button_, 5, 0);
    lv_obj_set_style_shadow_width(hide_button_, 0, 0);
    lv_obj_clear_flag(hide_button_, LV_OBJ_FLAG_SCROLLABLE);
    auto* hide_label = lv_label_create(hide_button_);
    lv_label_set_text(hide_label, "收起");
    lv_obj_set_style_text_color(hide_label, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(hide_label, &phone_font_12, 0);
    lv_obj_center(hide_label);
    lv_obj_add_event_cb(hide_button_, HideButtonEventHandler, LV_EVENT_CLICKED, this);
    lv_obj_move_foreground(hide_button_);

    ESP_LOGI(TAG, "Soft keyboard shown");
}

void SoftKeyboard::Hide() {
    if (target_textarea_ != nullptr && lv_obj_is_valid(target_textarea_)) {
        lv_obj_remove_event_cb_with_user_data(target_textarea_, TextareaEventHandler, this);
    }
    textarea_handler_registered_ = false;
    DeleteKeyboardObjects();
    target_textarea_ = nullptr;
    on_ready_callback_ = nullptr;
    ESP_LOGI(TAG, "Soft keyboard hidden");
}

void SoftKeyboard::Collapse() {
    DeleteKeyboardObjects();
    ESP_LOGI(TAG, "Soft keyboard collapsed");
}

void SoftKeyboard::DeleteKeyboardObjects() {
    if (hide_button_ != nullptr && lv_obj_is_valid(hide_button_)) {
        lv_obj_delete(hide_button_);
    }
    hide_button_ = nullptr;
    if (keyboard_ != nullptr && lv_obj_is_valid(keyboard_)) {
        lv_obj_delete(keyboard_);
    }
    keyboard_ = nullptr;
}

void SoftKeyboard::KeyboardEventHandler(lv_event_t* e) {
    auto* self = static_cast<SoftKeyboard*>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        const auto on_ready = self->on_ready_callback_;
        if (on_ready) {
            on_ready();
        }
        if (self->keyboard_ != nullptr) {
            self->Collapse();
        }
    } else if (code == LV_EVENT_CANCEL) {
        self->Collapse();
    }
}

void SoftKeyboard::TextareaEventHandler(lv_event_t* e) {
    auto* self = static_cast<SoftKeyboard*>(lv_event_get_user_data(e));
    if (self == nullptr || self->target_textarea_ == nullptr || self->keyboard_ != nullptr ||
        !lv_obj_is_valid(self->target_textarea_)) {
        return;
    }
    self->Show(self->target_textarea_, self->on_ready_callback_);
}

void SoftKeyboard::HideButtonEventHandler(lv_event_t* e) {
    auto* self = static_cast<SoftKeyboard*>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->Collapse();
    }
}
