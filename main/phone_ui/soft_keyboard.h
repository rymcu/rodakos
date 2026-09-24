#pragma once

#include <lvgl.h>
#include <functional>

/**
 * 软件键盘组件。
 *
 * The keyboard's confirm key invokes the optional callback. Closing or
 * collapsing the keyboard only hides input and never submits the form.
 */
class SoftKeyboard {
public:
    SoftKeyboard() = default;
    ~SoftKeyboard();

    void Show(lv_obj_t* textarea, std::function<void()> on_ready = nullptr);
    void Hide();
    void Collapse();
    bool IsVisible() const { return keyboard_ != nullptr; }

private:
    lv_obj_t* keyboard_ = nullptr;
    lv_obj_t* hide_button_ = nullptr;
    lv_obj_t* target_textarea_ = nullptr;
    bool textarea_handler_registered_ = false;
    std::function<void()> on_ready_callback_;

    static void KeyboardEventHandler(lv_event_t* e);
    static void TextareaEventHandler(lv_event_t* e);
    static void HideButtonEventHandler(lv_event_t* e);
    void DeleteKeyboardObjects();
};
