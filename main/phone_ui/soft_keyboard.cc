#include "phone_ui/soft_keyboard.h"
#include "phone_ui/rodakos_theme.h"
#include <esp_log.h>

static const char* TAG = "SoftKeyboard";

SoftKeyboard::~SoftKeyboard() {
    Hide();
}

void SoftKeyboard::Show(lv_obj_t* textarea, std::function<void()> on_close) {
    if (keyboard_ != nullptr) {
        Hide();  // 先隐藏旧的
    }

    target_textarea_ = textarea;
    on_close_callback_ = on_close;

    // 创建键盘（使用 LVGL 内置键盘）
    keyboard_ = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_textarea(keyboard_, textarea);

    // 设置键盘大小和位置（占据屏幕下半部分）
    lv_obj_set_size(keyboard_, 320, 120);
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);

    // 应用主题颜色
    lv_obj_set_style_bg_color(keyboard_, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_text_color(keyboard_, rodakos_theme_text_primary(), 0);

    // 按键样式
    lv_obj_set_style_bg_color(keyboard_, rodakos_theme_bg_tertiary(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard_, rodakos_theme_text_primary(), LV_PART_ITEMS);

    // 按键按下效果
    lv_obj_set_style_bg_color(keyboard_, rodakos_theme_primary(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(keyboard_, rodakos_theme_bg_primary(), LV_PART_ITEMS | LV_STATE_PRESSED);

    // 设置键盘模式
    lv_keyboard_set_mode(keyboard_, LV_KEYBOARD_MODE_TEXT_LOWER);

    // 注册事件
    lv_obj_add_event_cb(keyboard_, KeyboardEventHandler, LV_EVENT_ALL, this);

    ESP_LOGI(TAG, "Soft keyboard shown");
}

void SoftKeyboard::Hide() {
    if (keyboard_ != nullptr && lv_obj_is_valid(keyboard_)) {
        lv_obj_delete(keyboard_);
        keyboard_ = nullptr;
    }
    target_textarea_ = nullptr;
    on_close_callback_ = nullptr;

    ESP_LOGI(TAG, "Soft keyboard hidden");
}

void SoftKeyboard::KeyboardEventHandler(lv_event_t* e) {
    auto* self = static_cast<SoftKeyboard*>(lv_event_get_user_data(e));
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        // 用户按下 "OK" 或 "Close" 按钮
        if (self->on_close_callback_) {
            self->on_close_callback_();
        }
        self->Hide();
    }
}
