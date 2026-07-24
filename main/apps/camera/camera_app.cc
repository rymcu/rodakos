#include "apps/camera/camera_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>

struct CameraCaptureGuard {
    std::atomic<CameraApp*> app{nullptr};
    std::atomic<bool> running{false};
    std::atomic<uint32_t> generation{0};
};

namespace {
constexpr const char* TAG = "CameraApp";
constexpr lv_coord_t kPreviewBoxWidth = 304;
constexpr lv_coord_t kPreviewBoxHeight = 154;
constexpr lv_coord_t kCaptureButtonSize = 54;
constexpr uint32_t kCaptureTaskStackBytes = 4096;

struct CameraCapturePayload {
    std::shared_ptr<CameraCaptureGuard> guard;
    rodakos::CameraService* camera = nullptr;
    bool ok = false;
    std::string saved_path;
    std::string error;
    uint32_t generation = 0;
};

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        context->navigation().ReturnHome();
    }
}

lv_obj_t* CreateCaptureButton(lv_obj_t* parent) {
    auto* button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, kCaptureButtonSize, kCaptureButtonSize);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, rodakos_theme_primary(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 4, 0);
    lv_obj_set_style_border_color(button, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_color(button, rodakos_theme_bg_tertiary(), LV_STATE_DISABLED);
    lv_obj_set_style_translate_y(button, 2, LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    auto* icon = lv_label_create(button);
    lv_label_set_text(icon, FONT_AWESOME_CAMERA);
    lv_obj_set_style_text_font(icon, PhoneIconFont(), 0);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    lv_obj_center(icon);
    return button;
}

void CaptureCompleteCallback(void* user_data) {
    auto* payload = static_cast<CameraCapturePayload*>(user_data);
    if (payload == nullptr) {
        return;
    }

    auto guard = payload->guard;
    CameraApp* app = guard ? guard->app.load() : nullptr;
    if (app != nullptr) {
        app->OnCaptureComplete(payload->ok, payload->saved_path, payload->error, payload->generation);
    } else if (guard && payload->generation == guard->generation.load()) {
        guard->running.store(false);
    }
    delete payload;
}

void CaptureTask(void* arg) {
    auto* payload = static_cast<CameraCapturePayload*>(arg);
    if (payload != nullptr && payload->camera != nullptr) {
        payload->ok = payload->camera->CapturePhoto(payload->saved_path);
        if (!payload->ok) {
            payload->error = payload->camera->last_error();
        }

        bool queued = false;
        if (lvgl_port_lock(1000)) {
            queued = lv_async_call(CaptureCompleteCallback, payload) == LV_RESULT_OK;
            lvgl_port_unlock();
        }
        if (!queued) {
            if (payload->guard && payload->generation == payload->guard->generation.load()) {
                payload->guard->running.store(false);
            }
            delete payload;
        }
    } else {
        delete payload;
    }
    vTaskDelete(nullptr);
}

void LogCaptureTaskCreateFailure() {
    ESP_LOGW(TAG,
             "Failed to start capture task: internal_free=%u internal_largest=%u "
             "spiram_free=%u spiram_largest=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

}  // namespace

bool CameraApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    camera_ = context.services().camera();
    audio_focus_ = context.services().audio_focus();
    capture_guard_ = std::make_shared<CameraCaptureGuard>();
    capture_guard_->app.store(this);
    RequestAudioResources();

    const bool preview_started = camera_ != nullptr && camera_->StartPreview();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        if (camera_ != nullptr) {
            camera_->StopPreview();
        }
        ReleaseAudioResources();
        return false;
    }

    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "Camera", [](lv_event_t* e) {
        auto* self = static_cast<CameraApp*>(lv_event_get_user_data(e));
        self->NavigateBack();
    }, [](lv_event_t* e) {
        auto* self = static_cast<CameraApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, this);

    preview_box_ = lv_obj_create(root_);
    lv_obj_remove_style_all(preview_box_);
    lv_obj_set_size(preview_box_, kPreviewBoxWidth, kPreviewBoxHeight);
    lv_obj_align(preview_box_, LV_ALIGN_TOP_MID, 0, kRodakosAppHeaderHeight + 6);
    lv_obj_set_style_bg_color(preview_box_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(preview_box_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(preview_box_, 8, 0);
    lv_obj_set_style_clip_corner(preview_box_, true, 0);
    lv_obj_clear_flag(preview_box_, LV_OBJ_FLAG_SCROLLABLE);

    preview_image_ = lv_image_create(preview_box_);
    lv_obj_center(preview_image_);

    placeholder_label_ = lv_label_create(preview_box_);
    lv_label_set_text(placeholder_label_, preview_started ? "Starting preview..." : "Camera unavailable");
    lv_obj_set_width(placeholder_label_, kPreviewBoxWidth - 28);
    lv_label_set_long_mode(placeholder_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(placeholder_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(placeholder_label_, rodakos_theme_text_secondary(), 0);
    lv_obj_set_style_text_font(placeholder_label_, &phone_font_14, 0);
    lv_obj_center(placeholder_label_);

    status_label_ = lv_label_create(root_);
    lv_obj_set_width(status_label_, 300);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(status_label_, &phone_font_12, 0);
    lv_obj_align(status_label_, LV_ALIGN_BOTTOM_MID, 0, -4);

    capture_button_ = CreateCaptureButton(root_);
    lv_obj_align(capture_button_, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_add_event_cb(capture_button_, [](lv_event_t* e) {
        auto* self = static_cast<CameraApp*>(lv_event_get_user_data(e));
        self->CapturePhoto();
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_state(capture_button_, LV_STATE_DISABLED);

    if (preview_started) {
        UpdateStatus("Waiting for preview...");
        preview_timer_ = lv_timer_create(PreviewTimerCallback, 120, this);
    } else {
        const char* error = camera_ != nullptr ? camera_->last_error() : "Camera service is not available";
        UpdateStatus(error, true);
    }

    ESP_LOGI(TAG, "Camera app created, preview=%d", preview_started ? 1 : 0);
    return true;
}

void CameraApp::OnDestroy() {
    if (capture_guard_) {
        capture_guard_->generation.fetch_add(1);
        capture_guard_->app.store(nullptr);
    }

    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            if (preview_timer_ != nullptr) {
                lv_timer_delete(preview_timer_);
                preview_timer_ = nullptr;
            }
            if (root_ != nullptr && lv_obj_is_valid(root_)) {
                lv_obj_delete(root_);
            }
        }
    }

    if (camera_ != nullptr) {
        camera_->StopPreview();
    }
    ReleaseAudioResources();

    root_ = nullptr;
    preview_box_ = nullptr;
    preview_image_ = nullptr;
    placeholder_label_ = nullptr;
    status_label_ = nullptr;
    capture_button_ = nullptr;
    preview_pixels_.clear();
    displayed_sequence_ = 0;
    camera_ = nullptr;
    audio_focus_ = nullptr;
    context_ = nullptr;
    ui_ = nullptr;
}

void CameraApp::CapturePhoto() {
    if (camera_ == nullptr || capture_guard_ == nullptr) {
        UpdateStatus("Camera service is not available", true);
        return;
    }
    if (capture_guard_->running.exchange(true)) {
        return;
    }

    const uint32_t generation = capture_guard_->generation.fetch_add(1) + 1;
    auto* payload = new CameraCapturePayload();
    payload->guard = capture_guard_;
    payload->camera = camera_;
    payload->generation = generation;

    UpdateStatus("Saving photo...");
    if (capture_button_ != nullptr) {
        lv_obj_add_state(capture_button_, LV_STATE_DISABLED);
    }
    const BaseType_t ret =
        xTaskCreate(CaptureTask, "camera_capture", kCaptureTaskStackBytes, payload, 3, nullptr);
    if (ret != pdPASS) {
        capture_guard_->running.store(false);
        delete payload;
        if (capture_button_ != nullptr) {
            lv_obj_clear_state(capture_button_, LV_STATE_DISABLED);
        }
        LogCaptureTaskCreateFailure();
        UpdateStatus("Failed to start capture task", true);
    }
}

void CameraApp::OnCaptureComplete(bool ok,
                                  const std::string& saved_path,
                                  const std::string& error,
                                  uint32_t generation) {
    if (!capture_guard_ || generation != capture_guard_->generation.load()) {
        return;
    }
    capture_guard_->running.store(false);
    if (capture_button_ != nullptr) {
        lv_obj_clear_state(capture_button_, LV_STATE_DISABLED);
    }

    if (ok) {
        UpdateStatus(saved_path.c_str());
        if (ui_ != nullptr) {
            char toast[96];
            std::snprintf(toast, sizeof(toast), "Saved %s", saved_path.c_str());
            ui_->ShowToastUnlocked(toast);
        }
    } else {
        UpdateStatus(error.empty() ? "Capture failed" : error.c_str(), true);
        if (ui_ != nullptr) {
            ui_->ShowToastUnlocked("Capture failed");
        }
    }
}

void CameraApp::PreviewTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<CameraApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->UpdatePreview();
    }
}

void CameraApp::UpdatePreview() {
    if (camera_ == nullptr || preview_image_ == nullptr) {
        return;
    }

    rodakos::CameraFrame frame;
    if (!camera_->GetLatestFrame(frame) || frame.sequence == displayed_sequence_) {
        return;
    }
    if (frame.width <= 0 || frame.height <= 0 || frame.stride <= 0 || frame.rgb565.empty()) {
        return;
    }

    preview_pixels_ = std::move(frame.rgb565);
    displayed_sequence_ = frame.sequence;

    std::memset(&preview_dsc_, 0, sizeof(preview_dsc_));
    preview_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    preview_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    preview_dsc_.header.w = frame.width;
    preview_dsc_.header.h = frame.height;
    preview_dsc_.header.stride = frame.stride;
    preview_dsc_.data = preview_pixels_.data();
    preview_dsc_.data_size = preview_pixels_.size();

    lv_image_set_src(preview_image_, nullptr);
    lv_image_set_src(preview_image_, &preview_dsc_);

    const int32_t scale_w = (kPreviewBoxWidth * LV_SCALE_NONE) / frame.width;
    const int32_t scale_h = (kPreviewBoxHeight * LV_SCALE_NONE) / frame.height;
    const int32_t scale = std::max<int32_t>(1, std::min(scale_w, scale_h));
    lv_image_set_scale(preview_image_, static_cast<uint32_t>(scale));
    lv_obj_center(preview_image_);

    if (placeholder_label_ != nullptr) {
        lv_obj_add_flag(placeholder_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (capture_button_ != nullptr && (!capture_guard_ || !capture_guard_->running.load())) {
        lv_obj_clear_state(capture_button_, LV_STATE_DISABLED);
    }
    if (displayed_sequence_ == frame.sequence && frame.sequence == 1) {
        UpdateStatus("Ready");
    }
}

void CameraApp::UpdateStatus(const char* text, bool error) {
    if (status_label_ == nullptr) {
        return;
    }
    lv_label_set_text(status_label_, text != nullptr ? text : "");
    lv_obj_set_style_text_color(status_label_,
                                error ? rodakos_theme_error() : rodakos_theme_text_secondary(),
                                0);
}

void CameraApp::RequestAudioResources() {
    if (audio_focus_ == nullptr || audio_focus_token_ != 0) {
        return;
    }

    rodakos::AudioFocusRequest request;
    request.owner = "camera";
    request.gain = rodakos::AudioFocusGain::kExclusive;
    request.resume_on_release = false;
    request.release_playback_hardware = true;
    if (!audio_focus_->RequestFocus(request, audio_focus_token_)) {
        audio_focus_token_ = 0;
        ESP_LOGW(TAG, "Camera could not acquire exclusive audio focus");
    }
}

void CameraApp::ReleaseAudioResources() {
    if (audio_focus_ == nullptr || audio_focus_token_ == 0) {
        return;
    }
    audio_focus_->ReleaseFocus(audio_focus_token_);
    audio_focus_token_ = 0;
}

void CameraApp::NavigateBack() {
    NavigateHome();
}

void CameraApp::NavigateHome() {
    lv_async_call(DeferReturnHome, context_);
}

void RegisterCameraApp(PhoneAppRegistry& registry) {
    PhoneAppDescriptor desc;
    desc.id = "camera";
    desc.title = "Camera";
    desc.icon = FONT_AWESOME_CAMERA;
    desc.category = PhoneAppCategory::kMedia;
    desc.capabilities = PhoneCapability::kCamera | PhoneCapability::kStorage;
    desc.show_on_home = true;
    desc.aliases = {"photo", "capture", "相机", "拍照"};
    desc.create = []() -> std::unique_ptr<PhoneApp> {
        return std::make_unique<CameraApp>();
    };
    registry.Register(desc);
}
