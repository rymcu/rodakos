#include "apps/camera/camera_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"
#include "rodakos_adapters/file_service.h"

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdlib>
#include <mutex>
#include <new>
#include <optional>
#include <utility>

#include <esp_heap_caps.h>
#include <esp_log.h>

struct CameraCaptureResult {
    rodakos::CameraCaptureOutcome outcome{
        false, rodakos::CameraCaptureFailureStage::kServiceUnavailable, 0};
    std::string saved_path;
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
// Layout follows the approved 320x240 design: the viewfinder fills the left 266x200 below the
// header and shows the whole 4:3 frame; a 54 px rail on the right holds the shutter.
constexpr lv_coord_t kPreviewBoxWidth = 266;
constexpr lv_coord_t kPreviewBoxHeight = 200;
constexpr lv_coord_t kRailWidth = 320 - kPreviewBoxWidth;
constexpr lv_coord_t kCaptureButtonSize = 44;
constexpr lv_coord_t kShutterDot = 30;
constexpr uint32_t kSavedToastMs = 2000;
constexpr uint32_t kErrorToastMs = 4000;
constexpr uint32_t kCaptureTaskStackBytes = 4096;
constexpr uint32_t kPreviewStartDelayMs = 30;
constexpr size_t kMinimumCameraDmaHeadroom = 4096;
constexpr size_t kHomeReturnReserveSizes[] = {12288, 10240, 8192};

struct CameraCapturePayload {
    std::shared_ptr<CameraCaptureGuard> guard;
    rodakos::CameraService* camera = nullptr;
    uint64_t generation = 0;
};

// A classic shutter: a 3 px ring around a solid dot, in the primary text color.
lv_obj_t* CreateCaptureButton(lv_obj_t* parent) {
    auto* button = lv_btn_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, kCaptureButtonSize, kCaptureButtonSize);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(button, 3, 0);
    lv_obj_set_style_border_color(button, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_ext_click_area(button, 6);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    auto* dot = lv_obj_create(button);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, kShutterDot, kShutterDot);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_scale(dot, 220, LV_STATE_PRESSED);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(dot);
    return button;
}

lv_obj_t* CreateGlyph(lv_obj_t* parent, const char* icon, lv_color_t color, const lv_font_t* font) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

const char* PreviewErrorText(const std::string& error) {
    if (error == "Camera OOM" || error.find("Not enough space") != std::string::npos) return "内存不足，请重试";
    if (error == "Camera service is not available") return "相机服务不可用";
    return "请重试";
}

const char* CaptureErrorText(const rodakos::CameraCaptureOutcome& outcome) {
    using Stage = rodakos::CameraCaptureFailureStage;
    if (outcome.native_error == ENOMEM || outcome.stage == Stage::kJpegInputAlloc ||
        outcome.stage == Stage::kJpegOutputAlloc || outcome.stage == Stage::kJpegCompactAlloc) {
        return "保存失败，内存不足";
    }
    if (outcome.native_error == ENOSPC) return "保存失败，SD 卡空间不足";
    if (outcome.stage == Stage::kStorageUnavailable || outcome.native_error == ENODEV ||
        outcome.native_error == ENXIO) {
        return "保存失败，SD 卡不可用";
    }
    switch (outcome.stage) {
        case Stage::kDirectoryFailed:
        case Stage::kPathSelectionFailed:
        case Stage::kWriteFailed:
            return "保存失败，SD 卡写入失败";
        default:
            return "保存失败，请重试";
    }
}

void CaptureTask(void* arg) {
    // RTOS task deletion does not unwind the stack; release the payload first.
    {
        std::unique_ptr<CameraCapturePayload> payload(static_cast<CameraCapturePayload*>(arg));
        if (payload != nullptr && payload->guard != nullptr) {
            CameraCaptureResult result;
            result.generation = payload->generation;
            if (payload->camera != nullptr) {
                result.outcome = payload->camera->CapturePhotoWithOutcome(result.saved_path);
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

    CreateAppHeader(root_, "相机", [](lv_event_t* e) {
        static_cast<CameraApp*>(lv_event_get_user_data(e))->NavigateBack();
    }, [](lv_event_t* e) {
        auto* self = static_cast<CameraApp*>(lv_event_get_user_data(e));
        if (self->calibration_layer_ != nullptr) self->CloseCalibration();
        else self->OpenCalibration();
    }, this, nullptr, ICON_SETTINGS, &calibration_button_);
    // Checked while the calibration page is open.
    lv_obj_set_style_bg_color(calibration_button_, rodakos_theme_primary(), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(lv_obj_get_child(calibration_button_, 0), rodakos_theme_bg_primary(),
                                LV_STATE_CHECKED);
    lv_obj_set_style_opa(calibration_button_, LV_OPA_40, LV_STATE_DISABLED);

    preview_box_ = lv_obj_create(root_);
    lv_obj_remove_style_all(preview_box_);
    lv_obj_set_size(preview_box_, kPreviewBoxWidth, kPreviewBoxHeight);
    lv_obj_set_pos(preview_box_, 0, kRodakosAppHeaderHeight);
    lv_obj_set_style_bg_color(preview_box_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(preview_box_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(preview_box_, LV_OBJ_FLAG_SCROLLABLE);

    preview_image_ = lv_image_create(preview_box_);
    lv_obj_center(preview_image_);

    placeholder_label_ = lv_label_create(preview_box_);
    lv_label_set_text(placeholder_label_, "正在启动相机");
    lv_obj_set_width(placeholder_label_, kPreviewBoxWidth - 24);
    lv_label_set_long_mode(placeholder_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(placeholder_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(placeholder_label_, rodakos_theme_text_secondary(), 0);
    lv_obj_set_style_text_font(placeholder_label_, &phone_font_14, 0);
    lv_obj_center(placeholder_label_);

    // The result pill sits inside the viewfinder, bottom-left, over the live image.
    status_label_ = lv_label_create(preview_box_);
    // Messages are short fixed phrases; clip rather than dot so the label keeps its own text.
    lv_obj_set_height(status_label_, 32);
    lv_obj_set_style_max_width(status_label_, kPreviewBoxWidth - 20, 0);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_bg_color(status_label_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(status_label_, LV_OPA_60, 0);
    lv_obj_set_style_radius(status_label_, 16, 0);
    lv_obj_set_style_pad_hor(status_label_, 12, 0);
    lv_obj_set_style_pad_top(status_label_, 7, 0);
    lv_obj_set_style_text_font(status_label_, &phone_font_14, 0);
    lv_obj_align(status_label_, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

    rail_ = lv_obj_create(root_);
    lv_obj_remove_style_all(rail_);
    lv_obj_set_size(rail_, kRailWidth, kPreviewBoxHeight);
    lv_obj_set_pos(rail_, kPreviewBoxWidth, kRodakosAppHeaderHeight);
    lv_obj_set_style_bg_color(rail_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(rail_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(rail_, LV_OBJ_FLAG_SCROLLABLE);

    capture_button_ = CreateCaptureButton(rail_);
    lv_obj_center(capture_button_);
    lv_obj_add_event_cb(capture_button_, [](lv_event_t* e) {
        auto* self = static_cast<CameraApp*>(lv_event_get_user_data(e));
        self->CapturePhoto();
    }, LV_EVENT_CLICKED, this);
    SetControlsEnabled(false);

    capture_result_timer_ = lv_timer_create(CaptureResultTimerCallback, 120, this);
    if (capture_result_timer_ == nullptr) {
        return false;
    }
    preview_start_timer_ = lv_timer_create(PreviewStartTimerCallback, kPreviewStartDelayMs, this);
    if (preview_start_timer_ == nullptr) {
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
    // Joins the calibration worker before the camera service pointer goes away.
    calibration_.reset();
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
            CloseCalibration();
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
    calibration_button_ = nullptr;
    rail_ = nullptr;
    notice_ = notice_title_ = notice_detail_ = retry_button_ = nullptr;
    status_hide_after_ms_ = 0;
    if (calibration_) calibration_->Stop();
    preview_frame_ = {};
    preview_dsc_ = {};
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
    // Mount the SD card before the camera stream takes internal DMA memory. Mounting it later, at
    // the first capture, fails allocating the SDMMC descriptors, and IDF 6.0.2 then keeps the host
    // controller claimed, so no app can mount the card again until reboot.
    if (context_ != nullptr) {
        auto* files = context_->services().file_service();
        if (files != nullptr && !files->IsMounted() && !files->Init()) {
            ESP_LOGW(TAG, "SD card not mounted before preview; photos cannot be saved");
        }
    }
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
        ESP_LOGW(TAG, "Camera preview failed to start: %s", error.c_str());
        ShowPreviewNotice("相机没有画面", PreviewErrorText(error), true);
        ReleaseHomeReturnMemory();
        ReleaseAudioResources();
        return;
    }

    HidePreviewNotice();
    if (placeholder_label_ != nullptr) {
        lv_label_set_text(placeholder_label_, "正在等待画面");
        lv_obj_clear_flag(placeholder_label_, LV_OBJ_FLAG_HIDDEN);
    }
    preview_timer_ = lv_timer_create(PreviewTimerCallback, 120, this);
    if (preview_timer_ == nullptr) {
        camera_->StopPreview();
        ReleaseHomeReturnMemory();
        ReleaseAudioResources();
        ShowPreviewNotice("相机没有画面", "请重试", true);
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
    if (camera_ == nullptr || capture_guard_ == nullptr || !preview_ready_) {
        return;
    }
    auto* payload = new (std::nothrow) CameraCapturePayload();
    if (payload == nullptr) {
        UpdateStatus("保存失败，内存不足", true, kErrorToastMs);
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

    UpdateStatus("正在保存…");
    if (capture_button_ != nullptr) {
        lv_obj_add_state(capture_button_, LV_STATE_DISABLED);
    }
    if (calibration_button_ != nullptr) lv_obj_add_state(calibration_button_, LV_STATE_DISABLED);
    const BaseType_t ret =
        xTaskCreate(CaptureTask, "camera_capture", kCaptureTaskStackBytes, payload, 3, nullptr);
    if (ret != pdPASS) {
        {
            std::lock_guard<std::mutex> lock(capture_guard_->mutex);
            capture_guard_->running = false;
        }
        delete payload;
        SetControlsEnabled(preview_ready_);
        LogCaptureTaskCreateFailure();
        UpdateStatus("保存失败，请重试", true, kErrorToastMs);
    }
}

void CameraApp::OnCaptureComplete(const rodakos::CameraCaptureOutcome& outcome,
                                  const std::string& saved_path,
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
    SetControlsEnabled(preview_ready_);

    if (outcome.ok) {
        ESP_LOGI(TAG, "Photo saved: %s", saved_path.c_str());
        UpdateStatus(ICON_CHECK " 已保存到相册", false, kSavedToastMs);
    } else {
        ESP_LOGW(TAG, "Photo capture failed: stage=%s native_error=%d",
                 rodakos::CameraCaptureFailureStageName(outcome.stage), outcome.native_error);
        UpdateStatus(CaptureErrorText(outcome), true, kErrorToastMs);
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
        OnCaptureComplete(result->outcome, result->saved_path, result->generation);
    }
    // The result timer already runs every 120 ms, so it also retires the pill without a timer of its own.
    if (status_hide_after_ms_ != 0 && lv_tick_elaps(status_shown_at_) >= status_hide_after_ms_) {
        HideStatus();
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

    const auto copy_status = camera_->CopyLatestFrameIfNewer(displayed_sequence_, preview_frame_);
    if (copy_status == rodakos::FrameCopyStatus::kUnavailable) {
        const auto state = camera_->GetState();
        if (!state.preview_running && !state.last_error.empty()) {
            preview_ready_ = false;
            SetControlsEnabled(false);
            ESP_LOGW(TAG, "Camera preview stopped: %s", state.last_error.c_str());
            CloseCalibration();
            ShowPreviewNotice("相机没有画面", PreviewErrorText(state.last_error), true);
            if (preview_timer_ != nullptr) {
                lv_timer_delete(preview_timer_);
                preview_timer_ = nullptr;
            }
            ReleaseAudioResources();
        }
        return;
    }
    if (copy_status != rodakos::FrameCopyStatus::kUpdated) {
        return;
    }

    const auto& frame = preview_frame_;
    if (frame.width <= 0 || frame.height <= 0 || frame.stride <= 0 || frame.rgb565.empty()) {
        return;
    }

    const bool first_displayed_frame = !preview_ready_;
    preview_ready_ = true;
    displayed_sequence_ = frame.sequence;

    if (preview_dsc_.data != frame.rgb565.data() || preview_dsc_.header.w != frame.width ||
        preview_dsc_.header.h != frame.height || preview_dsc_.header.stride != frame.stride ||
        preview_dsc_.data_size != frame.rgb565.size() ||
        lv_image_get_src(preview_image_) != &preview_dsc_) {
        preview_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        preview_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        preview_dsc_.header.w = static_cast<uint16_t>(frame.width);
        preview_dsc_.header.h = static_cast<uint16_t>(frame.height);
        preview_dsc_.header.stride = static_cast<uint16_t>(frame.stride);
        preview_dsc_.data = frame.rgb565.data();
        preview_dsc_.data_size = static_cast<uint32_t>(frame.rgb565.size());
        lv_image_set_src(preview_image_, &preview_dsc_);
        UpdatePreviewLayout();
    }
    lv_obj_invalidate(preview_image_);

    if (first_displayed_frame) {
        if (placeholder_label_ != nullptr) lv_obj_add_flag(placeholder_label_, LV_OBJ_FLAG_HIDDEN);
        if (!CaptureInFlight()) SetControlsEnabled(true);
        ESP_LOGI(TAG, "Camera preview image updated: sequence=%" PRIu32, frame.sequence);
    }
}

void CameraApp::UpdatePreviewLayout() {
    const int width = preview_dsc_.header.w;
    const int height = preview_dsc_.header.h;
    if (!preview_ready_ || preview_image_ == nullptr || width == 0 || height == 0) return;

    const int32_t scale_w = (kPreviewBoxWidth * LV_SCALE_NONE) / width;
    const int32_t scale_h = (kPreviewBoxHeight * LV_SCALE_NONE) / height;
    // Show the whole 4:3 frame: the 266x200 viewfinder matches the sensor aspect, so nothing is
    // cropped. Calibration uses 0.7x left-aligned (kCalibrationScale in camera_calibration_page.cc)
    // so its result panel fits to the right.
    const bool calibrating = calibration_layer_ != nullptr;
    const int32_t scale = std::max<int32_t>(1, calibrating ? LV_SCALE_NONE * 7 / 10
                                                           : std::min(scale_w, scale_h));
    lv_image_set_scale(preview_image_, static_cast<uint32_t>(scale));
    if (calibrating) {
        // LVGL scales around the centre of the unscaled image; offset so the scaled image's
        // left edge sits at x=0 of the preview box.
        const lv_coord_t scaled_w = static_cast<lv_coord_t>(width * scale / LV_SCALE_NONE);
        lv_obj_align(preview_image_, LV_ALIGN_LEFT_MID, (scaled_w - width) / 2, 0);
    } else {
        lv_obj_center(preview_image_);
    }
}

void CameraApp::UpdateStatus(const char* text, bool error, uint32_t hide_after_ms) {
    if (status_label_ == nullptr) {
        return;
    }
    lv_label_set_text(status_label_, text != nullptr ? text : "");
    lv_obj_set_style_text_color(status_label_, error ? rodakos_theme_error() : lv_color_white(), 0);
    lv_obj_set_width(status_label_, LV_SIZE_CONTENT);
    lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(status_label_);
    status_shown_at_ = lv_tick_get();
    status_hide_after_ms_ = hide_after_ms;
}

void CameraApp::HideStatus() {
    status_hide_after_ms_ = 0;
    if (status_label_ != nullptr) lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
}

void CameraApp::SetControlsEnabled(bool enabled) {
    for (auto* control : {capture_button_, calibration_button_}) {
        if (control == nullptr) continue;
        if (enabled) lv_obj_clear_state(control, LV_STATE_DISABLED);
        else lv_obj_add_state(control, LV_STATE_DISABLED);
    }
}

void CameraApp::ShowPreviewNotice(const char* title, const char* detail, bool retry) {
    if (preview_box_ == nullptr) return;
    preview_ready_ = false;
    SetControlsEnabled(false);
    HideStatus();
    if (placeholder_label_ != nullptr) lv_obj_add_flag(placeholder_label_, LV_OBJ_FLAG_HIDDEN);
    if (preview_image_ != nullptr) lv_image_set_src(preview_image_, nullptr);
    if (notice_ == nullptr) {
        notice_ = lv_obj_create(preview_box_);
        lv_obj_remove_style_all(notice_);
        lv_obj_set_size(notice_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(notice_, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_opa(notice_, LV_OPA_COVER, 0);
        lv_obj_clear_flag(notice_, LV_OBJ_FLAG_SCROLLABLE);
        auto* icon = CreateGlyph(notice_, ICON_CAMERA, rodakos_theme_text_tertiary(), PhoneIconFontLarge());
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 30);
        notice_title_ = CreateGlyph(notice_, "", rodakos_theme_text_primary(), &phone_font_14);
        lv_obj_align(notice_title_, LV_ALIGN_TOP_MID, 0, 72);
        notice_detail_ = CreateGlyph(notice_, "", rodakos_theme_text_secondary(), &phone_font_14);
        lv_obj_align(notice_detail_, LV_ALIGN_TOP_MID, 0, 96);
        retry_button_ = lv_btn_create(notice_);
        lv_obj_remove_style_all(retry_button_);
        lv_obj_set_size(retry_button_, 120, 40);
        lv_obj_set_style_radius(retry_button_, 10, 0);
        lv_obj_set_style_bg_color(retry_button_, rodakos_theme_primary(), 0);
        lv_obj_set_style_bg_opa(retry_button_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(retry_button_, lv_color_darken(rodakos_theme_primary(), 40), LV_STATE_PRESSED);
        lv_obj_align(retry_button_, LV_ALIGN_TOP_MID, 0, 132);
        lv_obj_set_flex_flow(retry_button_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(retry_button_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(retry_button_, 6, 0);
        const lv_color_t on_primary = ui_ != nullptr ? ui_->theme().accent_text : lv_color_black();
        CreateGlyph(retry_button_, ICON_REFRESH, on_primary, PhoneIconFont());
        CreateGlyph(retry_button_, "重试", on_primary, &phone_font_14);
        lv_obj_add_event_cb(retry_button_, [](lv_event_t* e) {
            static_cast<CameraApp*>(lv_event_get_user_data(e))->RetryPreview();
        }, LV_EVENT_CLICKED, this);
    }
    lv_label_set_text(notice_title_, title);
    lv_label_set_text(notice_detail_, detail != nullptr ? detail : "");
    if (retry) lv_obj_clear_flag(retry_button_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(retry_button_, LV_OBJ_FLAG_HIDDEN);
}

void CameraApp::HidePreviewNotice() {
    if (notice_ == nullptr) return;
    lv_obj_delete(notice_);
    notice_ = notice_title_ = notice_detail_ = retry_button_ = nullptr;
}

void CameraApp::RetryPreview() {
    // Only after the failed attempt has fully cleaned up: no running preview timer.
    if (preview_timer_ != nullptr || preview_start_timer_ != nullptr) return;
    HidePreviewNotice();
    if (placeholder_label_ != nullptr) {
        lv_label_set_text(placeholder_label_, "正在启动相机");
        lv_obj_clear_flag(placeholder_label_, LV_OBJ_FLAG_HIDDEN);
    }
    displayed_sequence_ = 0;
    if (camera_ != nullptr) camera_->StopPreview();
    StartPreview();
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
    if (calibration_layer_ != nullptr) {
        CloseCalibration();
        return;
    }
    NavigateHome();
}

bool CameraApp::OnBackRequested() {
    if (calibration_layer_ == nullptr) return false;
    CloseCalibration();
    return true;
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
    desc.icon = ICON_CAMERA;
    desc.category = PhoneAppCategory::kMedia;
    desc.capabilities = PhoneCapability::kCamera | PhoneCapability::kStorage;
    desc.show_on_home = true;
    desc.aliases = {"photo", "capture", "相机", "拍照"};
    desc.create = []() -> std::unique_ptr<PhoneApp> {
        return std::make_unique<CameraApp>();
    };
    registry.Register(desc);
}
