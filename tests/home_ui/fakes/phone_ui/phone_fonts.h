#pragma once

#include <font_awesome.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern lv_font_t phone_font_12;
extern lv_font_t phone_font_14;
extern lv_font_t phone_font_18;

const lv_font_t* PhoneIconFont(void);
const lv_font_t* PhoneIconFontLarge(void);
void PhoneFontsInit(void);

#ifdef __cplusplus
}
#endif
