#include "phone_ui/rodakos_layout.h"
#include "phone_ui/rodakos_theme.h"
#include <esp_log.h>

static const char* TAG = "Layout";

// 默认布局配置（320x240 屏幕）
// Body 需要容纳 3行×64高度 + 2个8px间距 = 208px
static const layout_config_t default_config = {
    .screen_width = 320,
    .screen_height = 240,
    .header_height = 28,  // 紧凑的 header
    .footer_height = 20,  // 紧凑的 footer
    .padding_small = 4,
    .padding_medium = 8,
    .padding_large = 16,
};

const layout_config_t* rodakos_layout_config = &default_config;

void rodakos_layout_init(const layout_config_t* config) {
    if (config != nullptr) {
        rodakos_layout_config = config;
    } else {
        rodakos_layout_config = &default_config;
    }
    ESP_LOGI(TAG, "Layout initialized: %dx%d, header=%d, footer=%d",
             rodakos_layout_config->screen_width,
             rodakos_layout_config->screen_height,
             rodakos_layout_config->header_height,
             rodakos_layout_config->footer_height);
}

lv_obj_t* rodakos_layout_create(lv_obj_t* parent,
                                lv_obj_t** header,
                                lv_obj_t** body,
                                lv_obj_t** footer) {
    const layout_config_t* cfg = rodakos_layout_config;

    // 计算 body 高度
    const lv_coord_t body_height = cfg->screen_height - cfg->header_height - cfg->footer_height;

    // 根容器
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, cfg->screen_width, cfg->screen_height);
    lv_obj_set_style_bg_color(root, rodakos_theme_bg_primary(), 0);  // 使用主题背景色
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    // Header 容器
    if (header != nullptr) {
        *header = lv_obj_create(root);
        lv_obj_remove_style_all(*header);
        lv_obj_set_size(*header, cfg->screen_width, cfg->header_height);
        lv_obj_set_pos(*header, 0, 0);
        lv_obj_set_style_bg_opa(*header, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(*header, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(*header, LV_OBJ_FLAG_CLICKABLE);
    }

    // Body 容器（可滚动）
    if (body != nullptr) {
        *body = lv_obj_create(root);
        lv_obj_remove_style_all(*body);
        lv_obj_set_size(*body, cfg->screen_width, body_height);
        lv_obj_set_pos(*body, 0, cfg->header_height);
        lv_obj_set_style_bg_opa(*body, LV_OPA_TRANSP, 0);
        // 启用滚动
        lv_obj_set_scrollbar_mode(*body, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_scroll_dir(*body, LV_DIR_VER);  // 仅垂直滚动
    }

    // Footer 容器
    if (footer != nullptr) {
        *footer = lv_obj_create(root);
        lv_obj_remove_style_all(*footer);
        lv_obj_set_size(*footer, cfg->screen_width, cfg->footer_height);
        lv_obj_set_pos(*footer, 0, cfg->header_height + body_height);
        lv_obj_set_style_bg_opa(*footer, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(*footer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(*footer, LV_OBJ_FLAG_CLICKABLE);
    }

    return root;
}

lv_obj_t* rodakos_layout_create_grid(lv_obj_t* parent,
                                     int cols, int rows,
                                     lv_coord_t cell_width,
                                     lv_coord_t cell_height,
                                     lv_coord_t gap_x,
                                     lv_coord_t gap_y) {
    // 计算网格总尺寸
    const lv_coord_t grid_width = cols * cell_width + (cols - 1) * gap_x;
    const lv_coord_t grid_height = rows * cell_height + (rows - 1) * gap_y;

    // 创建网格容器
    lv_obj_t* grid = lv_obj_create(parent);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, grid_width, grid_height);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    // 自动居中在父容器内
    lv_obj_center(grid);

    ESP_LOGI(TAG, "Grid created: %dx%d cells, size %dx%d, gap %d,%d",
             cols, rows, grid_width, grid_height, gap_x, gap_y);

    return grid;
}

void rodakos_layout_grid_place(lv_obj_t* grid,
                                lv_obj_t* child,
                                int index,
                                int cols,
                                lv_coord_t cell_width,
                                lv_coord_t cell_height,
                                lv_coord_t gap_x,
                                lv_coord_t gap_y) {
    // 自动计算行列位置
    const int col = index % cols;
    const int row = index / cols;
    const lv_coord_t x = col * (cell_width + gap_x);
    const lv_coord_t y = row * (cell_height + gap_y);

    // 设置位置（相对于 grid 容器）
    lv_obj_set_pos(child, x, y);
}

lv_obj_t* rodakos_layout_create_flex(lv_obj_t* parent,
                                     lv_flex_flow_t direction,
                                     lv_coord_t gap) {
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);

    // 设置 Flex 布局
    if (direction == LV_FLEX_FLOW_ROW || direction == LV_FLEX_FLOW_ROW_WRAP ||
        direction == LV_FLEX_FLOW_ROW_REVERSE || direction == LV_FLEX_FLOW_ROW_WRAP_REVERSE) {
        lv_obj_set_style_pad_column(container, gap, 0);
    } else {
        lv_obj_set_style_pad_row(container, gap, 0);
    }

    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, direction);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    return container;
}

lv_coord_t rodakos_layout_padding_small(void) {
    return rodakos_layout_config->padding_small;
}

lv_coord_t rodakos_layout_padding_medium(void) {
    return rodakos_layout_config->padding_medium;
}

lv_coord_t rodakos_layout_padding_large(void) {
    return rodakos_layout_config->padding_medium;
}
