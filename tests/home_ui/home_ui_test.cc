#include "phone_os/resource_failure_injection.h"
#include "test_framework.h"
#include "framebuffer_test_helpers.h"

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
#include "phone_os/touch_pointer_state.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
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
    explicit HomeFixture(size_t app_count, std::shared_ptr<uint8_t> wallpaper = {})
        : ui(320, 240), context(ui, navigation, registry, services, settings) {
        if (wallpaper) ui.SetWallpaper(std::move(wallpaper), 320, 240);
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

class CachedBridgePointer {
public:
    CachedBridgePointer() : indev_(lv_indev_create()) {
        RODAK_CHECK(indev_ != nullptr);
        lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
        lv_indev_set_mode(indev_, LV_INDEV_MODE_EVENT);
        lv_indev_set_disp(indev_, lv_display_get_default());
        lv_indev_set_driver_data(indev_, this);
        lv_indev_set_read_cb(indev_, [](lv_indev_t* indev, lv_indev_data_t* data) {
            auto* self = static_cast<CachedBridgePointer*>(lv_indev_get_driver_data(indev));
            self->cancelled_ = self->state_.Read(self->local_pressed_, self->local_point_,
                                               self->remote_pressed_, self->remote_point_, *data);
            if (self->cancelled_) lv_indev_reset(indev, nullptr);
        });
    }

    ~CachedBridgePointer() { lv_indev_delete(indev_); }

    void Local(bool pressed, const lv_point_t& point) {
        local_pressed_ = pressed;
        local_point_ = point;
        lv_indev_read(indev_);
        if (cancelled_) lv_indev_read(indev_);
        Pump(20);
    }

    void Remote(bool pressed, const lv_point_t& point) {
        remote_pressed_ = pressed;
        remote_point_ = point;
        lv_indev_read(indev_);
        if (cancelled_) lv_indev_read(indev_);
        Pump(20);
    }

private:
    lv_indev_t* indev_;
    rodakos::TouchPointerState state_;
    bool cancelled_ = false;
    bool local_pressed_ = false;
    lv_point_t local_point_ = {0, 0};
    bool remote_pressed_ = false;
    lv_point_t remote_point_ = {0, 0};
};

class FixedBatteryProvider final : public rodakos::BatteryStateProvider {
public:
    explicit FixedBatteryProvider(rodakos::BatterySnapshot snapshot)
        : snapshot_(snapshot) {}

    rodakos::BatterySnapshot Read() override { return snapshot_; }

private:
    rodakos::BatterySnapshot snapshot_;
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

RODAK_TEST("status bar displays the current battery level") {
    ResetScreen();
    ResetSettings();
    FixedBatteryProvider battery(rodakos::BatterySnapshot{
        .level_percent = 86,
        .voltage_mv = 4100,
        .charging = false,
        .charging_valid = true,
    });
    HomeFixture fixture(4);
    fixture.services.SetBattery(&battery);

    fixture.home.UpdateBatteryStatus();

    RODAK_CHECK_EQ(std::string(lv_label_get_text(fixture.home.battery_label_)), "86%");
    RODAK_CHECK_EQ(std::string(lv_label_get_text(fixture.home.battery_icon_)),
                   FONT_AWESOME_BATTERY_FULL);
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

RODAK_TEST("Home renders a retained RGB565 wallpaper behind controls and clears it on builtin restore") {
    ResetScreen();
    ResetSettings();
    const auto previous_theme = *rodakos_theme_get();
    rodakos_theme_init(RODAKOS_THEME_DARK);
    bool released = false;
    std::shared_ptr<uint8_t> wallpaper(new uint8_t[320 * 240 * 2], [&released](uint8_t* data) {
        delete[] data;
        released = true;
    });
    for (size_t index = 0; index < 320U * 240U; ++index) {
        wallpaper.get()[index * 2] = 0;
        wallpaper.get()[index * 2 + 1] = 0xf8;
    }
    {
        HomeFixture fixture(4, wallpaper);
        const auto* descriptor = fixture.ui.wallpaper();
        RODAK_CHECK(descriptor != nullptr);
        RODAK_CHECK_EQ(descriptor->header.cf, LV_COLOR_FORMAT_RGB565);
        RODAK_CHECK_EQ(descriptor->header.stride, 640U);
        RODAK_CHECK_EQ(descriptor->data_size, 320U * 240U * 2U);
        auto* image = lv_obj_get_child(fixture.home.root_, 0);
        RODAK_CHECK(lv_obj_check_type(image, &lv_image_class));
        RODAK_CHECK_EQ(lv_image_get_src(image), descriptor);
        RODAK_CHECK_FALSE(lv_obj_has_flag(image, LV_OBJ_FLAG_CLICKABLE));
        RODAK_CHECK(lv_obj_has_flag(image, LV_OBJ_FLAG_IGNORE_LAYOUT));
        RODAK_CHECK_EQ(lv_obj_get_style_bg_opa(fixture.home.body_, 0), LV_OPA_50);
        wallpaper.reset();
        RODAK_CHECK_FALSE(released);
        const uint32_t pixel = rodakos_home_ui_test::FramebufferPixel(0, 120);
        RODAK_CHECK((pixel >> 16) > 80U);
        RODAK_CHECK((pixel >> 16) > ((pixel >> 8) & 255U));
        RODAK_CHECK_EQ((pixel >> 8) & 255U, pixel & 255U);

        fixture.home.OnDestroy();
        fixture.ui.SetWallpaper({}, 0, 0);
        RODAK_CHECK(released);
        RODAK_CHECK(fixture.ui.wallpaper() == nullptr);
        RODAK_CHECK(fixture.home.OnCreate(fixture.context));
        Pump();
        RODAK_CHECK_FALSE(lv_obj_check_type(lv_obj_get_child(fixture.home.root_, 0), &lv_image_class));
        RODAK_CHECK_EQ(lv_obj_get_style_bg_opa(fixture.home.body_, 0), LV_OPA_TRANSP);
        RODAK_CHECK_EQ(rodakos_home_ui_test::FramebufferPixel(0, 120), 0U);
    }
    rodakos_theme_set_custom(&previous_theme);
}

RODAK_TEST("Home and Phone components share every remote theme preset and custom primary") {
    ResetScreen();
    ResetSettings();
    const auto previous_theme = *rodakos_theme_get();
    for (const char* preset : {"dark", "light", "blue", "green"}) {
        rodakos_theme_init_from_name(preset);
        const auto preset_theme = *rodakos_theme_get();
        const uint32_t primary = std::string_view(preset) == "light" ? 0x000000U : 0xffffffU;
        rodakos_theme_apply_preset_primary(preset, primary);
        {
            HomeFixture fixture(4);
            fixture.ui.SyncThemeName(preset);
            const auto& theme = fixture.ui.theme();
            RODAK_CHECK_EQ(rodakos_theme_get()->primary, primary);
            RODAK_CHECK_EQ(rodakos_theme_get()->bg_primary, preset_theme.bg_primary);
            RODAK_CHECK(lv_color_eq(theme.background, lv_color_hex(preset_theme.bg_primary)));
            RODAK_CHECK(lv_color_eq(theme.accent, lv_color_hex(primary)));
            RODAK_CHECK(lv_color_eq(theme.accent_text, lv_color_hex(primary == 0 ? 0xffffff : 0)));
            RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_color(fixture.home.root_, 0), theme.background));
            RODAK_CHECK(lv_color_eq(lv_obj_get_style_text_color(fixture.home.clock_label_, 0), theme.text_primary));
            auto* button = PhoneCreateTextButton(fixture.ui, fixture.home.root_, "Action", 80, 30);
            RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_color(button, 0), theme.accent));
            RODAK_CHECK(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(button, 0), 0), theme.accent_text));

            fixture.home.OnDestroy();
            fixture.ui.SetThemeName("light");
            RODAK_CHECK(fixture.home.OnCreate(fixture.context));
            RODAK_CHECK(lv_color_eq(fixture.ui.theme().accent, rodakos_theme_primary()));
            RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_color(fixture.home.root_, 0), rodakos_theme_bg_primary()));
        }
    }
    rodakos_theme_set_custom(&previous_theme);
}

RODAK_TEST("offline local tap keeps its release position through the touch bridge") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(4);
    CachedBridgePointer pointer;
    lv_area_t area;
    lv_obj_get_coords(HomeButton(fixture.home, 2), &area);
    const lv_point_t center = {(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};

    pointer.Local(true, center);
    pointer.Local(false, center);

    RODAK_CHECK_EQ(fixture.navigation.launches.size(), 1U);
    RODAK_CHECK_EQ(fixture.navigation.launches.front(), std::string("app002"));
}

RODAK_TEST("local tap ignores the previous remote release position") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(4);
    CachedBridgePointer pointer;
    pointer.Remote(true, {0, 239});
    pointer.Remote(false, {0, 239});
    lv_area_t area;
    lv_obj_get_coords(HomeButton(fixture.home, 2), &area);
    const lv_point_t center = {(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};

    pointer.Local(true, center);
    pointer.Local(false, center);

    RODAK_CHECK_EQ(fixture.navigation.launches.size(), 1U);
    RODAK_CHECK_EQ(fixture.navigation.launches.front(), std::string("app002"));
}

RODAK_TEST("remote release retains the last delivered coordinate") {
    rodakos::TouchPointerState state;
    lv_indev_data_t data = {};
    state.Read(false, {211, 80}, true, {160, 120}, data);
    RODAK_CHECK_EQ(data.state, LV_INDEV_STATE_PRESSED);
    state.Read(false, {211, 80}, false, {0, 0}, data);
    RODAK_CHECK_EQ(data.state, LV_INDEV_STATE_RELEASED);
    RODAK_CHECK_EQ(data.point.x, 160);
    RODAK_CHECK_EQ(data.point.y, 120);
}

RODAK_TEST("recreated Home resolves the restored tile offset before paging") {
    ResetScreen();
    ResetSettings();

    {
        HomeFixture fixture(13);
        lv_obj_update_layout(fixture.home.tileview_);
        lv_tileview_set_tile(
            fixture.home.tileview_, fixture.home.page_tiles_[0], LV_ANIM_OFF);
        RODAK_CHECK_EQ(
            lv_obj_send_event(fixture.home.tileview_, LV_EVENT_SCROLL_END, nullptr),
            LV_RESULT_OK);
        Pump();
        RODAK_CHECK_EQ(fixture.home.ActivePageIndex(), 0U);

        Swipe(HomeButton(fixture.home), -160);
        Pump(1200);
        RODAK_CHECK_EQ(fixture.home.ActivePageIndex(), 1U);
    }

    // 模块级 HomePageSession 现在恢复到第二页。仅恢复 tile 索引还不够，
    // viewport 也必须定位到百分比坐标解析后的 x，否则下一次滑动会把方向
    // 应用到第一页的旧偏移量。
    HomeFixture recreated(13);
    const int32_t page_width = lv_obj_get_content_width(recreated.home.tileview_);
    RODAK_CHECK_EQ(recreated.home.ActivePageIndex(), 1U);
    RODAK_CHECK(lv_obj_get_scroll_x(recreated.home.tileview_) >= page_width - 2);
    RODAK_CHECK_EQ(lv_obj_get_scroll_dir(recreated.home.tileview_), LV_DIR_LEFT);

    Swipe(HomeButton(recreated.home), 160);
    Pump(1200);
    RODAK_CHECK_EQ(recreated.home.ActivePageIndex(), 0U);
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
    fixture.ui.SetThemeName("light");
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

RODAK_TEST("SoftKeyboard collapse never submits and text field reopens it") {
    ResetScreen();
    ResetSettings();

    auto* textarea = lv_textarea_create(lv_screen_active());
    int ready_calls = 0;
    SoftKeyboard keyboard;
    keyboard.Show(textarea, [&ready_calls]() { ++ready_calls; });
    Pump();
    RODAK_CHECK(keyboard.IsVisible());
    RODAK_CHECK(FindLabel(lv_screen_active(), "收起") != nullptr);

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
    lv_obj_send_event(keyboard_object, LV_EVENT_CANCEL, nullptr);
    Pump();
    RODAK_CHECK_FALSE(keyboard.IsVisible());
    RODAK_CHECK_EQ(ready_calls, 0);

    lv_obj_send_event(textarea, LV_EVENT_CLICKED, nullptr);
    Pump();
    RODAK_CHECK(keyboard.IsVisible());
    keyboard.Hide();
    RODAK_CHECK_FALSE(keyboard.IsVisible());
}

RODAK_TEST("SoftKeyboard rebinds a collapsed keyboard when switching text fields") {
    ResetScreen();
    ResetSettings();

    auto* first = lv_textarea_create(lv_screen_active());
    auto* second = lv_textarea_create(lv_screen_active());
    int first_ready_calls = 0;
    int second_ready_calls = 0;
    SoftKeyboard keyboard;
    keyboard.Show(first, [&first_ready_calls]() { ++first_ready_calls; });
    keyboard.Collapse();
    keyboard.Show(second, [&second_ready_calls]() { ++second_ready_calls; });
    keyboard.Collapse();

    lv_obj_send_event(first, LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_FALSE(keyboard.IsVisible());
    lv_obj_send_event(second, LV_EVENT_CLICKED, nullptr);
    Pump();
    RODAK_CHECK(keyboard.IsVisible());

    lv_obj_t* keyboard_object = nullptr;
    for (uint32_t index = 0; index < lv_obj_get_child_count(lv_screen_active()); ++index) {
        lv_obj_t* child = lv_obj_get_child(lv_screen_active(), index);
        if (lv_obj_check_type(child, &lv_keyboard_class)) {
            keyboard_object = child;
            break;
        }
    }
    RODAK_CHECK(keyboard_object != nullptr);
    lv_obj_send_event(keyboard_object, LV_EVENT_READY, nullptr);
    RODAK_CHECK_EQ(first_ready_calls, 0);
    RODAK_CHECK_EQ(second_ready_calls, 1);
    RODAK_CHECK_FALSE(keyboard.IsVisible());

    keyboard.Hide();
    lv_obj_send_event(first, LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(second, LV_EVENT_CLICKED, nullptr);
    RODAK_CHECK_FALSE(keyboard.IsVisible());
}

RODAK_TEST("SoftKeyboard ready is distinct from collapse") {
    ResetScreen();
    ResetSettings();

    auto* textarea = lv_textarea_create(lv_screen_active());
    int ready_calls = 0;
    SoftKeyboard keyboard;
    keyboard.Show(textarea, [&ready_calls]() { ++ready_calls; });
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
    lv_obj_send_event(keyboard_object, LV_EVENT_READY, nullptr);
    Pump();
    RODAK_CHECK_EQ(ready_calls, 1);
    RODAK_CHECK_FALSE(keyboard.IsVisible());
    keyboard.Hide();
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

RODAK_TEST("failed neighbor page allocation preserves active page and allows retry") {
    ResetScreen();
    ResetSettings();
    HomeFixture fixture(25);
    fixture.home.CancelPendingPageWindowRefresh();
    lv_tileview_set_tile(fixture.home.tileview_, fixture.home.page_tiles_[0], LV_ANIM_OFF);
    RODAK_CHECK(fixture.home.RefreshHomePageWindow(0));
    fixture.home.CancelPendingPageWindowRefresh();
    // Move to the already resident middle page; the third page has never been populated.
    lv_tileview_set_tile(fixture.home.tileview_, fixture.home.page_tiles_[1], LV_ANIM_OFF);
    RODAK_CHECK(rodakos::ArmResourceFailure("home_page"));
    RODAK_CHECK(fixture.home.RefreshHomePageWindow(1));
    RODAK_CHECK(fixture.home.page_populated_[1]);
    RODAK_CHECK_FALSE(fixture.home.page_populated_[2]);
    RODAK_CHECK_EQ(lv_obj_get_child_count(fixture.home.page_tiles_[2]), 0U);
    RODAK_CHECK_EQ(lv_obj_get_scroll_dir(fixture.home.tileview_), LV_DIR_LEFT);
    RODAK_CHECK(fixture.home.RefreshHomePageWindow(1));
    RODAK_CHECK(fixture.home.page_populated_[2]);
    RODAK_CHECK_EQ(lv_obj_get_child_count(fixture.home.page_tiles_[2]), 1U);
}
