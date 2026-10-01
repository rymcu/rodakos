#include "lvgl_creation_failures.h"

#include <lvgl.h>

namespace rodakos_home_ui_test {
namespace {
LvglCreationFailure armed_failure = LvglCreationFailure::kNone;
}
void ArmLvglCreationFailure(LvglCreationFailure failure) {
    armed_failure = failure;
}
bool ConsumeLvglCreationFailure(LvglCreationFailure failure) {
    if (armed_failure != failure) return false;
    armed_failure = LvglCreationFailure::kNone;
    return true;
}
}

#ifdef RODAKOS_TEST_LVGL_WRAPPERS
extern "C" {
lv_obj_t* __real_lv_obj_create(lv_obj_t* parent);
lv_obj_t* __real_lv_image_create(lv_obj_t* parent);
lv_timer_t* __real_lv_timer_create(lv_timer_cb_t callback, uint32_t period, void* user_data);

lv_obj_t* __wrap_lv_obj_create(lv_obj_t* parent) {
    if (rodakos_home_ui_test::ConsumeLvglCreationFailure(
            rodakos_home_ui_test::LvglCreationFailure::kObject)) return nullptr;
    return __real_lv_obj_create(parent);
}
lv_obj_t* __wrap_lv_image_create(lv_obj_t* parent) {
    if (rodakos_home_ui_test::ConsumeLvglCreationFailure(
            rodakos_home_ui_test::LvglCreationFailure::kImage)) return nullptr;
    return __real_lv_image_create(parent);
}
lv_timer_t* __wrap_lv_timer_create(lv_timer_cb_t callback, uint32_t period, void* user_data) {
    if (rodakos_home_ui_test::ConsumeLvglCreationFailure(
            rodakos_home_ui_test::LvglCreationFailure::kTimer)) return nullptr;
    return __real_lv_timer_create(callback, period, user_data);
}
}
#endif
