#include "test_framework.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define private public
#include "apps/home/home_app.h"
#undef private

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "settings.h"

#include <lvgl.h>
#include <src/others/test/lv_test.h>

namespace {

using rodakos_home_ui_test::ResetSettings;
using rodakos_home_ui_test::SettingsState;

void ResetScreen() {
    lv_test_mouse_release();
    lv_test_wait(2);
    lv_indev_reset(nullptr, nullptr);
    lv_test_mouse_move_to(0, 239);
    lv_obj_clean(lv_screen_active());
    lv_test_wait(2);
}

void Pump(uint32_t milliseconds = 5) {
    lv_test_wait(milliseconds);
    lv_obj_update_layout(lv_screen_active());
}

void Click(lv_obj_t* object) {
    RODAK_CHECK(object != nullptr);
    RODAK_CHECK(lv_obj_is_valid(object));
    lv_obj_update_layout(lv_screen_active());
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    lv_test_mouse_click_at((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
    Pump();
}

void LongPress(lv_obj_t* object) {
    RODAK_CHECK(object != nullptr);
    RODAK_CHECK(lv_obj_is_valid(object));
    lv_obj_update_layout(lv_screen_active());
    lv_test_mouse_release();
    lv_test_wait(10);
    lv_test_mouse_move_to_obj(object);
    lv_test_wait(10);
    lv_test_mouse_press();
    lv_test_wait(260);
    lv_test_mouse_release();
    Pump(80);
}

void LongPressThenDrag(lv_obj_t* object, int32_t delta_x, int32_t delta_y = 0) {
    RODAK_CHECK(object != nullptr);
    RODAK_CHECK(lv_obj_is_valid(object));
    lv_obj_update_layout(lv_screen_active());
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    const int32_t start_x = (area.x1 + area.x2) / 2;
    const int32_t start_y = (area.y1 + area.y2) / 2;

    lv_test_mouse_release();
    lv_test_wait(50);
    lv_test_mouse_move_to(start_x, start_y);
    lv_test_mouse_press();
    lv_test_wait(160);
    lv_test_mouse_move_to(start_x + delta_x, start_y + delta_y);
    lv_test_wait(40);
    lv_test_mouse_release();
    Pump(80);
}

void Swipe(lv_obj_t* object, int32_t delta_x, int32_t delta_y = 0) {
    RODAK_CHECK(object != nullptr);
    RODAK_CHECK(lv_obj_is_valid(object));
    lv_obj_update_layout(lv_screen_active());
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    const int32_t start_x = (area.x1 + area.x2) / 2;
    const int32_t start_y = (area.y1 + area.y2) / 2;

    lv_test_mouse_release();
    lv_test_wait(50);
    lv_test_mouse_move_to(start_x, start_y);
    lv_test_mouse_press();
    lv_test_wait(50);
    for (int32_t step = 1; step <= 4; ++step) {
        lv_test_mouse_move_to(start_x + delta_x * step / 4,
                              start_y + delta_y * step / 4);
        lv_test_wait(40);
    }
    lv_test_mouse_release();
    Pump(100);
}

void QuickSwipe(lv_obj_t* object, int32_t delta_x, int32_t delta_y = 0) {
    RODAK_CHECK(object != nullptr);
    RODAK_CHECK(lv_obj_is_valid(object));
    lv_obj_update_layout(lv_screen_active());
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    const int32_t start_x = (area.x1 + area.x2) / 2;
    const int32_t start_y = (area.y1 + area.y2) / 2;

    lv_test_mouse_release();
    lv_test_wait(50);
    lv_test_mouse_move_to(start_x, start_y);
    lv_test_mouse_press();
    lv_test_wait(40);
    lv_test_mouse_move_to(start_x + delta_x, start_y + delta_y);
    lv_test_wait(40);
    lv_test_mouse_release();
    Pump(80);
}

void DragOutAndBack(lv_obj_t* object, int32_t delta_x, int32_t delta_y = 0) {
    RODAK_CHECK(object != nullptr);
    RODAK_CHECK(lv_obj_is_valid(object));
    lv_obj_update_layout(lv_screen_active());
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    const int32_t start_x = (area.x1 + area.x2) / 2;
    const int32_t start_y = (area.y1 + area.y2) / 2;

    lv_test_mouse_release();
    lv_test_wait(50);
    lv_test_mouse_move_to(start_x, start_y);
    lv_test_mouse_press();
    lv_test_wait(40);
    lv_test_mouse_move_to(start_x + delta_x, start_y + delta_y);
    lv_test_wait(40);
    lv_test_mouse_move_to(start_x, start_y);
    lv_test_wait(40);
    lv_test_mouse_release();
    Pump(80);
}

class ScopedLongPressTime {
public:
    explicit ScopedLongPressTime(uint16_t milliseconds)
        : indev_(lv_test_indev_get_indev(LV_INDEV_TYPE_POINTER)) {
        lv_indev_set_long_press_time(indev_, milliseconds);
    }

    ~ScopedLongPressTime() {
        lv_indev_set_long_press_time(indev_, 120);
    }

private:
    lv_indev_t* indev_;
};

lv_obj_t* FindLabel(lv_obj_t* root, std::string_view text) {
    if (root == nullptr || !lv_obj_is_valid(root)) {
        return nullptr;
    }
    if (lv_obj_check_type(root, &lv_label_class) &&
        text == lv_label_get_text(root)) {
        return root;
    }
    const uint32_t count = lv_obj_get_child_count(root);
    for (uint32_t index = 0; index < count; ++index) {
        if (auto* result = FindLabel(lv_obj_get_child(root, index), text)) {
            return result;
        }
    }
    return nullptr;
}

lv_obj_t* HomeButton(HomeApp& home, uint32_t index = 0) {
    RODAK_CHECK_FALSE(home.page_tiles_.empty());
    lv_obj_t* tile = home.page_tiles_[home.ActivePageIndex()];
    RODAK_CHECK_EQ(lv_obj_get_child_count(tile), 1U);
    lv_obj_t* grid = lv_obj_get_child(tile, 0);
    RODAK_CHECK(lv_obj_get_child_count(grid) > index);
    return lv_obj_get_child(grid, index);
}

lv_obj_t* CancelButton(HomeApp& home) {
    RODAK_CHECK(home.layout_editor_ != nullptr);
    lv_obj_t* toolbar = lv_obj_get_child(home.layout_editor_, 0);
    RODAK_CHECK(toolbar != nullptr);
    return lv_obj_get_child(toolbar, 0);
}

size_t ResidentPageCount(const HomeApp& home) {
    return static_cast<size_t>(std::count(
        home.page_populated_.begin(), home.page_populated_.end(), true));
}

struct HomeFixture {
    explicit HomeFixture(size_t app_count)
        : ui(320, 240), context(ui, navigation, registry, services, settings) {
        for (size_t index = 0; index < app_count; ++index) {
            char id[16];
            char title[24];
            std::snprintf(id, sizeof(id), "app%03u", static_cast<unsigned>(index));
            std::snprintf(title, sizeof(title), "App %03u", static_cast<unsigned>(index));
            PhoneAppDescriptor descriptor;
            descriptor.id = id;
            descriptor.title = title;
            descriptor.icon = "*";
            registry.Register(std::move(descriptor));
        }
        RODAK_CHECK(home.OnCreate(context));
        Pump();
    }

    ~HomeFixture() {
        home.OnDestroy();
        Pump();
        lv_obj_clean(lv_screen_active());
        Pump();
    }

    PhoneUi ui;
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context;
    HomeApp home;
};

void EnterArrange(HomeFixture& fixture) {
    LongPress(HomeButton(fixture.home));
    RODAK_CHECK(fixture.home.HasEditingTarget());
    RODAK_CHECK(fixture.home.layout_editor_ != nullptr);
    RODAK_CHECK(FindLabel(fixture.home.layout_editor_, "Arrange") != nullptr);
    RODAK_CHECK(fixture.navigation.launches.empty());
}

}  // namespace

RODAK_TEST("long press enters Arrange through the LVGL pointer input") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(4);

    EnterArrange(fixture);
}

RODAK_TEST("tap slop launches but one-page drags over an app do not") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(4);

    Click(HomeButton(fixture.home, 2));
    RODAK_CHECK_EQ(fixture.navigation.launches.size(), 1U);
    RODAK_CHECK_EQ(fixture.navigation.launches.front(), std::string("app002"));
    fixture.navigation.launches.clear();

    QuickSwipe(HomeButton(fixture.home, 2), 16, 0);
    RODAK_CHECK(fixture.navigation.launches.empty());
    QuickSwipe(HomeButton(fixture.home, 2), 0, 16);
    RODAK_CHECK(fixture.navigation.launches.empty());
    QuickSwipe(HomeButton(fixture.home, 2), 12, 12);
    RODAK_CHECK(fixture.navigation.launches.empty());
    RODAK_CHECK_FALSE(fixture.home.HasEditingTarget());

    QuickSwipe(HomeButton(fixture.home, 2), 0, 6);
    RODAK_CHECK_EQ(fixture.navigation.launches.size(), 1U);
    RODAK_CHECK_EQ(fixture.navigation.launches.front(), std::string("app002"));
}

RODAK_TEST("dragging after long press neither launches nor opens Arrange") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(4);

    LongPressThenDrag(HomeButton(fixture.home, 2), 20);
    RODAK_CHECK(fixture.navigation.launches.empty());
    RODAK_CHECK_FALSE(fixture.home.HasEditingTarget());
    RODAK_CHECK(fixture.home.layout_editor_ == nullptr);
}

RODAK_TEST("cumulative and returned drags stay suppressed") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(4);
    ScopedLongPressTime long_press_time(500);

    Swipe(HomeButton(fixture.home, 2), 16);
    RODAK_CHECK(fixture.navigation.launches.empty());
    RODAK_CHECK_FALSE(fixture.home.HasEditingTarget());

    DragOutAndBack(HomeButton(fixture.home, 2), 12);
    RODAK_CHECK(fixture.navigation.launches.empty());
    RODAK_CHECK_FALSE(fixture.home.HasEditingTarget());
}

RODAK_TEST("multi-page app swipes respect boundaries without launching") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(24);

    RODAK_CHECK_EQ(fixture.home.ActivePageIndex(), 0U);
    QuickSwipe(HomeButton(fixture.home, 2), 40);
    RODAK_CHECK_EQ(fixture.home.ActivePageIndex(), 0U);
    RODAK_CHECK(fixture.navigation.launches.empty());

    Swipe(HomeButton(fixture.home, 2), -160);
    Pump(1200);
    RODAK_CHECK_EQ(fixture.home.ActivePageIndex(), 1U);
    RODAK_CHECK(fixture.navigation.launches.empty());
    RODAK_CHECK_FALSE(fixture.home.HasEditingTarget());

    QuickSwipe(HomeButton(fixture.home, 2), -40);
    RODAK_CHECK_EQ(fixture.home.ActivePageIndex(), 1U);
    RODAK_CHECK(fixture.navigation.launches.empty());

    Swipe(HomeButton(fixture.home, 2), 160);
    Pump(1200);
    RODAK_CHECK_EQ(fixture.home.ActivePageIndex(), 0U);
    RODAK_CHECK(fixture.navigation.launches.empty());
    RODAK_CHECK_FALSE(fixture.home.HasEditingTarget());
}

RODAK_TEST("a vertical quick swipe on one page is not an app tap") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(4);

    QuickSwipe(HomeButton(fixture.home, 2), 0, 16);
    RODAK_CHECK(fixture.navigation.launches.empty());
    RODAK_CHECK_FALSE(fixture.home.HasEditingTarget());
}

RODAK_TEST("Cancel discards a moved draft without writing Settings") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(4);
    const auto original = fixture.home.layout_;

    EnterArrange(fixture);
    Click(fixture.home.editor_next_button_);
    RODAK_CHECK_NE(fixture.home.draft_layout_, original);
    Click(CancelButton(fixture.home));

    RODAK_CHECK_FALSE(fixture.home.HasEditingTarget());
    RODAK_CHECK_EQ(fixture.home.layout_, original);
    RODAK_CHECK_EQ(SettingsState().write_calls, 0);
    RODAK_CHECK_EQ(SettingsState().commit_calls, 0);
    RODAK_CHECK(rodakos_home_ui_test::GetCommittedSetting("home", "layout").empty());
}

RODAK_TEST("Done persists one changed draft with one guarded commit") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(4);
    const auto original = fixture.home.layout_;

    EnterArrange(fixture);
    Click(fixture.home.editor_next_button_);
    RODAK_CHECK_NE(fixture.home.draft_layout_, original);
    Click(fixture.home.editor_done_button_);

    RODAK_CHECK_FALSE(fixture.home.HasEditingTarget());
    RODAK_CHECK_NE(fixture.home.layout_, original);
    RODAK_CHECK_EQ(fixture.home.layout_.revision, 1U);
    RODAK_CHECK_EQ(SettingsState().write_calls, 1);
    RODAK_CHECK_EQ(SettingsState().commit_calls, 1);
    RODAK_CHECK_FALSE(
        rodakos_home_ui_test::GetCommittedSetting("home", "layout").empty());
}

RODAK_TEST("repeated Home closes Arrange and drops its draft") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(4);
    const auto original = fixture.home.layout_;

    EnterArrange(fixture);
    Click(fixture.home.editor_next_button_);
    RODAK_CHECK_NE(fixture.home.draft_layout_, original);
    RODAK_CHECK(fixture.home.OnHomeRequested());

    RODAK_CHECK_FALSE(fixture.home.HasEditingTarget());
    RODAK_CHECK_EQ(fixture.home.layout_, original);
    RODAK_CHECK_EQ(SettingsState().write_calls, 0);
    RODAK_CHECK_EQ(SettingsState().commit_calls, 0);
}

RODAK_TEST("theme rebuild retains the unsaved Arrange session") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(4);

    EnterArrange(fixture);
    Click(fixture.home.editor_next_button_);
    const auto draft = fixture.home.draft_layout_;
    rodakos_home_ui_test::SetCommittedSetting("display", "theme", "light");
    RODAK_CHECK_EQ(
        lv_obj_send_event(fixture.home.tileview_, LV_EVENT_SCROLL_END, nullptr),
        LV_RESULT_OK);
    RODAK_CHECK(fixture.home.page_window_refresh_pending_);

    RODAK_CHECK(fixture.home.OnThemeChanged(fixture.context));
    Pump();

    RODAK_CHECK_FALSE(fixture.home.page_window_refresh_pending_);
    RODAK_CHECK(fixture.home.HasEditingTarget());
    RODAK_CHECK_EQ(fixture.home.draft_layout_, draft);
    RODAK_CHECK(FindLabel(fixture.home.layout_editor_, "Arrange") != nullptr);
    RODAK_CHECK_EQ(rodakos_theme_get()->bg_primary, 0xFFFFFFU);
    RODAK_CHECK_EQ(SettingsState().write_calls, 0);
    RODAK_CHECK_EQ(SettingsState().commit_calls, 0);
}

RODAK_TEST("SoftKeyboard occupies the bottom 320 by 120 pixels") {
    ResetScreen();
    ResetSettings();

    auto* textarea = lv_textarea_create(lv_screen_active());
    SoftKeyboard keyboard;
    keyboard.Show(textarea);
    Pump();

    lv_obj_t* keyboard_object = nullptr;
    const uint32_t child_count = lv_obj_get_child_count(lv_screen_active());
    for (uint32_t index = 0; index < child_count; ++index) {
        lv_obj_t* child = lv_obj_get_child(lv_screen_active(), index);
        if (lv_obj_check_type(child, &lv_keyboard_class)) {
            keyboard_object = child;
            break;
        }
    }
    RODAK_CHECK(keyboard_object != nullptr);
    RODAK_CHECK_EQ(lv_obj_get_width(keyboard_object), 320);
    RODAK_CHECK_EQ(lv_obj_get_height(keyboard_object), 120);
    lv_area_t area;
    lv_obj_get_coords(keyboard_object, &area);
    RODAK_CHECK_EQ(area.x1, 0);
    RODAK_CHECK_EQ(area.y1, 120);
    RODAK_CHECK_EQ(area.x2, 319);
    RODAK_CHECK_EQ(area.y2, 239);

    keyboard.Hide();
    RODAK_CHECK_FALSE(keyboard.IsVisible());
}

RODAK_TEST("96 apps use eight managed pages without All Apps") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(96);

    RODAK_CHECK_EQ(fixture.home.page_tiles_.size(), 8U);
    RODAK_CHECK_FALSE(fixture.home.projection_.has_all_apps());
    const auto window = rodakos::ResolveHomePageRenderWindow(
        fixture.home.page_tiles_.size(), fixture.home.ActivePageIndex());
    RODAK_CHECK_EQ(ResidentPageCount(fixture.home), window.page_count);
    for (size_t page = 0; page < fixture.home.page_tiles_.size(); ++page) {
        RODAK_CHECK_EQ(fixture.home.page_populated_[page], window.Contains(page));
    }
}

RODAK_TEST("97 apps expose All Apps and asynchronously retain only adjacent pages") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(97);

    RODAK_CHECK_EQ(fixture.home.page_tiles_.size(), 8U);
    RODAK_CHECK(fixture.home.projection_.has_all_apps());
    RODAK_CHECK_EQ(fixture.home.projection_.managed_items.size(), 95U);
    RODAK_CHECK_EQ(fixture.home.projection_.overflow_items.size(), 2U);

    lv_tileview_set_tile(fixture.home.tileview_, fixture.home.page_tiles_[7], LV_ANIM_OFF);
    RODAK_CHECK_EQ(
        lv_obj_send_event(fixture.home.tileview_, LV_EVENT_SCROLL_END, nullptr),
        LV_RESULT_OK);
    RODAK_CHECK(fixture.home.page_window_refresh_pending_);
    Pump(10);

    RODAK_CHECK_FALSE(fixture.home.page_window_refresh_pending_);
    RODAK_CHECK_EQ(fixture.home.ActivePageIndex(), 7U);
    RODAK_CHECK_EQ(ResidentPageCount(fixture.home), 2U);
    for (size_t page = 0; page < fixture.home.page_tiles_.size(); ++page) {
        const bool expected = page == 6 || page == 7;
        RODAK_CHECK_EQ(fixture.home.page_populated_[page], expected);
        RODAK_CHECK_EQ(lv_obj_get_child_count(fixture.home.page_tiles_[page]),
                       expected ? 1U : 0U);
    }

    lv_obj_t* all_apps_label = FindLabel(fixture.home.page_tiles_[7], "All Apps");
    RODAK_CHECK(all_apps_label != nullptr);
    Click(lv_obj_get_parent(all_apps_label));
    RODAK_CHECK_EQ(fixture.home.collection_state_.kind,
                   HomeApp::CollectionKind::kAllApps);
    RODAK_CHECK(FindLabel(fixture.home.collection_view_, "All Apps") != nullptr);
}
