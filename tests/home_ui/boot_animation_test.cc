#include "test_framework.h"
#include "framebuffer_test_helpers.h"
#include "lvgl_creation_failures.h"

#include <atomic>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#define private public
#include "phone_ui/boot_animation.h"
#undef private

#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include <src/indev/lv_indev_private.h>
#include <src/others/test/lv_test.h>

namespace {

using rodakos_home_ui_test::FramebufferPixel;
using rodakos_home_ui_test::LvglCreationFailure;
using rodakos_home_ui_test::ArmLvglCreationFailure;

std::shared_ptr<uint8_t> Buffer(std::initializer_list<uint8_t> values) {
    std::shared_ptr<uint8_t> result(new uint8_t[values.size()], std::default_delete<uint8_t[]>());
    std::copy(values.begin(), values.end(), result.get());
    return result;
}

std::shared_ptr<rodakos::AppearanceBootAssets> Assets(
        rodakos::AppearancePixelFormat format = rodakos::AppearancePixelFormat::kA4) {
    auto assets = std::make_shared<rodakos::AppearanceBootAssets>();
    assets->metadata.animation_kind = format == rodakos::AppearancePixelFormat::kA4 ? "text" : "image";
    assets->metadata.animation_template = "letters";
    assets->metadata.duration_ms = 2500;
    assets->metadata.background = 0x0000ff;
    assets->metadata.color = 0x00ff00;
    assets->metadata.theme_preset = "dark";
    assets->metadata.theme_primary = 0x79cbff;
    assets->metadata.resources.push_back({
        .id = "logo", .format = format, .width = 2, .height = 1,
        .offset = 0, .length = format == rodakos::AppearancePixelFormat::kA4 ? 1U
            : format == rodakos::AppearancePixelFormat::kRgb565 ? 4U : 6U,
    });
    assets->metadata.units.push_back({
        .resource_id = "logo", .x = 20, .y = 30, .start_ms = 100, .duration_ms = 400,
    });
    if (format == rodakos::AppearancePixelFormat::kA4) assets->buffers.push_back(Buffer({255, 0}));
    else if (format == rodakos::AppearancePixelFormat::kRgb565) assets->buffers.push_back(Buffer({0, 0xf8, 0xe0, 7}));
    else assets->buffers.push_back(Buffer({0, 0xf8, 0xe0, 7, 0, 255}));
    return assets;
}

struct BootFixture {
    BootFixture() : theme(*rodakos_theme_get()), ui(320, 240), animation(ui) {
        ArmLvglCreationFailure(LvglCreationFailure::kNone);
        input = lv_test_indev_get_indev(LV_INDEV_TYPE_POINTER);
        lv_indev_enable(input, true);
        lv_test_mouse_release();
        lv_test_wait(2);
        lv_indev_reset(input, nullptr);
        lv_obj_clean(lv_screen_active());
        lv_obj_clean(lv_layer_top());
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x102030), 0);
        lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
        ui.SetPrimaryInput(input);
    }
    ~BootFixture() {
        ArmLvglCreationFailure(LvglCreationFailure::kNone);
        animation.Stop();
        lv_obj_clean(lv_screen_active());
        lv_obj_clean(lv_layer_top());
        lv_indev_enable(input, true);
        rodakos_theme_set_custom(&theme);
        lv_test_wait(2);
    }
    rodakos_theme_t theme;
    PhoneUi ui;
    BootAnimation animation;
    lv_indev_t* input = nullptr;
};

void CheckCleaned(BootFixture& fixture) {
    RODAK_CHECK(fixture.animation.root_ == nullptr);
    RODAK_CHECK(fixture.animation.timer_ == nullptr);
    RODAK_CHECK(fixture.animation.assets_ == nullptr);
    RODAK_CHECK(fixture.animation.custom_units_.empty());
    RODAK_CHECK(fixture.animation.custom_images_.empty());
    RODAK_CHECK(fixture.input->enabled);
    RODAK_CHECK_EQ(lv_obj_get_child_count(lv_layer_top()), 0U);
}

}

RODAK_TEST("BootAnimation draws expanded A8 glyphs using compiled color and real elapsed ticks") {
    BootFixture fixture;
    auto assets = Assets();
    RODAK_CHECK(fixture.animation.Start(assets));
    RODAK_CHECK_FALSE(fixture.input->enabled);
    RODAK_CHECK_EQ(fixture.animation.custom_units_.size(), 1U);
    auto* image = fixture.animation.custom_units_.front();
    const auto* descriptor = static_cast<const lv_image_dsc_t*>(lv_image_get_src(image));
    RODAK_CHECK_EQ(descriptor->header.cf, LV_COLOR_FORMAT_A8);
    RODAK_CHECK_EQ(descriptor->header.stride, 2U);
    RODAK_CHECK_EQ(descriptor->data_size, 2U);
    RODAK_CHECK_EQ(descriptor->data, assets->buffers.front().get());
    lv_obj_update_layout(fixture.animation.root_);
    RODAK_CHECK_EQ(lv_obj_get_y(image), 40);
    lv_test_fast_forward(300);
    RODAK_CHECK_EQ(fixture.animation.elapsed_ms_, 300U);
    RODAK_CHECK_EQ(lv_obj_get_style_image_opa(image, 0), 128);
    RODAK_CHECK_EQ(lv_obj_get_y(image), 35);
    lv_test_fast_forward(200);
    RODAK_CHECK_EQ(lv_obj_get_y(image), 30);
    RODAK_CHECK_EQ(FramebufferPixel(20, 30), 0x00ff00U);
    RODAK_CHECK_EQ(FramebufferPixel(21, 30), 0x0000ffU);
}

RODAK_TEST("BootAnimation draws little-endian RGB565 and separate RGB565A8 alpha") {
    for (auto format : {rodakos::AppearancePixelFormat::kRgb565,
                        rodakos::AppearancePixelFormat::kRgb565A8}) {
        BootFixture fixture;
        auto assets = Assets(format);
        assets->metadata.animation_template = "fade";
        RODAK_CHECK(fixture.animation.Start(assets));
        lv_test_fast_forward(500);
        auto* image = fixture.animation.custom_units_.front();
        const auto* descriptor = static_cast<const lv_image_dsc_t*>(lv_image_get_src(image));
        RODAK_CHECK_EQ(descriptor->header.stride, 4U);
        RODAK_CHECK_EQ(descriptor->header.cf, format == rodakos::AppearancePixelFormat::kRgb565
            ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_RGB565A8);
        RODAK_CHECK_EQ(lv_obj_get_y(image), 30);
        RODAK_CHECK_EQ(FramebufferPixel(20, 30), format == rodakos::AppearancePixelFormat::kRgb565
            ? 0xff0000U : 0x0000ffU);
        RODAK_CHECK_EQ(FramebufferPixel(21, 30), 0x00ff00U);
    }
}

RODAK_TEST("BootAnimation Finish waits for configured duration then fades and restores touch") {
    BootFixture fixture;
    auto assets = Assets();
    assets->metadata.duration_ms = 1800;
    RODAK_CHECK(fixture.animation.Start(assets));
    fixture.animation.Finish();
    lv_test_fast_forward(1500);
    RODAK_CHECK_FALSE(fixture.animation.finishing_);
    RODAK_CHECK_FALSE(fixture.animation.HasCompleted());
    RODAK_CHECK_FALSE(fixture.input->enabled);
    lv_test_fast_forward(100);
    RODAK_CHECK(fixture.animation.finishing_);
    lv_test_fast_forward(110);
    RODAK_CHECK_EQ(lv_obj_get_style_opa(fixture.animation.root_, 0), 127);
    lv_test_fast_forward(110);
    RODAK_CHECK(fixture.animation.HasCompleted());
    RODAK_CHECK_EQ(fixture.animation.duration_ms(), 1820U);
    CheckCleaned(fixture);
    auto* button = lv_button_create(lv_screen_active());
    lv_obj_set_size(button, 80, 40);
    lv_obj_set_pos(button, 20, 20);
    int clicks = 0;
    lv_obj_add_event_cb(button, [](lv_event_t* event) {
        ++*static_cast<int*>(lv_event_get_user_data(event));
    }, LV_EVENT_CLICKED, &clicks);
    lv_test_wait(5);
    lv_test_mouse_click_at(40, 40);
    RODAK_CHECK_EQ(clicks, 1);
}

RODAK_TEST("BootAnimation remains visible until system Finish even after nominal duration") {
    BootFixture fixture;
    RODAK_CHECK(fixture.animation.Start(Assets()));
    lv_test_fast_forward(6000);
    RODAK_CHECK_FALSE(fixture.animation.HasCompleted());
    RODAK_CHECK_FALSE(fixture.animation.finishing_);
    RODAK_CHECK_EQ(lv_obj_get_style_opa(fixture.animation.root_, 0), LV_OPA_COVER);
    fixture.animation.Finish();
    lv_test_fast_forward(220);
    RODAK_CHECK(fixture.animation.HasCompleted());
    RODAK_CHECK_EQ(fixture.animation.duration_ms(), 6220U);
    CheckCleaned(fixture);
}

RODAK_TEST("BootAnimation Stop frees custom assets and its timer and permits a clean restart") {
    BootFixture fixture;
    auto assets = Assets();
    std::weak_ptr<rodakos::AppearanceBootAssets> lifetime = assets;
    RODAK_CHECK(fixture.animation.Start(std::move(assets)));
    fixture.animation.Stop();
    CheckCleaned(fixture);
    RODAK_CHECK(lifetime.expired());
    RODAK_CHECK_FALSE(fixture.animation.HasCompleted());
    lv_test_fast_forward(6000);
    RODAK_CHECK_FALSE(fixture.animation.HasCompleted());
    RODAK_CHECK(fixture.animation.Start());
    RODAK_CHECK_EQ(lv_obj_get_child_count(fixture.animation.root_), 7U);
    RODAK_CHECK_FALSE(fixture.input->enabled);
}

RODAK_TEST("BootAnimation missing resource fails cleanly and caller can fall back to builtin") {
    BootFixture fixture;
    auto assets = Assets();
    assets->buffers.clear();
    RODAK_CHECK_FALSE(fixture.animation.Start(assets));
    CheckCleaned(fixture);
    RODAK_CHECK(fixture.animation.Start());
    RODAK_CHECK_EQ(lv_obj_get_child_count(fixture.animation.root_), 7U);
    lv_test_fast_forward(800);
    for (size_t index = 0; index < 7; ++index) {
        RODAK_CHECK_EQ(lv_obj_get_style_opa(fixture.animation.logo_letters_[index], 0), LV_OPA_COVER);
    }
}

#ifdef RODAKOS_TEST_LVGL_WRAPPERS
RODAK_TEST("BootAnimation creation boundary failures release resources before builtin fallback") {
    for (auto failure : {LvglCreationFailure::kObject, LvglCreationFailure::kImage,
                         LvglCreationFailure::kTimer}) {
        BootFixture fixture;
        auto assets = Assets();
        std::weak_ptr<rodakos::AppearanceBootAssets> lifetime = assets;
        ArmLvglCreationFailure(failure);
        RODAK_CHECK_FALSE(fixture.animation.Start(std::move(assets)));
        CheckCleaned(fixture);
        RODAK_CHECK(lifetime.expired());
        RODAK_CHECK(fixture.animation.Start());
        RODAK_CHECK_EQ(lv_obj_get_child_count(fixture.animation.root_), 7U);
    }
}
#endif
