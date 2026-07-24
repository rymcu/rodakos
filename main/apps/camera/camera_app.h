#pragma once

#include "phone_os/phone_app.h"
#include "phone_os/camera_service.h"
#include "phone_os/audio_focus_service.h"

#include <atomic>
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
    void OnResume() override {}
    void OnPause() override {}
    void OnDestroy() override;

    void CapturePhoto();
    void OnCaptureComplete(bool ok, const std::string& saved_path, const std::string& error, uint32_t generation);

private:
    static void PreviewTimerCallback(lv_timer_t* timer);

    void UpdatePreview();
    void UpdateStatus(const char* text, bool error = false);
    void RequestAudioResources();
    void ReleaseAudioResources();
    void NavigateBack();
    void NavigateHome();

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
    lv_timer_t* preview_timer_ = nullptr;
    lv_image_dsc_t preview_dsc_ = {};
    std::vector<uint8_t> preview_pixels_;
    uint32_t displayed_sequence_ = 0;
    std::shared_ptr<CameraCaptureGuard> capture_guard_;
};

void RegisterCameraApp(PhoneAppRegistry& registry);
