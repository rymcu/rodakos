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
#include <cinttypes>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <optional>
#include <utility>

#include <esp_heap_caps.h>
#include <esp_log.h>

struct CameraCaptureResult {
    bool ok = false;
    std::string saved_path;
    std::string error;
    uint64_t generation = 0;
};

struct CameraCaptureGuard {
    std::mutex mutex;
    bool revoked = false;
    bool running = false;
    uint64_t generation = 0;
    std::optional<CameraCaptureResult> result;
};

namespace {
constexpr const char* TAG = "CameraApp";
// Keep only a small edge margin; the 4:3 sensor image is height-limited by
// the 240px display once the app header is reserved.
constexpr lv_coord_t kPreviewBoxWidth = 312;
constexpr lv_coord_t kPreviewBoxHeight = 184;
constexpr lv_coord_t kCaptureButtonSize = 54;
constexpr uint32_t kCaptureTaskStackBytes = 4096;
constexpr uint32_t kPreviewStartDelayMs = 30;
constexpr size_t kMinimumCameraDmaHeadroom = 4096;
constexpr size_t kHomeReturnReserveSizes[] = {12288, 10240, 8192};

struct CameraCapturePayload {
    std::shared_ptr<CameraCaptureGuard> guard;
    rodakos::CameraService* camera = nullptr;
    uint64_t generation = 0;
};

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

void CaptureTask(void* arg) {
    // RTOS task deletion does not unwind the stack; release the payload first.
    {
        std::unique_ptr<CameraCapturePayload> payload(static_cast<CameraCapturePayload*>(arg));
        if (payload != nullptr && payload->guard != nullptr) {
            CameraCaptureResult result;
            result.generation = payload->generation;
            if (payload->camera != nullptr) {
                result.ok = payload->camera->CapturePhoto(result.saved_path);
                if (!result.ok) {
                    result.error = payload->camera->last_error();
                }
            } else {
                result.error = "Camera service is not available";
            }
            std::lock_guard<std::mutex> lock(payload->guard->mutex);
            if (!payload->guard->revoked && payload->guard->running &&
                payload->guard->generation == result.generation) {
                // The UI polls this owned result; completion needs no LVGL allocation or lock.
                payload->guard->result = std::move(result);
            }
        }
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

bool IsCameraMemoryFailure(const std::string& error) {
    return error == "Camera OOM" || error.find("Not enough space") != std::string::npos;
}

}  // namespace

bool CameraApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    camera_ = context.services().camera();
    audio_focus_ = context.services().audio_focus();
    capture_guard_ = std::make_shared<CameraCaptureGuard>();
    preview_ready_ = false;

    if (!CreateUi()) {
        if (camera_ != nullptr) {
            camera_->StopPreview();
        }
        ReleaseAudioResources();
        return false;
    }

    ESP_LOGI(TAG, "Camera app created; preview startup deferred");
    return true;
}

void LogPauseResources(const char* phase) {
    ESP_LOGI(TAG,
             "Pause resources: phase=%s internal_free=%u internal_largest=%u "
             "dma_free=%u dma_largest=%u",
             phase,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)));
}

bool CameraApp::CreateUi(int lock_timeout_ms) {
    if (ui_ == nullptr) {
        return false;
    }

    PhoneUiLock lock(*ui_, lock_timeout_ms);
    if (!lock.locked()) {
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
    lv_obj_align(preview_box_, LV_ALIGN_TOP_MID, 0, kRodakosAppHeaderHeight + 4);
    lv_obj_set_style_bg_color(preview_box_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(preview_box_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(preview_box_, 8, 0);
    lv_obj_set_style_clip_corner(preview_box_, true, 0);
    lv_obj_clear_flag(preview_box_, LV_OBJ_FLAG_SCROLLABLE);

    preview_image_ = lv_image_create(preview_box_);
    lv_obj_center(preview_image_);

    placeholder_label_ = lv_label_create(preview_box_);
    lv_label_set_text(placeholder_label_, "Starting preview...");
    lv_obj_set_width(placeholder_label_, kPreviewBoxWidth - 28);
    lv_label_set_long_mode(placeholder_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(placeholder_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(placeholder_label_, rodakos_theme_text_secondary(), 0);
    lv_obj_set_style_text_font(placeholder_label_, &phone_font_14, 0);
    lv_obj_center(placeholder_label_);

    status_label_ = lv_label_create(root_);
    lv_obj_set_size(status_label_, 320, 20);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_bg_color(status_label_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(status_label_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(status_label_, 10, 0);
    lv_obj_set_style_pad_ver(status_label_, 2, 0);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(status_label_, &phone_font_12, 0);
    lv_obj_align(status_label_, LV_ALIGN_BOTTOM_MID, 0, 0);

    capture_button_ = CreateCaptureButton(root_);
    lv_obj_align(capture_button_, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_add_event_cb(capture_button_, [](lv_event_t* e) {
        auto* self = static_cast<CameraApp*>(lv_event_get_user_data(e));
        self->CapturePhoto();
    }, LV_EVENT_CLICKED, this);
    lv_obj_add_state(capture_button_, LV_STATE_DISABLED);

    UpdateStatus("Starting camera...");
    capture_result_timer_ = lv_timer_create(CaptureResultTimerCallback, 120, this);
    if (capture_result_timer_ == nullptr) {
        UpdateStatus("Failed to monitor photo results", true);
        return false;
    }
    preview_start_timer_ = lv_timer_create(PreviewStartTimerCallback, kPreviewStartDelayMs, this);
    if (preview_start_timer_ == nullptr) {
        UpdateStatus("Failed to schedule camera startup", true);
        return false;
    }
    lv_timer_set_repeat_count(preview_start_timer_, 1);

    return true;
}

void CameraApp::OnResume() {
    if (!preview_paused_for_transition_) {
        return;
    }
    if (!CreateUi(0)) {
        ESP_LOGE(TAG, "Failed to restore Camera UI after replacement rollback");
        std::abort();
    }
    preview_paused_for_transition_ = false;
}

void CameraApp::OnPause() {
    const bool ui_active = root_ != nullptr || capture_result_timer_ != nullptr ||
                           preview_start_timer_ != nullptr || preview_timer_ != nullptr;
    ESP_LOGI(TAG, "Pause: UI cleanup begin");
    DestroyUi();
    ESP_LOGI(TAG, "Pause: UI cleanup complete");
    LogPauseResources("ui-released");
    if (camera_ != nullptr) {
        camera_->StopPreview();
    }
    ESP_LOGI(TAG, "Pause: preview stop complete");
    LogPauseResources("preview-stopped");
    ReleaseHomeReturnMemory();
    LogPauseResources("home-reserve-released");
    ReleaseAudioResources();
    ESP_LOGI(TAG, "Pause: audio release complete");
    LogPauseResources("audio-released");
    preview_paused_for_transition_ = ui_active;
}

void CameraApp::OnDestroy() {
    ESP_LOGI(TAG, "Destroy: capture guard begin");
    if (capture_guard_) {
        std::lock_guard<std::mutex> lock(capture_guard_->mutex);
        capture_guard_->revoked = true;
        ++capture_guard_->generation;
        capture_guard_->result.reset();
        capture_guard_->running = false;
    }
    ESP_LOGI(TAG, "Destroy: capture guard complete");

    ESP_LOGI(TAG, "Destroy: UI cleanup begin");
    DestroyUi();
    ESP_LOGI(TAG, "Destroy: UI cleanup complete");

    ESP_LOGI(TAG, "Destroy: preview stop begin");
    if (camera_ != nullptr) {
        camera_->StopPreview();
    }
    ESP_LOGI(TAG, "Destroy: preview stop complete");
    ReleaseHomeReturnMemory();
    ESP_LOGI(TAG, "Destroy: audio release begin");
    ReleaseAudioResources();
    ESP_LOGI(TAG, "Destroy: audio release complete");

    preview_paused_for_transition_ = false;
    capture_guard_.reset();
    camera_ = nullptr;
    audio_focus_ = nullptr;
    context_ = nullptr;
    ui_ = nullptr;
}

void CameraApp::DestroyUi() {
    if (ui_ != nullptr) {
        // Timer callbacks own `this` until removed. A teardown timeout cannot abandon them.
        PhoneUiLock lock(*ui_, 0);
        if (lock.locked()) {
            if (capture_result_timer_ != nullptr) {
                lv_timer_delete(capture_result_timer_);
                capture_result_timer_ = nullptr;
            }
            if (preview_start_timer_ != nullptr) {
                lv_timer_delete(preview_start_timer_);
                preview_start_timer_ = nullptr;
            }
            if (preview_timer_ != nullptr) {
                lv_timer_delete(preview_timer_);
                preview_timer_ = nullptr;
            }
            if (root_ != nullptr && lv_obj_is_valid(root_)) {
                lv_obj_delete(root_);
            }
        }
    }
    root_ = nullptr;
    preview_box_ = nullptr;
    preview_image_ = nullptr;
    placeholder_label_ = nullptr;
    status_label_ = nullptr;
    capture_button_ = nullptr;
    std::vector<uint8_t>().swap(preview_pixels_);
    std::memset(&preview_dsc_, 0, sizeof(preview_dsc_));
    displayed_sequence_ = 0;
    preview_ready_ = false;
}

void CameraApp::PreviewStartTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<CameraApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->preview_start_timer_ = nullptr;
        self->StartPreview();
    }
}

void CameraApp::StartPreview() {
    ReserveHomeReturnMemory();
    const size_t internal_dma_largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (home_return_reserve_ != nullptr && internal_dma_largest < kMinimumCameraDmaHeadroom) {
        ESP_LOGW(TAG,
                 "Releasing Home return reserve for Camera DMA headroom: largest=%u required=%u",
                 static_cast<unsigned>(internal_dma_largest),
                 static_cast<unsigned>(kMinimumCameraDmaHeadroom));
        ReleaseHomeReturnMemory();
    }
    RequestAudioResources();
    bool preview_started = camera_ != nullptr && camera_->StartPreview();
    std::string error = camera_ != nullptr
                            ? camera_->last_error()
                            : "Camera service is not available";
    if (!preview_started && camera_ != nullptr && home_return_reserve_ != nullptr &&
        IsCameraMemoryFailure(error)) {
        ESP_LOGW(TAG,
                 "Retrying Camera after releasing Home return reserve: error=%s",
                 error.c_str());
        ReleaseHomeReturnMemory();
        preview_started = camera_->StartPreview();
        error = camera_->last_error();
    }
    if (!preview_started) {
        UpdateStatus(error.c_str(), true);
        if (placeholder_label_ != nullptr) {
            lv_label_set_text(placeholder_label_, "Camera unavailable");
        }
        ReleaseHomeReturnMemory();
        ReleaseAudioResources();
        return;
    }

    UpdateStatus("Waiting for preview...");
    preview_timer_ = lv_timer_create(PreviewTimerCallback, 120, this);
    if (preview_timer_ == nullptr) {
        camera_->StopPreview();
        ReleaseHomeReturnMemory();
        ReleaseAudioResources();
        UpdateStatus("Failed to monitor camera preview", true);
    }
}

void CameraApp::ReserveHomeReturnMemory() {
    if (home_return_reserve_ != nullptr) {
        return;
    }
    for (size_t size : kHomeReturnReserveSizes) {
        home_return_reserve_ = heap_caps_calloc(
            1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (home_return_reserve_ != nullptr) {
            home_return_reserve_size_ = size;
            ESP_LOGI(TAG, "Reserved %u bytes for Home return",
                     static_cast<unsigned>(size));
            return;
        }
    }
    ESP_LOGW(TAG, "Unable to reserve contiguous memory for Home return");
}

void CameraApp::ReleaseHomeReturnMemory() {
    if (home_return_reserve_ == nullptr) {
        return;
    }
    heap_caps_free(home_return_reserve_);
    ESP_LOGI(TAG, "Released %u-byte Home return reserve",
             static_cast<unsigned>(home_return_reserve_size_));
    home_return_reserve_ = nullptr;
    home_return_reserve_size_ = 0;
}

void CameraApp::CapturePhoto() {
    if (camera_ == nullptr || capture_guard_ == nullptr) {
        UpdateStatus("Camera service is not available", true);
        return;
    }
    if (!preview_ready_) {
        UpdateStatus("Camera preview is not ready", true);
        return;
    }
    auto* payload = new (std::nothrow) CameraCapturePayload();
    if (payload == nullptr) {
        UpdateStatus("No memory to save photo", true);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(capture_guard_->mutex);
        if (capture_guard_->revoked || capture_guard_->running) {
            delete payload;
            return;
        }
        capture_guard_->running = true;
        capture_guard_->result.reset();
        payload->generation = ++capture_guard_->generation;
    }
    payload->guard = capture_guard_;
    payload->camera = camera_;

    UpdateStatus("Saving photo...");
    if (capture_button_ != nullptr) {
        lv_obj_add_state(capture_button_, LV_STATE_DISABLED);
    }
    const BaseType_t ret =
        xTaskCreate(CaptureTask, "camera_capture", kCaptureTaskStackBytes, payload, 3, nullptr);
    if (ret != pdPASS) {
        {
            std::lock_guard<std::mutex> lock(capture_guard_->mutex);
            capture_guard_->running = false;
        }
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
                                  uint64_t generation) {
    if (!capture_guard_) return;
    {
        std::lock_guard<std::mutex> lock(capture_guard_->mutex);
        if (capture_guard_->revoked || !capture_guard_->running ||
            generation != capture_guard_->generation) {
            return;
        }
        capture_guard_->running = false;
    }
    if (capture_button_ != nullptr && preview_ready_) {
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

bool CameraApp::CaptureInFlight() const {
    if (!capture_guard_) return false;
    std::lock_guard<std::mutex> lock(capture_guard_->mutex);
    return capture_guard_->running;
}

void CameraApp::CaptureResultTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<CameraApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) self->ConsumeCaptureResult();
}

void CameraApp::ConsumeCaptureResult() {
    if (!capture_guard_) return;
    std::optional<CameraCaptureResult> result;
    {
        std::lock_guard<std::mutex> lock(capture_guard_->mutex);
        if (capture_guard_->revoked) return;
        result = std::move(capture_guard_->result);
        capture_guard_->result.reset();
    }
    if (result) {
        OnCaptureComplete(result->ok, result->saved_path, result->error, result->generation);
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
    if (!camera_->GetLatestFrame(frame)) {
        const auto state = camera_->GetState();
        if (!state.preview_running && !state.last_error.empty()) {
            preview_ready_ = false;
            if (capture_button_ != nullptr) lv_obj_add_state(capture_button_, LV_STATE_DISABLED);
            UpdateStatus(state.last_error.c_str(), true);
            if (placeholder_label_ != nullptr) {
                lv_label_set_text(placeholder_label_, "Camera unavailable");
            }
            if (preview_timer_ != nullptr) {
                lv_timer_delete(preview_timer_);
                preview_timer_ = nullptr;
            }
            ReleaseAudioResources();
        }
        return;
    }
    if (frame.sequence == displayed_sequence_) {
        return;
    }
    if (frame.width <= 0 || frame.height <= 0 || frame.stride <= 0 || frame.rgb565.empty()) {
        return;
    }

    const bool first_displayed_frame = displayed_sequence_ == 0;
    preview_ready_ = true;
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
    // Fill the widescreen preview box. The sensor is 4:3, so this crops a
    // small amount from the top and bottom instead of leaving wide side bars.
    const int32_t scale = std::max<int32_t>(1, std::max(scale_w, scale_h));
    lv_image_set_scale(preview_image_, static_cast<uint32_t>(scale));
    lv_obj_center(preview_image_);

    if (placeholder_label_ != nullptr) {
        lv_obj_add_flag(placeholder_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (capture_button_ != nullptr && !CaptureInFlight()) {
        lv_obj_clear_state(capture_button_, LV_STATE_DISABLED);
    }
    if (first_displayed_frame) {
        ESP_LOGI(TAG, "Camera preview image updated: sequence=%" PRIu32, frame.sequence);
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
    if (context_ != nullptr && !context_->navigation().RequestHome()) {
        ESP_LOGW(TAG, "Home navigation queue is unavailable or full");
        if (ui_ != nullptr) ui_->ShowToast("返回桌面失败，请重试");
    }
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
