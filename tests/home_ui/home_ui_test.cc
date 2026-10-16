#include "phone_os/resource_failure_injection.h"
#include "test_framework.h"
#include "framebuffer_test_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

// The module-level HomePageSession keeps the page that an earlier test left active.
void ShowFirstHomePage(HomeApp& home) {
    lv_obj_update_layout(home.tileview_);
    lv_tileview_set_tile(home.tileview_, home.page_tiles_[0], LV_ANIM_OFF);
    RODAK_CHECK_EQ(lv_obj_send_event(home.tileview_, LV_EVENT_SCROLL_END, nullptr), LV_RESULT_OK);
    Pump(10);
    RODAK_CHECK_EQ(home.ActivePageIndex(), 0U);
}

void PressAt(int32_t x, int32_t y) {
    lv_test_mouse_release();
    lv_test_wait(50);
    lv_test_mouse_move_to(x, y);
    lv_test_mouse_press();
    lv_test_wait(50);
}

struct IconAppSpec {
    const char* id;
    PhoneAppCategory category;
    PhoneAppIconTint tint;
};

// Built-in Home population with production categories and tints, in Registry order.
constexpr std::array<IconAppSpec, 14> kBuiltInIconApps = {{
    {"settings", PhoneAppCategory::kSystem, PhoneAppIconTint::kDefault},
    {"system", PhoneAppCategory::kSystem, PhoneAppIconTint::kDefault},
    {"music", PhoneAppCategory::kMedia, PhoneAppIconTint::kDefault},
    {"camera", PhoneAppCategory::kMedia, PhoneAppIconTint::kDefault},
    {"recorder", PhoneAppCategory::kMedia, PhoneAppIconTint::kDefault},
    {"calendar", PhoneAppCategory::kTools, PhoneAppIconTint::kDefault},
    {"gyro", PhoneAppCategory::kTools, PhoneAppIconTint::kDefault},
    {"assistant", PhoneAppCategory::kTools, PhoneAppIconTint::kCool},
    {"smart", PhoneAppCategory::kTools, PhoneAppIconTint::kCool},
    {"clock", PhoneAppCategory::kTools, PhoneAppIconTint::kWarm},
    {"photos", PhoneAppCategory::kMedia, PhoneAppIconTint::kWarm},
    {"wol", PhoneAppCategory::kTools, PhoneAppIconTint::kAlert},
    {"files", PhoneAppCategory::kTools, PhoneAppIconTint::kNeutral},
    {"themes", PhoneAppCategory::kSystem, PhoneAppIconTint::kWarm},
}};

struct IconFixture {
    explicit IconFixture(std::string_view layout_json = {})
        : ui(320, 240), context(ui, navigation, registry, services, settings) {
        if (!layout_json.empty()) {
            rodakos_home_ui_test::SetCommittedSetting("home", "layout", std::string(layout_json));
        }
        for (const auto& spec : kBuiltInIconApps) {
            PhoneAppDescriptor descriptor;
            descriptor.id = spec.id;
            descriptor.title = spec.id;
            descriptor.icon = "*";
            descriptor.category = spec.category;
            descriptor.icon_tint = spec.tint;
            registry.Register(std::move(descriptor));
        }
        RODAK_CHECK(home.OnCreate(context));
        ShowFirstHomePage(home);
    }

    ~IconFixture() {
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

size_t BuiltInIconIndex(std::string_view id) {
    for (size_t index = 0; index < kBuiltInIconApps.size(); ++index) {
        if (id == kBuiltInIconApps[index].id) {
            return index;
        }
    }
    RODAK_CHECK(false);
    return 0;
}

// Shows the page holding a default-layout built-in app and returns its desktop tile.
lv_obj_t* BuiltInIconTile(HomeApp& home, std::string_view id) {
    const size_t index = BuiltInIconIndex(id);
    const size_t page = index / rodakos::kHomeAppsPerPage;
    if (home.ActivePageIndex() != page) {
        lv_tileview_set_tile(home.tileview_, home.page_tiles_[page], LV_ANIM_OFF);
        RODAK_CHECK_EQ(lv_obj_send_event(home.tileview_, LV_EVENT_SCROLL_END, nullptr),
                       LV_RESULT_OK);
        Pump(10);
        RODAK_CHECK_EQ(home.ActivePageIndex(), page);
    }
    return HomeButton(home, static_cast<uint32_t>(index % rodakos::kHomeAppsPerPage));
}

lv_area_t DesktopIconArea(lv_obj_t* button) {
    lv_obj_update_layout(lv_screen_active());
    lv_area_t area;
    lv_obj_get_coords(button, &area);
    const int32_t left = area.x1 + (lv_obj_get_width(button) - 46) / 2;
    return {left, area.y1, left + 45, area.y1 + 45};
}

lv_color_t PixelColor(int32_t x, int32_t y) {
    return lv_color_hex(rodakos_home_ui_test::FramebufferPixel(x, y));
}

int32_t ChannelSum(lv_color_t color) {
    return color.red + color.green + color.blue;
}

bool ColorNear(lv_color_t actual, lv_color_t expected, int32_t tolerance) {
    return std::abs(actual.red - expected.red) <= tolerance &&
           std::abs(actual.green - expected.green) <= tolerance &&
           std::abs(actual.blue - expected.blue) <= tolerance;
}

int32_t PaintedRowWidth(int32_t y, int32_t x1, int32_t x2, lv_color_t background) {
    int32_t width = 0;
    for (int32_t x = x1; x <= x2; ++x) {
        if (!ColorNear(PixelColor(x, y), background, 2)) {
            ++width;
        }
    }
    return width;
}

lv_color_t DesktopIconCenter(lv_obj_t* button) {
    const lv_area_t icon = DesktopIconArea(button);
    // Sample left of center: the host glyph font draws into the middle of the tile.
    return PixelColor(icon.x1 + 6, (icon.y1 + icon.y2) / 2);
}

lv_obj_t* FirstChildOfClass(lv_obj_t* parent, const lv_obj_class_t* object_class) {
    const uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t index = 0; index < count; ++index) {
        lv_obj_t* child = lv_obj_get_child(parent, index);
        if (lv_obj_check_type(child, object_class)) {
            return child;
        }
    }
    return nullptr;
}

// Evaluates the color the RGB565 panel shows: LVGL truncates red/blue to 5 bits and green to 6.
double RelativeLuminance(uint32_t rgb) {
    const auto channel = [](uint32_t value, uint32_t mask) {
        const double normalized = static_cast<double>(value & mask) / 255.0;
        return normalized <= 0.04045 ? normalized / 12.92
                                     : std::pow((normalized + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel((rgb >> 16) & 0xFFU, 0xF8U) +
           0.7152 * channel((rgb >> 8) & 0xFFU, 0xFCU) +
           0.0722 * channel(rgb & 0xFFU, 0xF8U);
}

double ContrastRatio(uint32_t first, uint32_t second) {
    const double a = RelativeLuminance(first);
    const double b = RelativeLuminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

uint32_t ColorHex(lv_color_t color) {
    return (static_cast<uint32_t>(color.red) << 16) |
           (static_cast<uint32_t>(color.green) << 8) | color.blue;
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
                   ICON_BATTERY_FULL);
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

RODAK_TEST("desktop icon is a rounded vertical gradient in its slot color") {
    ResetScreen();
    ResetSettings();
    const auto previous_theme = *rodakos_theme_get();
    rodakos_theme_init(RODAKOS_THEME_DARK);
    {
        IconFixture fixture;
        lv_obj_t* button = BuiltInIconTile(fixture.home, "calendar");
        const lv_area_t icon = DesktopIconArea(button);
        const lv_color_t top = rodakos_theme_icon_color(RODAKOS_ICON_SLOT_TOOLS);
        const lv_color_t bottom = lv_color_darken(top, 31);
        const lv_color_t background = rodakos_theme_bg_primary();
        const int32_t sample_x = icon.x1 + 6;

        RODAK_CHECK(ColorNear(PixelColor(icon.x1, icon.y1), background, 2));
        RODAK_CHECK(ColorNear(PixelColor(icon.x2, icon.y2), background, 2));
        // Radius 14, not a circle: the row 4px from the top is much wider than a circle's.
        RODAK_CHECK(PaintedRowWidth(icon.y1 + 4, icon.x1, icon.x2, background) >= 36);

        const lv_color_t near_top = PixelColor(sample_x, icon.y1 + 4);
        const lv_color_t near_bottom = PixelColor(sample_x, icon.y2 - 4);
        RODAK_CHECK(ChannelSum(near_top) > ChannelSum(near_bottom));
        RODAK_CHECK(ColorNear(near_top, top, 6));
        RODAK_CHECK(ColorNear(PixelColor(sample_x, (icon.y1 + icon.y2) / 2),
                              lv_color_mix(bottom, top, 128), 6));
        // The corner curve covers x1 + 6 on the last row, so read the bottom stop at the
        // horizontal center, which the host glyph does not reach.
        RODAK_CHECK(ColorNear(PixelColor((icon.x1 + icon.x2) / 2, icon.y2), bottom, 3));
    }
    rodakos_theme_set_custom(&previous_theme);
}

RODAK_TEST("icon colors follow category and tint instead of the app id") {
    ResetScreen();
    ResetSettings();
    IconFixture fixture;
    const auto center = [&](const char* id) {
        return DesktopIconCenter(BuiltInIconTile(fixture.home, id));
    };

    RODAK_CHECK(lv_color_eq(center("settings"), center("system")));
    RODAK_CHECK(lv_color_eq(center("music"), center("camera")));
    RODAK_CHECK(lv_color_eq(center("music"), center("recorder")));
    RODAK_CHECK(lv_color_eq(center("calendar"), center("gyro")));
    RODAK_CHECK(lv_color_eq(center("assistant"), center("smart")));
    RODAK_CHECK(lv_color_eq(center("clock"), center("photos")));
    RODAK_CHECK(lv_color_eq(center("clock"), center("themes")));
    RODAK_CHECK_FALSE(lv_color_eq(center("themes"), center("settings")));
    RODAK_CHECK_FALSE(lv_color_eq(center("assistant"), center("calendar")));
    RODAK_CHECK_FALSE(lv_color_eq(center("photos"), center("music")));
    RODAK_CHECK_FALSE(lv_color_eq(center("wol"), center("calendar")));
    RODAK_CHECK_FALSE(lv_color_eq(center("files"), center("calendar")));
    RODAK_CHECK_FALSE(lv_color_eq(center("settings"), center("files")));

    const auto top_of = [&](const char* id) {
        const lv_area_t icon = DesktopIconArea(BuiltInIconTile(fixture.home, id));
        return PixelColor((icon.x1 + icon.x2) / 2, icon.y1);
    };
    const std::array<std::pair<const char*, int>, 9> expected_slots = {{
        {"settings", RODAKOS_ICON_SLOT_SYSTEM},
        {"music", RODAKOS_ICON_SLOT_MEDIA},
        {"calendar", RODAKOS_ICON_SLOT_TOOLS},
        {"assistant", RODAKOS_ICON_SLOT_COOL},
        {"clock", RODAKOS_ICON_SLOT_WARM},
        {"photos", RODAKOS_ICON_SLOT_WARM},
        {"wol", RODAKOS_ICON_SLOT_ALERT},
        {"files", RODAKOS_ICON_SLOT_NEUTRAL},
        {"themes", RODAKOS_ICON_SLOT_WARM},
    }};
    for (const auto& [id, slot] : expected_slots) {
        RODAK_CHECK(ColorNear(top_of(id), rodakos_theme_icon_color(slot), 4));
    }
}

RODAK_TEST("apps with the same category and no tint share one icon color") {
    ResetScreen();
    ResetSettings();
    // HomeFixture registers default Tools descriptors with distinct ids; the old FNV hash
    // spread app000..app003 across palette entries.
    HomeFixture fixture(4);
    ShowFirstHomePage(fixture.home);
    const lv_color_t first = DesktopIconCenter(HomeButton(fixture.home, 0));
    for (uint32_t index = 1; index < 4; ++index) {
        RODAK_CHECK(lv_color_eq(DesktopIconCenter(HomeButton(fixture.home, index)), first));
    }
}

RODAK_TEST("pressing a desktop icon insets and darkens it until release") {
    ResetScreen();
    ResetSettings();
    const auto previous_theme = *rodakos_theme_get();
    rodakos_theme_init(RODAKOS_THEME_DARK);
    {
        IconFixture fixture;
        lv_obj_t* button = BuiltInIconTile(fixture.home, "music");
        const lv_area_t icon = DesktopIconArea(button);
        const lv_color_t background = rodakos_theme_bg_primary();
        const int32_t middle_y = (icon.y1 + icon.y2) / 2;
        const lv_color_t idle_center = DesktopIconCenter(button);
        RODAK_CHECK_EQ(PaintedRowWidth(middle_y, icon.x1 - 2, icon.x2 + 2, background), 46);

        PressAt((icon.x1 + icon.x2) / 2, middle_y);
        RODAK_CHECK(lv_obj_has_state(button, LV_STATE_PRESSED));
        RODAK_CHECK_EQ(PaintedRowWidth(middle_y, icon.x1 - 2, icon.x2 + 2, background), 44);
        const lv_color_t pressed_center = DesktopIconCenter(button);
        RODAK_CHECK(ChannelSum(pressed_center) < ChannelSum(idle_center));

        lv_test_mouse_release();
        Pump(50);
        RODAK_CHECK_FALSE(lv_obj_has_state(button, LV_STATE_PRESSED));
        RODAK_CHECK_EQ(PaintedRowWidth(middle_y, icon.x1 - 2, icon.x2 + 2, background), 46);
        RODAK_CHECK(lv_color_eq(DesktopIconCenter(button), idle_center));
        RODAK_CHECK_EQ(fixture.navigation.launches.size(), 1U);
        RODAK_CHECK_EQ(fixture.navigation.launches.front(), std::string("music"));
    }
    rodakos_theme_set_custom(&previous_theme);
}

RODAK_TEST("a press that becomes a page swipe ends with the idle icon") {
    ResetScreen();
    ResetSettings();
    const auto previous_theme = *rodakos_theme_get();
    rodakos_theme_init(RODAKOS_THEME_DARK);
    {
        HomeFixture fixture(24);
        ShowFirstHomePage(fixture.home);
        lv_obj_t* button = HomeButton(fixture.home, 2);
        const lv_area_t icon = DesktopIconArea(button);
        const lv_color_t background = rodakos_theme_bg_primary();
        const int32_t middle_y = (icon.y1 + icon.y2) / 2;
        const lv_color_t idle_center = DesktopIconCenter(button);

        Swipe(button, -160);
        Pump(1200);
        RODAK_CHECK_EQ(fixture.home.ActivePageIndex(), 1U);
        Swipe(HomeButton(fixture.home, 2), 160);
        Pump(1200);
        RODAK_CHECK_EQ(fixture.home.ActivePageIndex(), 0U);
        RODAK_CHECK(fixture.navigation.launches.empty());

        button = HomeButton(fixture.home, 2);
        RODAK_CHECK_FALSE(lv_obj_has_state(button, LV_STATE_PRESSED));
        RODAK_CHECK_EQ(PaintedRowWidth(middle_y, icon.x1 - 2, icon.x2 + 2, background), 46);
        RODAK_CHECK(lv_color_eq(DesktopIconCenter(button), idle_center));
    }
    rodakos_theme_set_custom(&previous_theme);
}

RODAK_TEST("an input reset during a press restores the idle icon") {
    ResetScreen();
    ResetSettings();
    const auto previous_theme = *rodakos_theme_get();
    rodakos_theme_init(RODAKOS_THEME_DARK);
    {
        HomeFixture fixture(4);
        ShowFirstHomePage(fixture.home);
        lv_obj_t* button = HomeButton(fixture.home, 2);
        const lv_area_t icon = DesktopIconArea(button);
        const lv_color_t background = rodakos_theme_bg_primary();
        const int32_t middle_y = (icon.y1 + icon.y2) / 2;

        PressAt((icon.x1 + icon.x2) / 2, middle_y);
        RODAK_CHECK(lv_obj_has_state(button, LV_STATE_PRESSED));
        RODAK_CHECK_EQ(PaintedRowWidth(middle_y, icon.x1 - 2, icon.x2 + 2, background), 44);

        lv_indev_reset(nullptr, nullptr);
        lv_refr_now(nullptr);
        RODAK_CHECK_FALSE(lv_obj_has_state(button, LV_STATE_PRESSED));
        RODAK_CHECK_EQ(PaintedRowWidth(middle_y, icon.x1 - 2, icon.x2 + 2, background), 46);
        lv_test_mouse_release();
        Pump(50);
        RODAK_CHECK(fixture.navigation.launches.empty());
    }
    rodakos_theme_set_custom(&previous_theme);
}

RODAK_TEST("folder previews, collection rows and Arrange header use slot colors") {
    ResetScreen();
    ResetSettings();
    IconFixture fixture(
        R"({"v":1,"rev":0,"items":[{"type":"folder","id":"f_0000000a","name":"Mix",)"
        R"("apps":["settings","music","assistant","files"]},{"type":"app","id":"calendar"}]})");
    RODAK_CHECK_EQ(fixture.home.layout_.items.front().type, rodakos::HomeLayoutItemType::kFolder);

    lv_obj_t* folder_tile = HomeButton(fixture.home, 0);
    lv_obj_t* folder_icon = FirstChildOfClass(folder_tile, &lv_obj_class);
    RODAK_CHECK(folder_icon != nullptr);
    RODAK_CHECK_EQ(lv_obj_get_style_radius(folder_icon, 0), 14);
    RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_color(folder_icon, 0), rodakos_theme_bg_tertiary()));
    RODAK_CHECK_EQ(lv_obj_get_child_count(folder_icon), 4U);
    const std::array<int, 4> preview_slots = {
        RODAKOS_ICON_SLOT_SYSTEM, RODAKOS_ICON_SLOT_MEDIA,
        RODAKOS_ICON_SLOT_COOL, RODAKOS_ICON_SLOT_NEUTRAL};
    for (uint32_t index = 0; index < 4; ++index) {
        lv_obj_t* preview = lv_obj_get_child(folder_icon, index);
        RODAK_CHECK_EQ(lv_obj_get_style_radius(preview, 0), 5);
        RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_color(preview, 0),
                                rodakos_theme_icon_color(preview_slots[index])));
        RODAK_CHECK_EQ(lv_obj_get_style_bg_grad_dir(preview, 0), LV_GRAD_DIR_NONE);
    }

    Click(folder_tile);
    RODAK_CHECK_EQ(fixture.home.collection_state_.kind, HomeApp::CollectionKind::kFolder);
    lv_obj_t* music_row = lv_obj_get_parent(FindLabel(fixture.home.collection_view_, "music"));
    RODAK_CHECK(music_row != nullptr);
    lv_obj_t* row_icon = lv_obj_get_child(music_row, 0);
    RODAK_CHECK_EQ(lv_obj_get_width(row_icon), 28);
    RODAK_CHECK_EQ(lv_obj_get_style_radius(row_icon, 0), 8);
    const lv_color_t media = rodakos_theme_icon_color(RODAKOS_ICON_SLOT_MEDIA);
    RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_color(row_icon, 0), media));
    RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_grad_color(row_icon, 0), lv_color_darken(media, 31)));
    RODAK_CHECK_EQ(lv_obj_get_style_bg_grad_dir(row_icon, 0), LV_GRAD_DIR_VER);

    LongPress(music_row);
    RODAK_CHECK(fixture.home.HasEditingTarget());
    lv_obj_t* header_icon = lv_obj_get_child(fixture.home.layout_editor_, 1);
    RODAK_CHECK_EQ(lv_obj_get_width(header_icon), 40);
    RODAK_CHECK_EQ(lv_obj_get_style_radius(header_icon, 0), 12);
    RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_color(header_icon, 0), media));
    RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_grad_color(header_icon, 0), lv_color_darken(media, 31)));
    RODAK_CHECK_EQ(lv_obj_get_style_bg_grad_dir(header_icon, 0), LV_GRAD_DIR_VER);
}

RODAK_TEST("folder previews of unregistered apps fall back to the neutral slot") {
    ResetScreen();
    ResetSettings();
    IconFixture fixture(
        R"({"v":1,"rev":0,"items":[{"type":"folder","id":"f_0000000b","name":"Mix",)"
        R"("apps":["settings","music"]}]})");
    // Reconciliation drops unknown IDs, so exercise the lookup fallback through Arrange's
    // draft, which can still reference an app that is no longer registered.
    LongPress(HomeButton(fixture.home, 0));
    RODAK_CHECK(fixture.home.HasEditingTarget());
    fixture.home.draft_layout_.items.front().apps.push_back("removed_app");
    fixture.home.CreateLayoutEditorUi();
    Pump();
    lv_obj_t* header_icon = lv_obj_get_child(fixture.home.layout_editor_, 1);
    RODAK_CHECK_EQ(lv_obj_get_child_count(header_icon), 3U);
    RODAK_CHECK_EQ(lv_obj_get_style_radius(lv_obj_get_child(header_icon, 0), 0), 3);
    RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_child(header_icon, 2), 0),
                            rodakos_theme_icon_color(RODAKOS_ICON_SLOT_NEUTRAL)));
}

RODAK_TEST("All Apps overflow tile uses the primary gradient and button foreground") {
    ResetScreen();
    ResetSettings();
    const auto previous_theme = *rodakos_theme_get();
    rodakos_theme_apply_preset_primary("dark", 0xF4F4A0);
    {
        HomeFixture fixture(97);
        fixture.ui.SyncThemeName("dark");
        RODAK_CHECK(fixture.home.OnThemeChanged(fixture.context));
        lv_tileview_set_tile(fixture.home.tileview_, fixture.home.page_tiles_[7], LV_ANIM_OFF);
        RODAK_CHECK_EQ(
            lv_obj_send_event(fixture.home.tileview_, LV_EVENT_SCROLL_END, nullptr),
            LV_RESULT_OK);
        Pump(10);

        lv_obj_t* label = FindLabel(fixture.home.page_tiles_[7], "All Apps");
        RODAK_CHECK(label != nullptr);
        lv_obj_t* overflow_icon = FirstChildOfClass(lv_obj_get_parent(label), &lv_obj_class);
        RODAK_CHECK(overflow_icon != nullptr);
        const lv_color_t primary = lv_color_hex(0xF4F4A0);
        const lv_color_t foreground = fixture.ui.theme().accent_text;
        RODAK_CHECK(lv_color_eq(foreground, lv_color_black()));
        RODAK_CHECK_EQ(lv_obj_get_style_radius(overflow_icon, 0), 14);
        RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_color(overflow_icon, 0), primary));
        RODAK_CHECK(lv_color_eq(lv_obj_get_style_bg_grad_color(overflow_icon, 0),
                                lv_color_darken(primary, 31)));
        RODAK_CHECK_EQ(lv_obj_get_style_bg_grad_dir(overflow_icon, 0), LV_GRAD_DIR_VER);
        RODAK_CHECK_EQ(lv_obj_get_child_count(overflow_icon), 2U);
        for (uint32_t index = 0; index < 2; ++index) {
            RODAK_CHECK(lv_color_eq(
                lv_obj_get_style_text_color(lv_obj_get_child(overflow_icon, index), 0), foreground));
        }
    }
    rodakos_theme_set_custom(&previous_theme);
}

RODAK_TEST("every preset palette keeps readable, distinct icon slots") {
    const auto previous_theme = *rodakos_theme_get();
    for (const char* preset : {"dark", "light", "blue", "green"}) {
        rodakos_theme_init_from_name(preset);
        const auto* theme = rodakos_theme_get();
        const bool light = rodakos_theme_is_light_name(preset);
        for (int slot = 0; slot < RODAKOS_ICON_SLOT_COUNT; ++slot) {
            const uint32_t top = theme->icon_palette[slot];
            const uint32_t bottom = ColorHex(lv_color_darken(lv_color_hex(top), 31));
            RODAK_CHECK(ContrastRatio(0xFFFFFF, top) >= 3.0);
            RODAK_CHECK(ContrastRatio(bottom, theme->bg_primary) >= (light ? 3.0 : 2.0));
            for (int other = slot + 1; other < RODAKOS_ICON_SLOT_COUNT; ++other) {
                const lv_color_t a = lv_color_hex(top);
                const lv_color_t b = lv_color_hex(theme->icon_palette[other]);
                const int32_t dr = a.red - b.red;
                const int32_t dg = a.green - b.green;
                const int32_t db = a.blue - b.blue;
                RODAK_CHECK(dr * dr + dg * dg + db * db >= 40 * 40);
            }
        }
        RODAK_CHECK(lv_color_eq(rodakos_theme_icon_color(-1),
                                rodakos_theme_icon_color(RODAKOS_ICON_SLOT_SYSTEM)));
        RODAK_CHECK(lv_color_eq(rodakos_theme_icon_color(RODAKOS_ICON_SLOT_COUNT),
                                rodakos_theme_icon_color(RODAKOS_ICON_SLOT_SYSTEM)));

        rodakos_theme_apply_preset_primary(preset, 0x123456);
        for (int slot = 0; slot < RODAKOS_ICON_SLOT_COUNT; ++slot) {
            RODAK_CHECK_EQ(rodakos_theme_get()->icon_palette[slot], theme->icon_palette[slot]);
        }
    }
    rodakos_theme_set_custom(&previous_theme);
}
