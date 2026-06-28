#pragma once

#include "phone_ui/phone_ui.h"

#include <string>

lv_obj_t* PhoneCreateIconButton(PhoneUi& ui, lv_obj_t* parent, const char* icon, lv_coord_t size, bool subtle = false);
lv_obj_t* RodakosCreateHeaderIconButton(lv_obj_t* parent, const char* icon);
lv_obj_t* PhoneCreateCard(PhoneUi& ui, lv_obj_t* parent, lv_coord_t width, lv_coord_t height);
lv_obj_t* PhoneCreateLabel(PhoneUi& ui, lv_obj_t* parent, const char* text, bool secondary = false);
lv_obj_t* PhoneCreateTextButton(PhoneUi& ui, lv_obj_t* parent, const char* text, lv_coord_t width, lv_coord_t height);
