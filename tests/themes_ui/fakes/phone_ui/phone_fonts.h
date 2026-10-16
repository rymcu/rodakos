#pragma once

// Production declarations. tests/home_ui/fakes declares phone_font_logo const for its
// Montserrat-only fake; this suite links the real phone_fonts.cc instead.

// RodakOS 字体系统
// RodakOS-owned UI font subset，解决中文和图标的“豆腐块”问题：
//   - font_puhui_16_4  : 普惠体（中文 / CJK 字形）
//   - rodakos_icons_*  : Ionicons 线条图标（由 components/rodakos_fonts/icons.json 生成）
//
// 通过 LVGL 9 的字体回退链（fallback）把三套字体串起来：
//   Montserrat（拉丁字母，保持原有英文排版） -> 图标 -> 普惠体（中文）
// 这样既不改变现有英文界面的尺寸，又能正确显示图标和中文。

#include <lvgl.h>

// 图标宏（ICON_HOME / ICON_SETTINGS / ICON_WIFI 等）
#include <rodakos_icons.h>

#ifdef __cplusplus
extern "C" {
#endif

// 组合字体（Montserrat -> Font Awesome -> 普惠体），由 PhoneFontsInit() 初始化。
// 直接以 &phone_font_XX 替换原来的 &lv_font_montserrat_XX 即可获得图标 + 中文回退。
extern lv_font_t phone_font_12;
extern lv_font_t phone_font_14;
extern lv_font_t phone_font_18;
extern lv_font_t phone_font_logo;

// 纯图标字体访问器（用于只显示图标的控件）。
const lv_font_t* PhoneIconFont(void);       // 图标 20px，全部 ICON_*（状态栏 / 列表图标）
const lv_font_t* PhoneIconFontLarge(void);  // 图标 30px，仅桌面应用图标子集

// 构建组合字体。必须在 lvgl_port_init() 之后、创建任何界面之前调用一次。
void PhoneFontsInit(void);

#ifdef __cplusplus
}
#endif
