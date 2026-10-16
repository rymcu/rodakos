#include "test_framework.h"

#include <rodakos_icons.h>

#include <lvgl.h>

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string_view>

// The other Home UI tests use a Montserrat-only font fake. These checks link the generated
// production icon fonts so glyph coverage and placement are tested on the real data.
extern "C" const lv_font_t rodakos_icons_20_4;
extern "C" const lv_font_t rodakos_icons_30_4;

namespace {

// Each ICON_* macro must be exactly one four-byte UTF-8 letter in plane 15.
uint32_t DecodeUtf8(std::string_view text) {
    RODAK_CHECK_EQ(text.size(), 4U);
    const auto byte = [&](size_t index) { return static_cast<uint8_t>(text[index]); };
    RODAK_CHECK_EQ(byte(0) & 0xF8U, 0xF0U);
    uint32_t letter = byte(0) & 0x07U;
    for (size_t index = 1; index < 4; ++index) {
        RODAK_CHECK_EQ(byte(index) & 0xC0U, 0x80U);
        letter = (letter << 6) | (byte(index) & 0x3FU);
    }
    RODAK_CHECK(letter >= 0xF0000U && letter <= 0xFFFFDU);
    return letter;
}

// Iconify icons are drawn centered in their square viewBox. lv_font_conv rounds the box to
// whole pixels, so the side gaps may differ by one pixel, and an edge-to-edge icon (WiFi) may
// overhang its advance by one pixel on each side. A larger imbalance means the font lost the
// left side bearing and the icon sits against the left edge of its advance.
void CheckCenteredGlyph(const lv_font_t& font, std::string_view icon) {
    const uint32_t letter = DecodeUtf8(icon);
    lv_font_glyph_dsc_t glyph{};
    RODAK_CHECK(lv_font_get_glyph_dsc(&font, &glyph, letter, 0));
    RODAK_CHECK(glyph.resolved_font == &font);
    RODAK_CHECK_FALSE(glyph.is_placeholder);
    const int32_t left = glyph.ofs_x;
    const int32_t right = static_cast<int32_t>(glyph.adv_w) - glyph.box_w - glyph.ofs_x;
    RODAK_CHECK(left >= -1);
    RODAK_CHECK(right >= -1);
    RODAK_CHECK(std::abs(left - right) <= 1);
}

}  // namespace

RODAK_TEST("every UI icon is in the 20px font and centered in its advance") {
    for (const char* icon : {
             ICON_ARROW_DOWN, ICON_ARROW_LEFT, ICON_ARROW_RIGHT, ICON_ARROW_UP, ICON_ASSISTANT,
             ICON_BATTERY_CHARGING, ICON_BATTERY_EMPTY, ICON_BATTERY_FULL, ICON_BATTERY_HALF,
             ICON_BATTERY_UNKNOWN, ICON_BRIGHTNESS, ICON_CALENDAR, ICON_CAMERA, ICON_CHECK,
             ICON_CHEVRON_LEFT, ICON_CHEVRON_RIGHT, ICON_CHIP, ICON_CLOCK, ICON_CLOSE, ICON_CLOUD,
             ICON_COMPASS, ICON_EDIT, ICON_FOLDER, ICON_GLOBE, ICON_HOME, ICON_IMAGE, ICON_INFO,
             ICON_KEY, ICON_LIGHT, ICON_LINK, ICON_LOCK, ICON_MICROPHONE, ICON_MOON, ICON_MUSIC,
             ICON_PALETTE, ICON_PAUSE, ICON_PLAY, ICON_POWER, ICON_REFRESH, ICON_REPEAT, ICON_SETTINGS,
             ICON_SHUFFLE, ICON_SIGNAL, ICON_SKIP_BACK, ICON_SKIP_FORWARD, ICON_STOP,
             ICON_STORAGE, ICON_VOLUME, ICON_WIFI, ICON_WIFI_OFF}) {
        CheckCenteredGlyph(rodakos_icons_20_4, icon);
    }
}

RODAK_TEST("desktop icons are in the 30px font and centered in their advance") {
    for (const char* icon : {
             ICON_SETTINGS, ICON_INFO, ICON_MUSIC, ICON_CAMERA, ICON_MICROPHONE, ICON_CALENDAR,
             ICON_COMPASS, ICON_ASSISTANT, ICON_LIGHT, ICON_CLOCK, ICON_IMAGE, ICON_POWER,
             ICON_FOLDER, ICON_STORAGE, ICON_PALETTE}) {
        CheckCenteredGlyph(rodakos_icons_30_4, icon);
    }
}
