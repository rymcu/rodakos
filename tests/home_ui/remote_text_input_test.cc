#include "test_framework.h"

#include "phone_ui/remote_text_input.h"
#include "phone_ui/soft_keyboard.h"

#include <lvgl.h>
#include <src/others/test/lv_test.h>
#include <string>

namespace {

void ResetTextScreen() {
    lv_test_mouse_release();
    lv_test_wait(2);
    lv_indev_reset(nullptr, nullptr);
    lv_obj_clean(lv_screen_active());
    lv_obj_clean(lv_layer_top());
    lv_test_wait(2);
}

lv_obj_t* CreateTextarea(lv_obj_t* parent = nullptr) {
    auto* textarea = lv_textarea_create(parent != nullptr ? parent : lv_screen_active());
    lv_textarea_set_text(textarea, "");
    lv_textarea_set_one_line(textarea, true);
    return textarea;
}

}  // namespace

RODAK_TEST("remote text enters a pointer-focused textarea without a group") {
    ResetTextScreen();
    auto* textarea = CreateTextarea();
    lv_obj_set_size(textarea, 250, 35);
    lv_obj_set_pos(textarea, 12, 35);
    lv_test_mouse_click_at(32, 52);
    lv_test_wait(10);
    RODAK_CHECK(lv_obj_get_group(textarea) == nullptr);
    RODAK_CHECK(lv_obj_has_state(textarea, LV_STATE_FOCUSED));

    const auto result = rodakos::ApplyRemoteTextInput("text", "你好 BigSmart🙂");

    RODAK_CHECK(result.accepted);
    RODAK_CHECK(result.reason == nullptr);
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(textarea)), "你好 BigSmart🙂");
}

RODAK_TEST("a programmatically opened SoftKeyboard exposes its editing target") {
    ResetTextScreen();
    auto* textarea = CreateTextarea();
    SoftKeyboard keyboard;
    keyboard.Show(textarea);
    RODAK_CHECK(lv_obj_get_group(textarea) == nullptr);
    RODAK_CHECK(rodakos::CurrentRemoteTextareaTarget() == textarea);
    RODAK_CHECK(rodakos::ApplyRemoteTextInput("text", "输入中文").accepted);
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(textarea)), "输入中文");

    keyboard.Collapse();
    RODAK_CHECK(rodakos::ApplyRemoteTextInput("text", "继续").accepted);
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(textarea)), "输入中文继续");
    keyboard.Hide();
    const auto rejected = rodakos::ApplyRemoteTextInput("text", "过期");
    RODAK_CHECK_FALSE(rejected.accepted);
    RODAK_CHECK_EQ(std::string(rejected.reason), "text_target_unavailable");
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(textarea)), "输入中文继续");
}

RODAK_TEST("SoftKeyboard switches remote text to the new field and clears the old focus") {
    ResetTextScreen();
    auto* first = CreateTextarea();
    auto* second = CreateTextarea();
    SoftKeyboard keyboard;
    keyboard.Show(first);
    RODAK_CHECK(rodakos::ApplyRemoteTextInput("text", "first").accepted);
    keyboard.Collapse();
    keyboard.Show(second);
    RODAK_CHECK_FALSE(lv_obj_has_state(first, LV_STATE_FOCUSED));
    RODAK_CHECK(rodakos::ApplyRemoteTextInput("text", "第二").accepted);
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(first)), "first");
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(second)), "第二");
}

RODAK_TEST("remote text rejects an unfocused field and non-text focus") {
    ResetTextScreen();
    auto* textarea = CreateTextarea();
    auto* button = lv_button_create(lv_screen_active());
    lv_obj_add_state(button, LV_STATE_FOCUSED);
    for (const std::string action : {"text", "backspace", "delete", "enter", "escape"}) {
        const auto result = rodakos::ApplyRemoteTextInput(
            action == "text" ? "text" : "shortcut", action == "text" ? "ignored" : action);
        RODAK_CHECK_FALSE(result.accepted);
        RODAK_CHECK_EQ(std::string(result.reason), "text_target_unavailable");
    }
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(textarea)), "");
}

RODAK_TEST("remote text excludes hidden and disabled parent subtrees") {
    ResetTextScreen();
    auto* parent = lv_obj_create(lv_screen_active());
    auto* textarea = CreateTextarea(parent);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    lv_obj_add_flag(parent, LV_OBJ_FLAG_HIDDEN);
    RODAK_CHECK_FALSE(rodakos::ApplyRemoteTextInput("text", "hidden").accepted);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(parent, LV_STATE_DISABLED);
    RODAK_CHECK_FALSE(rodakos::ApplyRemoteTextInput("text", "disabled").accepted);
    lv_obj_remove_state(parent, LV_STATE_DISABLED);
    lv_obj_add_state(textarea, LV_STATE_DISABLED);
    RODAK_CHECK_FALSE(rodakos::ApplyRemoteTextInput("text", "disabled").accepted);
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(textarea)), "");
}

RODAK_TEST("remote text rejects ambiguous focus and supports a focused top-layer field") {
    ResetTextScreen();
    auto* first = CreateTextarea();
    auto* second = CreateTextarea(lv_layer_top());
    lv_obj_add_state(first, LV_STATE_FOCUSED);
    lv_obj_add_state(second, LV_STATE_FOCUSED);
    RODAK_CHECK(rodakos::CurrentRemoteTextareaTarget() == nullptr);
    RODAK_CHECK_FALSE(rodakos::ApplyRemoteTextInput("text", "ambiguous").accepted);
    lv_obj_remove_state(first, LV_STATE_FOCUSED);
    RODAK_CHECK(rodakos::ApplyRemoteTextInput("text", "overlay").accepted);
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(first)), "");
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(second)), "overlay");
}

RODAK_TEST("remote text supports LVGL group focus without arbitrary field fallback") {
    ResetTextScreen();
    auto* textarea = CreateTextarea();
    auto* group = lv_group_create();
    lv_group_add_obj(group, textarea);
    RODAK_CHECK(lv_obj_has_state(textarea, LV_STATE_FOCUSED));
    RODAK_CHECK(rodakos::ApplyRemoteTextInput("text", "group").accepted);
    lv_group_delete(group);
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(textarea)), "group");
}

RODAK_TEST("remote backspace and delete preserve UTF8 and use opposite cursor sides") {
    ResetTextScreen();
    auto* textarea = CreateTextarea();
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    lv_textarea_set_text(textarea, "甲🙂乙");
    lv_textarea_set_cursor_pos(textarea, 2);
    RODAK_CHECK(rodakos::ApplyRemoteTextInput("shortcut", "backspace").accepted);
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(textarea)), "甲乙");
    RODAK_CHECK_EQ(lv_textarea_get_cursor_pos(textarea), 1U);
    RODAK_CHECK(rodakos::ApplyRemoteTextInput("shortcut", "delete").accepted);
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(textarea)), "甲");
    RODAK_CHECK_EQ(lv_textarea_get_cursor_pos(textarea), 1U);
}

RODAK_TEST("remote enter confirms SoftKeyboard and escape only collapses it") {
    ResetTextScreen();
    auto* textarea = CreateTextarea();
    int ready_calls = 0;
    SoftKeyboard keyboard;
    keyboard.Show(textarea, [&ready_calls]() { ++ready_calls; });
    RODAK_CHECK(rodakos::ApplyRemoteTextInput("shortcut", "escape").accepted);
    RODAK_CHECK_FALSE(keyboard.IsVisible());
    RODAK_CHECK_EQ(ready_calls, 0);
    RODAK_CHECK(rodakos::ApplyRemoteTextInput("shortcut", "enter").accepted);
    RODAK_CHECK_EQ(ready_calls, 1);
    RODAK_CHECK_FALSE(keyboard.IsVisible());
    keyboard.Show(textarea, [&ready_calls]() { ++ready_calls; });
    RODAK_CHECK(rodakos::ApplyRemoteTextInput("shortcut", "enter").accepted);
    RODAK_CHECK_EQ(ready_calls, 2);
    RODAK_CHECK_FALSE(keyboard.IsVisible());
}

RODAK_TEST("remote enter adds a line break for a multiline textarea") {
    ResetTextScreen();
    auto* textarea = CreateTextarea();
    lv_textarea_set_one_line(textarea, false);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    lv_textarea_set_text(textarea, "first");
    lv_textarea_set_cursor_pos(textarea, LV_TEXTAREA_CURSOR_LAST);
    RODAK_CHECK(rodakos::ApplyRemoteTextInput("shortcut", "enter").accepted);
    RODAK_CHECK_EQ(std::string(lv_textarea_get_text(textarea)), "first\n");
}

RODAK_TEST("deleting a SoftKeyboard target removes the keyboard and stale remote focus") {
    ResetTextScreen();
    auto* textarea = CreateTextarea();
    SoftKeyboard keyboard;
    keyboard.Show(textarea);
    lv_obj_delete(textarea);
    RODAK_CHECK_FALSE(keyboard.IsVisible());
    RODAK_CHECK_FALSE(rodakos::ApplyRemoteTextInput("text", "stale").accepted);
}
