#pragma once

#include "phone_ui/phone_ui.h"

#include <string>

constexpr lv_coord_t kRodakosAppHeaderHeight = 40;
constexpr lv_coord_t kRodakosAppHeaderPadding = 10;

lv_obj_t* PhoneCreateIconButton(PhoneUi& ui, lv_obj_t* parent, const char* icon, lv_coord_t size, bool subtle = false);
lv_obj_t* RodakosCreateHeaderIconButton(lv_obj_t* parent, const char* icon);
lv_obj_t* CreateAppHeader(lv_obj_t* parent,
                          const char* title,
                          lv_event_cb_t back_cb,
                          lv_event_cb_t home_cb,
                          void* user_data,
                          lv_obj_t** title_label = nullptr);
lv_obj_t* CreateAppHeader(lv_obj_t* parent,
                          const char* title,
                          lv_event_cb_t back_cb,
                          lv_event_cb_t action_cb,
                          void* user_data,
                          lv_obj_t** title_label,
                          const char* action_icon,
                          lv_obj_t** action_button = nullptr);
lv_obj_t* PhoneCreateCard(PhoneUi& ui, lv_obj_t* parent, lv_coord_t width, lv_coord_t height);
lv_obj_t* PhoneCreateLabel(PhoneUi& ui, lv_obj_t* parent, const char* text, bool secondary = false);
lv_obj_t* PhoneCreateTextButton(PhoneUi& ui, lv_obj_t* parent, const char* text, lv_coord_t width, lv_coord_t height);

struct PhoneStatusBlock {
    lv_obj_t* label;
    lv_obj_t* button;
};

PhoneStatusBlock CreatePhoneStatusBlock(PhoneUi& ui, lv_obj_t* parent, const char* text,
                                       const char* action_text = nullptr, lv_coord_t action_width = 96);
