#include "test_framework.h"

#include "phone_ui/phone_fonts.h"

#include <lvgl.h>
#include <src/others/test/lv_test.h>

int main() {
    lv_init();
    lv_test_display_create(320, 240);
    lv_test_indev_create_all();
    lv_indev_set_long_press_time(
        lv_test_indev_get_indev(LV_INDEV_TYPE_POINTER), 120);
    PhoneFontsInit();

    const int result = rodakos_home_ui_test::RunAllTests();
    lv_deinit();
    return result;
}
