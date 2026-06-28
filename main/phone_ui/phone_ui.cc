#include "phone_ui/phone_ui.h"

#include <esp_lvgl_port.h>

PhoneUi::PhoneUi(int width, int height)
    : width_(width), height_(height), theme_(PhoneDarkTheme()) {}

bool PhoneUi::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void PhoneUi::Unlock() {
    lvgl_port_unlock();
}

lv_obj_t* PhoneUi::screen() const {
    return lv_screen_active();
}

void PhoneUi::SetThemeName(const std::string& name) {
    theme_name_ = name == "light" ? "light" : "dark";
    theme_ = theme_name_ == "light" ? PhoneLightTheme() : PhoneDarkTheme();
}

void PhoneUi::ShowToast(const char* message, int duration_ms) {
    PhoneUiLock lock(*this);
    if (!lock.locked()) {
        return;
    }
    ShowToastUnlocked(message, duration_ms);
}

void PhoneUi::ShowToastUnlocked(const char* message, int duration_ms) {
    if (toast_ == nullptr) {
        toast_ = lv_label_create(screen());
        lv_obj_set_width(toast_, 260);
        lv_obj_set_style_radius(toast_, 12, 0);
        lv_obj_set_style_bg_color(toast_, theme_.surface_alt, 0);
        lv_obj_set_style_bg_opa(toast_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(toast_, theme_.text_primary, 0);
        lv_obj_set_style_text_align(toast_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_all(toast_, 10, 0);
        lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -14);
    }

    lv_label_set_text(toast_, message != nullptr ? message : "");
    lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_);

    if (toast_timer_ != nullptr) {
        lv_timer_delete(toast_timer_);
    }
    toast_timer_ = lv_timer_create([](lv_timer_t* timer) {
        auto* ui = static_cast<PhoneUi*>(lv_timer_get_user_data(timer));
        if (ui != nullptr && ui->toast_ != nullptr) {
            lv_obj_add_flag(ui->toast_, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui != nullptr) {
            ui->toast_timer_ = nullptr;
        }
        lv_timer_delete(timer);
    }, duration_ms, this);
    lv_timer_set_repeat_count(toast_timer_, 1);
}
