#pragma once

#include "phone_ui/rodakos_theme.h"

#include <lvgl.h>
#include <cstdint>
#include <string>

namespace rodakos_settings {

inline constexpr const char* kDisplayNamespace = "display";
inline constexpr const char* kBrightnessKey = "brightness";
inline constexpr uint32_t kTimeSyncTimeoutPolls = 20;

void DeferReturnHome(void* user_data);
void DeferOpenThemes(void* user_data);
std::string TrimServerName(const char* text);
std::string TrimCloudUrl(const char* text);
void UpdateBrightnessLabel(lv_obj_t* label, int value);
lv_obj_t* CreateSettingLabel(lv_obj_t* parent, const char* text, bool secondary = false);
lv_obj_t* CreateSettingIcon(lv_obj_t* parent, const char* icon);

// Shared Settings grid on the 320x200 body: lists at x=10, 300 px wide, 38 px rows.
inline constexpr lv_coord_t kGridX = 10;
inline constexpr lv_coord_t kGridWidth = 300;
inline constexpr lv_coord_t kRowHeight = 38;
inline constexpr lv_coord_t kGroupLabelHeight = 20;
inline constexpr lv_coord_t kGroupGap = 4;
inline constexpr lv_coord_t kButtonRadius = 7;
inline constexpr lv_coord_t kListRadius = 8;
// Bottom action row of a page body that does not scroll.
inline constexpr lv_coord_t kActionY = 150;
inline constexpr lv_coord_t kActionHeight = 40;
inline constexpr lv_coord_t kDialogButtonHeight = 36;

enum class SettingButtonStyle { kPrimary, kSecondary, kDanger, kDangerOutline };

// Small grey caption above a group; advances `y` by kGroupLabelHeight.
lv_obj_t* CreateGroupCaption(lv_obj_t* parent, const char* text, lv_coord_t& y);
// Rounded list background; rows go inside with CreateListRow.
lv_obj_t* CreateGroupList(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t width,
                          lv_coord_t height);
// Pressed highlight, and a divider above every row but the first.
lv_obj_t* CreateListRow(lv_obj_t* list, int index, lv_coord_t width = kGridWidth,
                        lv_coord_t height = kRowHeight, bool clickable = true);
lv_obj_t* CreateRowChevron(lv_obj_t* row);
// Right-aligned 12 px value; leaves room for the chevron when `chevron` is set.
lv_obj_t* CreateRowValue(lv_obj_t* row, lv_coord_t width, bool chevron);

lv_obj_t* CreateSettingButton(lv_obj_t* parent, const char* text, SettingButtonStyle style,
                              lv_coord_t width, lv_coord_t height = kActionHeight,
                              lv_obj_t** label_out = nullptr);
void StyleSettingButton(lv_obj_t* button, lv_obj_t* label, SettingButtonStyle style);

// Dimmed full-screen layer with a centred box, for confirmations without text entry.
// Text-entry dialogs stay full-screen and opaque so the soft keyboard has room.
struct SettingDialog {
    lv_obj_t* layer = nullptr;
    lv_obj_t* box = nullptr;
};
inline constexpr lv_coord_t kDialogWidth = 296;
inline constexpr lv_coord_t kDialogPad = 14;
inline constexpr lv_coord_t kDialogContentWidth = kDialogWidth - 2 * kDialogPad;
inline constexpr lv_coord_t kDialogHalfButton = (kDialogContentWidth - 12) / 2;
SettingDialog CreateSettingDialog(lv_coord_t height);
lv_obj_t* CreateDialogTitle(lv_obj_t* box, const char* text);
lv_obj_t* CreateDialogMessage(lv_obj_t* box, const char* text, lv_coord_t y);
// Cancel on the left, action on the right, both kDialogHalfButton wide.
lv_obj_t* CreateDialogButtons(lv_obj_t* box, const char* action, SettingButtonStyle style,
                              lv_event_cb_t cancel_cb, lv_event_cb_t action_cb, void* user_data);

}  // namespace rodakos_settings
