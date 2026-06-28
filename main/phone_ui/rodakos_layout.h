#pragma once

#include <lvgl.h>

/**
 * RodakOS 布局系统
 *
 * 提供自动布局功能，开发时无需手动计算位置和间距
 * 组件会自动处理所有布局细节
 */

#ifdef __cplusplus
extern "C" {
#endif

// 屏幕布局区域
typedef enum {
    LAYOUT_AREA_HEADER,   // 顶部状态栏区域
    LAYOUT_AREA_BODY,     // 主内容区域
    LAYOUT_AREA_FOOTER,   // 底部区域
} layout_area_t;

// 布局配置
typedef struct {
    lv_coord_t screen_width;
    lv_coord_t screen_height;

    // 区域高度（自动计算 body 高度）
    lv_coord_t header_height;
    lv_coord_t footer_height;

    // 标准间距
    lv_coord_t padding_small;   // 小间距（元素之间）
    lv_coord_t padding_medium;  // 中间距（组件之间）
    lv_coord_t padding_large;   // 大间距（区域之间）
} layout_config_t;

// 全局布局配置（单例）
extern const layout_config_t* rodakos_layout_config;

/**
 * 初始化布局系统
 *
 * @param config 布局配置，如果为 NULL 则使用默认配置（320x240）
 */
void rodakos_layout_init(const layout_config_t* config);

/**
 * 创建布局容器
 *
 * 自动创建 header/body/footer 三个区域容器
 *
 * @param parent 父对象（通常是 lv_scr_act()）
 * @param header 输出：header 容器指针
 * @param body 输出：body 容器指针
 * @param footer 输出：footer 容器指针
 * @return 根容器
 */
lv_obj_t* rodakos_layout_create(lv_obj_t* parent,
                                lv_obj_t** header,
                                lv_obj_t** body,
                                lv_obj_t** footer);

/**
 * 在指定区域内创建居中网格容器
 *
 * @param parent 父容器（header/body/footer）
 * @param cols 列数
 * @param rows 行数
 * @param cell_width 单元格宽度
 * @param cell_height 单元格高度
 * @param gap_x 水平间距（自动应用）
 * @param gap_y 垂直间距（自动应用）
 * @return 网格容器（已自动居中）
 */
lv_obj_t* rodakos_layout_create_grid(lv_obj_t* parent,
                                     int cols, int rows,
                                     lv_coord_t cell_width,
                                     lv_coord_t cell_height,
                                     lv_coord_t gap_x,
                                     lv_coord_t gap_y);

/**
 * 在网格中自动放置子对象
 *
 * @param grid 网格容器
 * @param child 子对象
 * @param index 索引（自动计算行列位置）
 * @param cols 总列数
 * @param cell_width 单元格宽度
 * @param cell_height 单元格高度
 * @param gap_x 水平间距
 * @param gap_y 垂直间距
 */
void rodakos_layout_grid_place(lv_obj_t* grid,
                                lv_obj_t* child,
                                int index,
                                int cols,
                                lv_coord_t cell_width,
                                lv_coord_t cell_height,
                                lv_coord_t gap_x,
                                lv_coord_t gap_y);

/**
 * 创建 Flex 布局容器（水平或垂直自动排列）
 *
 * @param parent 父容器
 * @param direction LV_FLEX_FLOW_ROW 或 LV_FLEX_FLOW_COLUMN
 * @param gap 子元素间距（自动应用）
 * @return Flex 容器
 */
lv_obj_t* rodakos_layout_create_flex(lv_obj_t* parent,
                                     lv_flex_flow_t direction,
                                     lv_coord_t gap);

/**
 * 获取标准间距值
 */
lv_coord_t rodakos_layout_padding_small(void);
lv_coord_t rodakos_layout_padding_medium(void);
lv_coord_t rodakos_layout_padding_large(void);

#ifdef __cplusplus
}
#endif
