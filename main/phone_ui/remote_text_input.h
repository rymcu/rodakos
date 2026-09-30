#pragma once

#include <lvgl.h>
#include <string>
#include <string_view>

namespace rodakos {

struct RemoteTextInputResult {
    bool accepted = false;
    const char* reason = "text_target_unavailable";
};

namespace remote_text_input_detail {

inline bool IsAvailable(lv_obj_t* object) {
    if (object == nullptr || !lv_obj_is_valid(object)) return false;
    lv_obj_t* root = lv_obj_get_screen(object);
    if (root != lv_screen_active() && root != lv_layer_top() && root != lv_layer_sys()) {
        return false;
    }
    for (lv_obj_t* current = object; current != nullptr; current = lv_obj_get_parent(current)) {
        if (lv_obj_has_flag(current, LV_OBJ_FLAG_HIDDEN) ||
            lv_obj_has_state(current, LV_STATE_DISABLED)) return false;
    }
    return true;
}

inline void FindFocusedTextarea(lv_obj_t* root, lv_obj_t*& target, bool& ambiguous) {
    if (ambiguous || !IsAvailable(root)) return;
    if (lv_obj_check_type(root, &lv_textarea_class) &&
        lv_obj_has_state(root, LV_STATE_FOCUSED)) {
        if (target != nullptr && target != root) ambiguous = true;
        else target = root;
    }
    for (uint32_t index = 0; index < lv_obj_get_child_count(root) && !ambiguous; ++index) {
        FindFocusedTextarea(lv_obj_get_child(root, index), target, ambiguous);
    }
}

inline lv_obj_t* FindKeyboard(lv_obj_t* root, lv_obj_t* textarea) {
    if (!IsAvailable(root)) return nullptr;
    if (lv_obj_check_type(root, &lv_keyboard_class) &&
        lv_keyboard_get_textarea(root) == textarea) return root;
    for (uint32_t index = 0; index < lv_obj_get_child_count(root); ++index) {
        if (auto* keyboard = FindKeyboard(lv_obj_get_child(root, index), textarea)) return keyboard;
    }
    return nullptr;
}

}  // namespace remote_text_input_detail

inline lv_obj_t* CurrentRemoteTextareaTarget() {
    if (lv_display_get_default() == nullptr) return nullptr;
    lv_obj_t* target = nullptr;
    bool ambiguous = false;
    for (auto* root : {lv_screen_active(), lv_layer_top(), lv_layer_sys()}) {
        remote_text_input_detail::FindFocusedTextarea(root, target, ambiguous);
    }
    return ambiguous ? nullptr : target;
}

// 此函数必须由 LVGL 任务调用；DataChannel 回调仅校验并排队。
inline RemoteTextInputResult ApplyRemoteTextInput(std::string_view kind,
                                                 const std::string& value) {
    lv_obj_t* target = CurrentRemoteTextareaTarget();
    if (target == nullptr) return {};
    if (kind == "text") {
        lv_textarea_add_text(target, value.c_str());
    } else if (kind == "shortcut" && value == "backspace") {
        lv_textarea_delete_char(target);
    } else if (kind == "shortcut" && value == "delete") {
        lv_textarea_delete_char_forward(target);
    } else if (kind == "shortcut" && (value == "enter" || value == "escape")) {
        if (value == "enter" && !lv_textarea_get_one_line(target)) {
            lv_textarea_add_char(target, '\n');
            return {true, nullptr};
        }
        lv_obj_t* receiver = target;
        for (auto* root : {lv_screen_active(), lv_layer_top(), lv_layer_sys()}) {
            if (auto* keyboard = remote_text_input_detail::FindKeyboard(root, target)) {
                receiver = keyboard;
                break;
            }
        }
        // READY/CANCEL 保持软键盘确认/收起语义；CLICKED 会误重新打开键盘。
        // 回调可能提交并删除字段或键盘，事件送达后不能再访问这些对象。
        lv_obj_send_event(receiver, value == "enter" ? LV_EVENT_READY : LV_EVENT_CANCEL, nullptr);
    } else {
        return {false, "unsupported_text_action"};
    }
    return {true, nullptr};
}

}  // namespace rodakos
