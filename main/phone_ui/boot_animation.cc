#include "phone_ui/boot_animation.h"

#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_log.h>

#include <cmath>

namespace {
constexpr const char* TAG = "BootAnimation";
constexpr char kLogoText[] = "RODAKOS";

float SmoothStep(uint32_t elapsed_ms, uint32_t start_ms, uint32_t duration_ms) {
    if (elapsed_ms <= start_ms) {
        return 0.0F;
    }
    if (elapsed_ms >= start_ms + duration_ms) {
        return 1.0F;
    }
    const float progress = static_cast<float>(elapsed_ms - start_ms) /
                          static_cast<float>(duration_ms);
    return progress * progress * (3.0F - 2.0F * progress);
}

lv_opa_t ScaleOpacity(float progress, lv_opa_t maximum = LV_OPA_COVER) {
    return static_cast<lv_opa_t>(std::lround(progress * maximum));
}
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

    const int32_t logo_width =
        static_cast<int32_t>(kLogoLetterCount * kLogoLetterCellWidth);
    const int32_t logo_x = (ui_.width() - logo_width) / 2;
    for (size_t i = 0; i < kLogoLetterCount; ++i) {
        logo_letters_[i] = lv_label_create(root_);
        if (logo_letters_[i] == nullptr) {
            DestroyUnlocked();
            ESP_LOGW(TAG, "Unable to create startup logo letter");
            return false;
        }
        char letter[2] = {kLogoText[i], '\0'};
        lv_label_set_text(logo_letters_[i], letter);
        lv_obj_set_width(logo_letters_[i], kLogoLetterCellWidth);
        lv_obj_set_style_text_align(logo_letters_[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(logo_letters_[i], &phone_font_logo, 0);
        lv_obj_set_style_text_color(logo_letters_[i], rodakos_theme_text_primary(), 0);
        lv_obj_set_style_opa(logo_letters_[i], LV_OPA_TRANSP, 0);
        lv_obj_set_pos(logo_letters_[i],
                       logo_x + static_cast<int32_t>(i * kLogoLetterCellWidth),
                       kLogoBaseY + kLogoOffsetY);
    }

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
    UpdateIntroUnlocked();
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
        UpdateIntroUnlocked();
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

void BootAnimation::UpdateIntroUnlocked() {
    if (root_ == nullptr) {
        return;
    }

    for (size_t i = 0; i < kLogoLetterCount; ++i) {
        const uint32_t start_ms = kLogoStartMs + static_cast<uint32_t>(i) * kLetterStaggerMs;
        const float progress = SmoothStep(elapsed_ms_, start_ms, kLetterDurationMs);
        const auto opacity = ScaleOpacity(progress);
        const int32_t y_offset = static_cast<int32_t>(std::lround(
            (1.0F - progress) * static_cast<float>(kLogoOffsetY)));
        lv_obj_set_style_opa(logo_letters_[i], opacity, 0);
        lv_obj_set_y(logo_letters_[i], kLogoBaseY + y_offset);
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
    for (auto& letter : logo_letters_) {
        letter = nullptr;
    }
    elapsed_ms_ = 0;
    finishing_ = false;
    finish_requested_ = false;
    finish_elapsed_ms_ = 0;
    if (ui_.primary_input() != nullptr) {
        lv_indev_enable(ui_.primary_input(), true);
    }
}
