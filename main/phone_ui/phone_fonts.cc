#include "phone_ui/phone_fonts.h"

// 来自 78/xiaozhi-fonts 组件的字体（每个字体是独立的 .c 文件，
// 只有被引用的字体才会被链接进固件，未引用的不会占用 flash）。
LV_FONT_DECLARE(font_puhui_16_4);    // 中文 / CJK
LV_FONT_DECLARE(font_awesome_20_4);  // 图标（状态栏 / 列表 / 文本内联）
LV_FONT_DECLARE(font_awesome_30_4);  // 图标（桌面大图标）

lv_font_t phone_font_12;
lv_font_t phone_font_14;
lv_font_t phone_font_18;

namespace {

// Font Awesome 20px 的可写副本，其回退指向普惠体，
// 作为文本组合字体的中间环（图标 -> 中文）。
lv_font_t s_icon_20;
bool s_initialized = false;

// 复制一份字体描述并设置回退字体。
// lv_font_t 是普通结构体，复制后与源字体共享同一份字形数据，
// 仅 fallback 指针不同，这是 LVGL 官方推荐的回退用法。
void MakeComposite(lv_font_t* out, const lv_font_t* base, const lv_font_t* fallback) {
    *out = *base;
    out->fallback = fallback;
}

}  // namespace

void PhoneFontsInit(void) {
    if (s_initialized) {
        return;
    }

    // 图标字体回退到中文：图标 -> 中文。
    MakeComposite(&s_icon_20, &font_awesome_20_4, &font_puhui_16_4);

    // 文本字体回退链：Montserrat（拉丁） -> Font Awesome（图标） -> 普惠体（中文）。
    MakeComposite(&phone_font_12, &lv_font_montserrat_12, &s_icon_20);
    MakeComposite(&phone_font_14, &lv_font_montserrat_14, &s_icon_20);
    MakeComposite(&phone_font_18, &lv_font_montserrat_18, &s_icon_20);

    s_initialized = true;
}

const lv_font_t* PhoneIconFont(void) {
    return &s_icon_20;
}

const lv_font_t* PhoneIconFontLarge(void) {
    return &font_awesome_30_4;
}
