#pragma once

#include <lvgl.h>
#include <cstdint>

/**
 * RodakOS 全局主题色系统
 *
 * 提供统一的颜色管理，所有界面使用同一套配色方案
 */

#ifdef __cplusplus
extern "C" {
#endif

// 主题色定义
typedef struct {
    // 基础色
    uint32_t primary;        // 主色调
    uint32_t secondary;      // 辅助色
    uint32_t accent;         // 强调色

    // 背景色
    uint32_t bg_primary;     // 主背景（通常是黑色或深色）
    uint32_t bg_secondary;   // 次背景（卡片、容器）
    uint32_t bg_tertiary;    // 三级背景（悬浮元素）

    // 文字色
    uint32_t text_primary;   // 主文字（高对比度）
    uint32_t text_secondary; // 次文字（中等对比度）
    uint32_t text_tertiary;  // 三级文字（低对比度）

    // 状态色
    uint32_t success;        // 成功
    uint32_t warning;        // 警告
    uint32_t error;          // 错误
    uint32_t info;           // 信息

    // 特殊色
    uint32_t border;         // 边框
    uint32_t divider;        // 分割线
    uint32_t overlay;        // 遮罩层

    // 图标色板（8种柔和的颜色用于应用图标）
    uint32_t icon_palette[8];
} rodakos_theme_t;

// 预设主题
typedef enum {
    RODAKOS_THEME_DARK,      // 深色主题（默认）
    RODAKOS_THEME_LIGHT,     // 浅色主题
    RODAKOS_THEME_BLUE,      // 蓝色主题
    RODAKOS_THEME_GREEN,     // 绿色主题
    RODAKOS_THEME_CUSTOM,    // 自定义主题
} rodakos_theme_preset_t;

/**
 * 初始化主题系统
 *
 * @param preset 预设主题类型
 */
void rodakos_theme_init(rodakos_theme_preset_t preset);

/**
 * 根据持久化的主题 id 应用主题。
 *
 * @param theme_id 主题 id：dark/light/blue/green
 * @return 实际应用的预设主题
 */
rodakos_theme_preset_t rodakos_theme_init_from_name(const char* theme_id);

/**
 * 判断指定主题是否应使用 PhoneUi 的浅色组件主题。
 */
bool rodakos_theme_is_light_name(const char* theme_id);

/**
 * 设置自定义主题
 *
 * @param theme 自定义主题配置
 */
void rodakos_theme_set_custom(const rodakos_theme_t* theme);

/**
 * 获取当前主题
 *
 * @return 当前主题配置指针
 */
const rodakos_theme_t* rodakos_theme_get(void);

/**
 * 获取主题颜色（包装为 lv_color_t）
 */
lv_color_t rodakos_theme_primary(void);
lv_color_t rodakos_theme_secondary(void);
lv_color_t rodakos_theme_accent(void);

lv_color_t rodakos_theme_bg_primary(void);
lv_color_t rodakos_theme_bg_secondary(void);
lv_color_t rodakos_theme_bg_tertiary(void);

lv_color_t rodakos_theme_text_primary(void);
lv_color_t rodakos_theme_text_secondary(void);
lv_color_t rodakos_theme_text_tertiary(void);

lv_color_t rodakos_theme_success(void);
lv_color_t rodakos_theme_warning(void);
lv_color_t rodakos_theme_error(void);
lv_color_t rodakos_theme_info(void);

lv_color_t rodakos_theme_border(void);
lv_color_t rodakos_theme_divider(void);
lv_color_t rodakos_theme_overlay(void);

/**
 * 获取图标色板中的颜色
 *
 * @param index 索引（0-7）
 * @return 图标颜色
 */
lv_color_t rodakos_theme_icon_color(int index);

/**
 * 应用主题到 LVGL 全局样式（可选）
 */
void rodakos_theme_apply_to_lvgl(void);

#ifdef __cplusplus
}
#endif
