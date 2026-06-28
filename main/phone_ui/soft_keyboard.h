#pragma once

#include <lvgl.h>
#include <functional>

/**
 * 软件键盘组件
 *
 * 为 WiFi 密码输入等场景提供屏幕键盘
 */
class SoftKeyboard {
public:
    SoftKeyboard() = default;
    ~SoftKeyboard();

    /**
     * 显示键盘
     * @param textarea 要输入的文本框
     * @param on_close 关闭时的回调（可选）
     */
    void Show(lv_obj_t* textarea, std::function<void()> on_close = nullptr);

    /**
     * 隐藏键盘
     */
    void Hide();

    /**
     * 键盘是否可见
     */
    bool IsVisible() const { return keyboard_ != nullptr; }

private:
    lv_obj_t* keyboard_ = nullptr;
    lv_obj_t* target_textarea_ = nullptr;
    std::function<void()> on_close_callback_;

    static void KeyboardEventHandler(lv_event_t* e);
};
