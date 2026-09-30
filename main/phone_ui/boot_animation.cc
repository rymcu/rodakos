#include "phone_ui/boot_animation.h"

#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_log.h>

namespace {
constexpr const char* TAG = "BootAnimation";
}

BootAnimation::BootAnimation(PhoneUi& ui) : ui_(ui) {}

BootAnimation::~BootAnimation() {
    Stop();
}

bool BootAnimation::Start() {
    PhoneUiLock lock(ui_);
    if (!lock.locked()) {
        ESP_LOGW(TAG, "Unable to lock LVGL for startup animation");
        return false;
    }
    if (root_ != nullptr) {
        return true;
    }

    root_ = lv_obj_create(lv_layer_top());
    if (root_ == nullptr) {
        ESP_LOGW(TAG, "Unable to create startup animation surface");
        return false;
    }
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, ui_.width(), ui_.height());
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(root_, LV_OPA_COVER, 0);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    auto* spinner = lv_spinner_create(root_);
    if (spinner == nullptr) {
        DestroyUnlocked();
        ESP_LOGW(TAG, "Unable to create startup spinner");
        return false;
    }
    lv_obj_set_size(spinner, 58, 58);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);
    lv_spinner_set_anim_params(spinner, 1000, 95);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(spinner, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, rodakos_theme_primary(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(spinner, LV_OPA_COVER, LV_PART_INDICATOR);

    auto* title = lv_label_create(root_);
    lv_label_set_text(title, "RodakOS");
    lv_obj_set_style_text_font(title, &phone_font_18, 0);
    lv_obj_set_style_text_color(title, rodakos_theme_text_primary(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 30);

    auto* subtitle = lv_label_create(root_);
    lv_label_set_text(subtitle, "Starting");
    lv_obj_set_style_text_font(subtitle, &phone_font_12, 0);
    lv_obj_set_style_text_color(subtitle, rodakos_theme_text_secondary(), 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 52);

    if (ui_.primary_input() != nullptr) {
        lv_indev_enable(ui_.primary_input(), false);
    }
    lv_obj_move_foreground(root_);
    timer_ = lv_timer_create(TimerCallback, kFramePeriodMs, this);
    if (timer_ == nullptr) {
        DestroyUnlocked();
        ESP_LOGW(TAG, "Unable to create startup animation timer");
        return false;
    }
    ESP_LOGI(TAG, "Startup animation shown");
    return true;
}

void BootAnimation::Finish() {
    PhoneUiLock lock(ui_);
    if (!lock.locked() || root_ == nullptr) {
        return;
    }
    finish_requested_ = true;
    if (elapsed_ms_ >= kMinimumDisplayMs) {
        finishing_ = true;
        finish_elapsed_ms_ = 0;
    }
}

void BootAnimation::Stop() {
    PhoneUiLock lock(ui_);
    if (!lock.locked()) {
        return;
    }
    DestroyUnlocked();
}

void BootAnimation::TimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<BootAnimation*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->Tick();
    }
}

void BootAnimation::Tick() {
    if (root_ == nullptr || !lv_obj_is_valid(root_)) {
        DestroyUnlocked();
        return;
    }
    if (!finishing_) {
        elapsed_ms_ += kFramePeriodMs;
        if (finish_requested_ && elapsed_ms_ >= kMinimumDisplayMs) {
            finishing_ = true;
            finish_elapsed_ms_ = 0;
        }
        return;
    }

    finish_elapsed_ms_ += kFramePeriodMs;
    const uint32_t clamped = finish_elapsed_ms_ > kFadeOutMs ? kFadeOutMs : finish_elapsed_ms_;
    const uint32_t remaining = kFadeOutMs - clamped;
    const auto opacity = static_cast<lv_opa_t>((remaining * LV_OPA_COVER) / kFadeOutMs);
    lv_obj_set_style_opa(root_, opacity, 0);
    if (clamped >= kFadeOutMs) {
        DestroyUnlocked();
    }
}

void BootAnimation::DestroyUnlocked() {
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
    root_ = nullptr;
    elapsed_ms_ = 0;
    finishing_ = false;
    finish_requested_ = false;
    finish_elapsed_ms_ = 0;
    if (ui_.primary_input() != nullptr) {
        lv_indev_enable(ui_.primary_input(), true);
    }
}
