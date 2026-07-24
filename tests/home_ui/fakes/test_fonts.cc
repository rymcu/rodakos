#include "phone_ui/phone_fonts.h"

lv_font_t phone_font_12;
lv_font_t phone_font_14;
lv_font_t phone_font_18;

namespace {
bool initialized = false;
}

void PhoneFontsInit() {
    if (initialized) {
        return;
    }
    phone_font_12 = lv_font_montserrat_12;
    phone_font_14 = lv_font_montserrat_14;
    phone_font_18 = lv_font_montserrat_18;
    initialized = true;
}

const lv_font_t* PhoneIconFont() {
    return &phone_font_14;
}

const lv_font_t* PhoneIconFontLarge() {
    return &phone_font_18;
}
