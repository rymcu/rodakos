#pragma once

// RodakOS 字体系统
// 参考 xiaozhi-esp32 的字体方案，使用 78/xiaozhi-fonts 组件解决“豆腐块”问题：
//   - font_puhui_16_4 : 阿里巴巴普惠体（中文 / CJK 字形）
//   - font_awesome_*  : Font Awesome 图标字体（替换 emoji / 方块字符图标）
//
// 通过 LVGL 9 的字体回退链（fallback）把三套字体串起来：
//   Montserrat（拉丁字母，保持原有英文排版） -> Font Awesome（图标） -> 普惠体（中文）
// 这样既不改变现有英文界面的尺寸，又能正确显示图标和中文。

#include <lvgl.h>

// Font Awesome 图标宏（FONT_AWESOME_HOUSE / _GEAR / _IMAGE / _WIFI / _SIGNAL_* 等）
#include <font_awesome.h>

#ifdef __cplusplus
extern "C" {
#endif

// 组合字体（Montserrat -> Font Awesome -> 普惠体），由 PhoneFontsInit() 初始化。
// 直接以 &phone_font_XX 替换原来的 &lv_font_montserrat_XX 即可获得图标 + 中文回退。
extern lv_font_t phone_font_12;
extern lv_font_t phone_font_14;
extern lv_font_t phone_font_18;

// 纯图标字体访问器（用于只显示图标的控件）。
const lv_font_t* PhoneIconFont(void);       // Font Awesome 20px（状态栏 / 列表图标）
const lv_font_t* PhoneIconFontLarge(void);  // Font Awesome 30px（桌面应用图标）

// 构建组合字体。必须在 lvgl_port_init() 之后、创建任何界面之前调用一次。
void PhoneFontsInit(void);

#ifdef __cplusplus
}
#endif
