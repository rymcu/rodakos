#include "test_framework.h"

#include "phone_os/display_service.h"

#include <lvgl.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

lv_display_t* g_display = nullptr;

void FlushCallback(lv_display_t* display, const lv_area_t*, uint8_t*) {
    lv_display_flush_ready(display);
}

lv_display_t* CreatePartialDisplay(std::vector<uint8_t>& draw_storage, lv_draw_buf_t& draw_buf) {
    auto* display = lv_display_create(320, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    draw_storage.assign(320U * 40U * 2U, 0);
    lv_draw_buf_init(&draw_buf, 320, 40, LV_COLOR_FORMAT_RGB565, 0, draw_storage.data(),
                     static_cast<uint32_t>(draw_storage.size()));
    lv_display_set_draw_buffers(display, &draw_buf, nullptr);
    lv_display_set_render_mode(display, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, FlushCallback);
    return display;
}

void PumpUntilFrame(rodakos::DisplayService& service, uint32_t previous_sequence = 0) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        lv_timer_handler();
        rodakos::DisplayFrame frame;
        if (service.GetLatestFrame(frame) && frame.sequence > previous_sequence) return;
    }
    RODAK_CHECK(false);
}

uint16_t PixelAt(const rodakos::DisplayFrame& frame, int x, int y) {
    const size_t offset = static_cast<size_t>(y) * frame.stride + static_cast<size_t>(x) * 2;
    return static_cast<uint16_t>(frame.rgb565[offset]) |
           (static_cast<uint16_t>(frame.rgb565[offset + 1]) << 8);
}

rodakos::DisplayFrame ReadFrame(rodakos::DisplayService& service) {
    rodakos::DisplayFrame frame;
    RODAK_CHECK(service.GetLatestFrame(frame));
    RODAK_CHECK_EQ(frame.width, 320);
    RODAK_CHECK_EQ(frame.height, 240);
    RODAK_CHECK_EQ(frame.stride, 640);
    RODAK_CHECK_EQ(frame.rgb565.size(), static_cast<size_t>(320 * 240 * 2));
    return frame;
}

}  // namespace

RODAK_TEST("DisplayService keeps untouched pixels across partial flush cycles") {
    std::vector<uint8_t> draw_storage;
    lv_draw_buf_t draw_buf = {};
    auto* previous_default = lv_display_get_default();
    g_display = CreatePartialDisplay(draw_storage, draw_buf);
    lv_display_set_default(g_display);

    auto* screen = lv_display_get_screen_active(g_display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x102030), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    auto* header = lv_obj_create(screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 320, 40);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x204060), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);

    auto* body = lv_obj_create(screen);
    lv_obj_remove_style_all(body);
    lv_obj_set_pos(body, 0, 40);
    lv_obj_set_size(body, 320, 200);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x6090c0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_PART_MAIN);

    {
        rodakos::DisplayService service(g_display);
        RODAK_CHECK(service.StartCapture());
        // Execute the queued full refresh just as the board LVGL task would.
        lv_timer_handler();
        PumpUntilFrame(service);
        auto baseline = ReadFrame(service);
        const uint16_t body_pixel = PixelAt(baseline, 160, 120);
        const uint16_t header_pixel = PixelAt(baseline, 160, 20);
        RODAK_CHECK(body_pixel != 0);
        RODAK_CHECK(header_pixel != 0);
        RODAK_CHECK(body_pixel != header_pixel);

        const std::array<uint32_t, 4> header_colors = {
            0x902020,
            0x209020,
            0x202090,
            0x909020,
        };
        uint32_t previous_sequence = baseline.sequence;
        for (const uint32_t color : header_colors) {
            lv_obj_set_style_bg_color(header, lv_color_hex(color), LV_PART_MAIN);
            lv_refr_now(g_display);
            PumpUntilFrame(service, previous_sequence);
            const auto frame = ReadFrame(service);
            RODAK_CHECK_EQ(PixelAt(frame, 160, 120), body_pixel);
            RODAK_CHECK(PixelAt(frame, 160, 20) != header_pixel);
            previous_sequence = frame.sequence;
        }

        const auto header_before_body_update = ReadFrame(service);
        const uint16_t header_after_updates = PixelAt(header_before_body_update, 160, 20);
        lv_obj_set_style_bg_color(body, lv_color_hex(0xc06030), LV_PART_MAIN);
        lv_refr_now(g_display);
        PumpUntilFrame(service, previous_sequence);
        const auto body_changed = ReadFrame(service);
        RODAK_CHECK(PixelAt(body_changed, 160, 120) != body_pixel);
        RODAK_CHECK_EQ(PixelAt(body_changed, 160, 20), header_after_updates);
        service.StopCapture();
    }

    lv_display_set_default(previous_default);
    lv_display_delete(g_display);
    g_display = nullptr;
}
