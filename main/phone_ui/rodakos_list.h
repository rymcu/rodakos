#pragma once

#include <lvgl.h>

// Grouped lists on the 320x200 app body, shared by Settings and 关于本机: lists at x=10,
// 300 px wide, 38 px rows, a 12 px grey caption above each group.
namespace rodakos_ui {

inline constexpr lv_coord_t kGridX = 10;
inline constexpr lv_coord_t kGridWidth = 300;
inline constexpr lv_coord_t kRowHeight = 38;
inline constexpr lv_coord_t kGroupLabelHeight = 20;
inline constexpr lv_coord_t kGroupGap = 4;
inline constexpr lv_coord_t kListRadius = 8;
// Title text starts here when the row has an icon.
inline constexpr lv_coord_t kRowTextX = 28;

// 14 px text in the primary or secondary text colour.
lv_obj_t* CreateListLabel(lv_obj_t* parent, const char* text, bool secondary = false);
// 20 px icon in the primary colour, aligned to the left edge of the row.
lv_obj_t* CreateListIcon(lv_obj_t* parent, const char* icon);
// Small grey caption above a group; advances `y` by kGroupLabelHeight.
lv_obj_t* CreateGroupCaption(lv_obj_t* parent, const char* text, lv_coord_t& y);
// Rounded list background; rows go inside with CreateListRow.
lv_obj_t* CreateGroupList(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                          lv_coord_t height);
// Row `index` of a list whose rows all have the same height. Pressed highlight when clickable,
// and a divider above every row but the first.
lv_obj_t* CreateListRow(lv_obj_t* list, int index, lv_coord_t width = kGridWidth,
                        lv_coord_t height = kRowHeight, bool clickable = true);
// Row at an explicit y, for lists that mix row heights.
lv_obj_t* CreateListRowAt(lv_obj_t* list, lv_coord_t y, lv_coord_t height, bool first, bool clickable);
lv_obj_t* CreateRowChevron(lv_obj_t* row);
// Right-aligned 12 px value; leaves room for the chevron when `chevron` is set.
lv_obj_t* CreateRowValue(lv_obj_t* row, lv_coord_t width, bool chevron);

}  // namespace rodakos_ui
