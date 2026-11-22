#pragma once

#include "phone_os/phone_app.h"
#include "phone_os/camera_service.h"
#include "phone_os/audio_focus_service.h"
#include "phone_os/camera_calibration_service.h"

#include <memory>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

struct CameraCaptureGuard;

class CameraApp : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override;
    void OnPause() override;
    void OnDestroy() override;
    // Closes the calibration page first; otherwise leaves the app.
    bool OnBackRequested() override;

    void CapturePhoto();

private:
    static void PreviewStartTimerCallback(lv_timer_t* timer);
    static void PreviewTimerCallback(lv_timer_t* timer);
    static void CaptureResultTimerCallback(lv_timer_t* timer);

    bool CreateUi(int lock_timeout_ms = 1000);
    void DestroyUi();
    void StartPreview();
    void ReserveHomeReturnMemory();
    void ReleaseHomeReturnMemory();
    void UpdatePreview();
    void ConsumeCaptureResult();
    bool CaptureInFlight() const;
    void OnCaptureComplete(bool ok, const std::string& saved_path,
                           const std::string& error, uint64_t generation);
    // Shows a short message in a pill at the bottom-left of the viewfinder. hide_after_ms 0 keeps it.
    void UpdateStatus(const char* text, bool error = false, uint32_t hide_after_ms = 0);
    void HideStatus();
    void ShowPreviewNotice(const char* title, const char* detail, bool retry);
    void HidePreviewNotice();
    void SetControlsEnabled(bool enabled);
    void RetryPreview();
    void RequestAudioResources();
    void ReleaseAudioResources();
    void NavigateBack();
    void NavigateHome();
    void OpenCalibration();
    void CloseCalibration();
    void UpdateCalibration();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::CameraService* camera_ = nullptr;
    rodakos::AudioFocusService* audio_focus_ = nullptr;
    uint32_t audio_focus_token_ = 0;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* preview_box_ = nullptr;
    lv_obj_t* preview_image_ = nullptr;
    lv_obj_t* placeholder_label_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* capture_button_ = nullptr;
    lv_obj_t* rail_ = nullptr;
    lv_obj_t* notice_ = nullptr;
    lv_obj_t* notice_title_ = nullptr;
    lv_obj_t* notice_detail_ = nullptr;
    lv_obj_t* retry_button_ = nullptr;
    uint32_t status_shown_at_ = 0;
    uint32_t status_hide_after_ms_ = 0;
    lv_timer_t* preview_start_timer_ = nullptr;
    lv_timer_t* preview_timer_ = nullptr;
    lv_timer_t* capture_result_timer_ = nullptr;
    lv_image_dsc_t preview_dsc_ = {};
    std::vector<uint8_t> preview_pixels_;
    uint32_t displayed_sequence_ = 0;
    bool preview_ready_ = false;
    bool preview_paused_for_transition_ = false;
    void* home_return_reserve_ = nullptr;
    size_t home_return_reserve_size_ = 0;
    std::shared_ptr<CameraCaptureGuard> capture_guard_;
    // Calibration page: drawn over the preview box, worker created on first open.
    std::unique_ptr<rodakos::CameraCalibrationService> calibration_;
    lv_obj_t* calibration_button_ = nullptr;
    lv_obj_t* calibration_layer_ = nullptr;
    lv_obj_t* calibration_center_box_ = nullptr;
    lv_obj_t* calibration_qr_box_ = nullptr;
    lv_obj_t* calibration_region_box_ = nullptr;
    lv_obj_t* calibration_panel_ = nullptr;
    lv_obj_t* calibration_status_ = nullptr;
    lv_obj_t* calibration_score_ = nullptr;
    lv_obj_t* calibration_bar_ = nullptr;
    lv_obj_t* calibration_best_mark_ = nullptr;
    lv_obj_t* calibration_hint_ = nullptr;
    lv_timer_t* calibration_timer_ = nullptr;
    uint32_t calibration_revision_ = 0;
};

void RegisterCameraApp(PhoneAppRegistry& registry);
