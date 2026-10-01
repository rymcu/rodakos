#include "phone_ui/boot_animation.h"

#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <esp_log.h>

#include <cmath>
#include <inttypes.h>

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

bool BootAnimation::Start(std::shared_ptr<rodakos::AppearanceBootAssets> assets) {
    PhoneUiLock lock(ui_);
    if (!lock.locked()) {
        ESP_LOGW(TAG, "Unable to lock LVGL for startup animation");
        return false;
    }
    if (root_ != nullptr) {
        return true;
    }
    assets_ = std::move(assets);
    completed_.store(false);
    completed_duration_ms_.store(0);
    minimum_display_ms_ = assets_ ? assets_->metadata.duration_ms - kFadeOutMs : kMinimumDisplayMs;
    started_tick_ = lv_tick_get();

    root_ = lv_obj_create(lv_layer_top());
    if (root_ == nullptr) {
        DestroyUnlocked();
        ESP_LOGW(TAG, "Unable to create startup animation surface");
        return false;
    }
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, ui_.width(), ui_.height());
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, assets_ ? lv_color_hex(assets_->metadata.background) : rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(root_, LV_OPA_COVER, 0);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    if (assets_ && assets_->metadata.animation_kind != "builtin") {
        const auto& metadata = assets_->metadata;
        custom_images_.resize(metadata.units.size());
        custom_units_.reserve(metadata.units.size());
        for (size_t i = 0; i < metadata.units.size(); ++i) {
            const auto& unit = metadata.units[i];
            const auto* resource = metadata.FindResource(unit.resource_id);
            const auto* data = resource != nullptr ? assets_->ResourceData(*resource) : nullptr;
            if (resource == nullptr || data == nullptr) {
                DestroyUnlocked();
                ESP_LOGW(TAG, "Custom animation resource missing");
                return false;
            }
            auto& descriptor = custom_images_[i];
            descriptor = {};
            descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
            descriptor.header.w = resource->width;
            descriptor.header.h = resource->height;
            descriptor.header.cf = resource->format == rodakos::AppearancePixelFormat::kA4
                ? LV_COLOR_FORMAT_A8 : resource->format == rodakos::AppearancePixelFormat::kRgb565
                ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_RGB565A8;
            descriptor.header.stride = resource->width * (resource->format == rodakos::AppearancePixelFormat::kA4 ? 1 : 2);
            descriptor.data_size = static_cast<uint32_t>(resource->width) * resource->height *
                (resource->format == rodakos::AppearancePixelFormat::kA4 ? 1 : resource->format == rodakos::AppearancePixelFormat::kRgb565 ? 2 : 3);
            descriptor.data = data;
            auto* image = lv_image_create(root_);
            if (image == nullptr) { DestroyUnlocked(); return false; }
            custom_units_.push_back(image);
            lv_image_set_src(image, &descriptor);
            if (resource->format == rodakos::AppearancePixelFormat::kA4) {
                lv_obj_set_style_image_recolor(image, lv_color_hex(metadata.color), 0);
                lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
            }
            lv_obj_set_style_image_opa(image, LV_OPA_TRANSP, 0);
            lv_obj_set_pos(image, unit.x, unit.y);
        }
    } else {
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
    ESP_LOGI(TAG, "Startup animation shown (%s, %" PRIu32 "ms)", assets_ ? "custom" : "builtin", minimum_display_ms_ + kFadeOutMs);
    return true;
}

void BootAnimation::Finish() {
    PhoneUiLock lock(ui_);
    if (!lock.locked() || root_ == nullptr) {
        return;
    }
    finish_requested_ = true;
    elapsed_ms_ = lv_tick_elaps(started_tick_);
    if (elapsed_ms_ >= minimum_display_ms_) {
        finishing_ = true;
        finish_elapsed_ms_ = 0;
        fade_started_tick_ = lv_tick_get();
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
        elapsed_ms_ = lv_tick_elaps(started_tick_);
        UpdateIntroUnlocked();
        if (finish_requested_ && elapsed_ms_ >= minimum_display_ms_) {
            finishing_ = true;
            finish_elapsed_ms_ = 0;
            fade_started_tick_ = lv_tick_get();
        }
        return;
    }

    finish_elapsed_ms_ = lv_tick_elaps(fade_started_tick_);
    const uint32_t clamped = finish_elapsed_ms_ > kFadeOutMs ? kFadeOutMs : finish_elapsed_ms_;
    const uint32_t remaining = kFadeOutMs - clamped;
    const auto opacity = static_cast<lv_opa_t>((remaining * LV_OPA_COVER) / kFadeOutMs);
    lv_obj_set_style_opa(root_, opacity, 0);
    if (clamped >= kFadeOutMs) {
        const uint32_t actual_duration = lv_tick_elaps(started_tick_);
        DestroyUnlocked();
        completed_duration_ms_.store(actual_duration);
        completed_.store(true);
        ESP_LOGI(TAG, "Startup animation completed (%" PRIu32 "ms), input restored=%d",
                 actual_duration, ui_.primary_input() != nullptr);
    }
}

void BootAnimation::UpdateIntroUnlocked() {
    if (root_ == nullptr) {
        return;
    }

    if (!custom_units_.empty() && assets_) {
        const auto& metadata = assets_->metadata;
        for (size_t i = 0; i < custom_units_.size(); ++i) {
            const auto& unit = metadata.units[i];
            const float progress = SmoothStep(elapsed_ms_, unit.start_ms, unit.duration_ms);
            lv_obj_set_style_image_opa(custom_units_[i], ScaleOpacity(progress), 0);
            const int32_t offset = metadata.animation_template == "fade" ? 0
                : static_cast<int32_t>(std::lround((1.0F - progress) * kLogoOffsetY));
            lv_obj_set_y(custom_units_[i], unit.y + offset);
        }
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
    for (auto& descriptor : custom_images_) {
        lv_image_cache_drop(&descriptor);
    }
    custom_units_.clear();
    custom_images_.clear();
    assets_.reset();
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
