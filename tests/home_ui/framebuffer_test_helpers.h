#pragma once

#include "test_framework.h"

#include <lvgl.h>
#include <cstddef>
#include <cstdint>

namespace rodakos_home_ui_test {
inline uint32_t FramebufferPixel(int32_t x, int32_t y) {
    const auto* buffer = lv_display_get_buf_active(lv_display_get_default());
    RODAK_CHECK(buffer != nullptr);
    RODAK_CHECK_EQ(buffer->header.cf, LV_COLOR_FORMAT_XRGB8888);
    RODAK_CHECK(x >= 0 && x < 320 && y >= 0 && y < 240);
    const size_t offset = static_cast<size_t>(y) * buffer->header.stride +
                          static_cast<size_t>(x) * 4;
    return static_cast<uint32_t>(buffer->data[offset]) |
           (static_cast<uint32_t>(buffer->data[offset + 1]) << 8) |
           (static_cast<uint32_t>(buffer->data[offset + 2]) << 16);
}
}
