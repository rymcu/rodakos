#ifndef LV_CONF_H
#define LV_CONF_H

/* Matches the device options that affect icon pixels (see sdkconfig): RGB565 output, complex
 * software drawing for the radius mask, two gradient stops, the 128 mix rounding offset, and the
 * large text-font format that the CJK subset needs. */
#define LV_COLOR_DEPTH 16
#define LV_DRAW_SW_COMPLEX 1
#define LV_GRADIENT_MAX_STOPS 2
#define LV_COLOR_MIX_ROUND_OFS 128
#define LV_FONT_FMT_TXT_LARGE 1
#define LV_USE_FONT_PLACEHOLDER 1

#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_OS LV_OS_NONE

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_USE_TEST 1
#define LV_USE_TEST_SCREENSHOT_COMPARE 0

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1

#define LV_USE_KEYBOARD 1
#define LV_USE_TILEVIEW 1

#endif
