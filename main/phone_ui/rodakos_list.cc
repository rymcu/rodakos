#include "phone_ui/rodakos_list.h"

#include "phone_ui/phone_fonts.h"
#include "phone_ui/rodakos_theme.h"

namespace rodakos_ui {

lv_obj_t* CreateListLabel(lv_obj_t* parent, const char* text, bool secondary) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label,
        secondary ? rodakos_theme_text_secondary() : rodakos_theme_text_primary(), 0);
    lv_obj_set_style_text_font(label, &phone_font_14, 0);
    return label;
}

lv_obj_t* CreateListIcon(lv_obj_t* parent, const char* icon) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_color(label, rodakos_theme_primary(), 0);
    lv_obj_set_style_text_font(label, PhoneIconFont(), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    return label;
}

lv_obj_t* CreateGroupCaption(lv_obj_t* parent, const char* text, lv_coord_t& y) {
    auto* caption = CreateListLabel(parent, text, true);
    lv_obj_set_style_text_font(caption, &phone_font_12, 0);
    lv_obj_set_pos(caption, kGridX + 4, y + 3);
    y += kGroupLabelHeight;
    return caption;
}

lv_obj_t* CreateGroupList(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                          lv_coord_t height) {
    auto* list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, width, height);
    lv_obj_set_pos(list, x, y);
    lv_obj_set_style_bg_color(list, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(list, kListRadius, 0);
    lv_obj_set_style_clip_corner(list, true, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    return list;
}

lv_obj_t* CreateListRowAt(lv_obj_t* list, lv_coord_t y, lv_coord_t height, bool first, bool clickable) {
    auto* row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), height);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    if (clickable) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    } else {
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    }
    if (!first) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, rodakos_theme_bg_tertiary(), 0);
    }
    return row;
}

lv_obj_t* CreateListRow(lv_obj_t* list, int index, lv_coord_t width, lv_coord_t height,
                        bool clickable) {
    auto* row = CreateListRowAt(list, height * index, height, index == 0, clickable);
    lv_obj_set_width(row, width);
    return row;
}

lv_obj_t* CreateRowChevron(lv_obj_t* row) {
    auto* arrow = lv_label_create(row);
    lv_label_set_text(arrow, ICON_CHEVRON_RIGHT);
    lv_obj_set_style_text_color(arrow, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_text_font(arrow, PhoneIconFont(), 0);
    // The 20 px glyph has ~6 px of side bearing; pull it to the row's text edge.
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 4, 0);
    return arrow;
}

lv_obj_t* CreateRowValue(lv_obj_t* row, lv_coord_t width, bool chevron) {
    auto* value = CreateListLabel(row, "", true);
    lv_obj_set_width(value, width);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(value, &phone_font_12, 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, chevron ? -18 : 0, 0);
    return value;
}

}  // namespace rodakos_ui
