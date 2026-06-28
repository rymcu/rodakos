#pragma once

#include <lvgl.h>

struct PhoneTheme {
    lv_color_t background;
    lv_color_t surface;
    lv_color_t surface_alt;
    lv_color_t border;
    lv_color_t text_primary;
    lv_color_t text_secondary;
    lv_color_t accent;
    lv_color_t accent_text;
};

PhoneTheme PhoneDarkTheme();
PhoneTheme PhoneLightTheme();
