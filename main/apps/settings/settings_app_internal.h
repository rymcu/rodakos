#pragma once

#include "phone_ui/rodakos_list.h"
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

// Grid and grouped-list helpers are shared with other apps; see phone_ui/rodakos_list.h.
using rodakos_ui::kGridX;
using rodakos_ui::kGridWidth;
using rodakos_ui::kRowHeight;
using rodakos_ui::kGroupLabelHeight;
using rodakos_ui::kGroupGap;
using rodakos_ui::kListRadius;
using rodakos_ui::CreateGroupCaption;
using rodakos_ui::CreateGroupList;
using rodakos_ui::CreateListRow;
using rodakos_ui::CreateRowChevron;
using rodakos_ui::CreateRowValue;

inline constexpr lv_coord_t kButtonRadius = 7;
// Bottom action row of a page body that does not scroll.
inline constexpr lv_coord_t kActionY = 150;
inline constexpr lv_coord_t kActionHeight = 40;
inline constexpr lv_coord_t kDialogButtonHeight = 36;

enum class SettingButtonStyle { kPrimary, kSecondary, kDanger, kDangerOutline };

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
