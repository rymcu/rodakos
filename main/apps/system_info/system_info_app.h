#pragma once

#include "phone_os/phone_app.h"
#include "rodakos_adapters/file_service.h"

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class AudioFocusService;
class AudioOutputService;
class AudioService;
class ButtonBindingService;
class CameraService;
class MotionService;
class MusicPlayerService;
class VoiceAssistantService;
class VoiceWakeService;
class WebFileSystemService;
}  // namespace rodakos

class SystemInfoApp final : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override {}
    void OnPause() override {}
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;

    void Refresh();

private:
    struct InfoLabels {
        lv_obj_t* value = nullptr;
        lv_obj_t* detail = nullptr;
    };

    void CreateUi();
    void DestroyUi();
    void ResetUiPointers();
    void BindServices(PhoneAppContext& context);
    InfoLabels CreateInfoCard(lv_obj_t* parent, const char* icon, const char* title);
    void RefreshWiFi();
    void RefreshMemory();
    void RefreshStorage();
    void RefreshRuntime();
    void RefreshHardware();
    void RefreshVoice();
    void RefreshFirmware();
    void ProbeStorage(bool allow_mount);
    void NavigateHome();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::FileService* file_service_ = nullptr;
    rodakos::AudioFocusService* audio_focus_service_ = nullptr;
    rodakos::AudioOutputService* audio_output_service_ = nullptr;
    rodakos::AudioService* audio_service_ = nullptr;
    rodakos::ButtonBindingService* button_service_ = nullptr;
    rodakos::CameraService* camera_service_ = nullptr;
    rodakos::MotionService* motion_service_ = nullptr;
    rodakos::MusicPlayerService* music_player_service_ = nullptr;
    rodakos::VoiceAssistantService* voice_assistant_service_ = nullptr;
    rodakos::VoiceWakeService* voice_wake_service_ = nullptr;
    rodakos::WebFileSystemService* web_files_service_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;

    InfoLabels wifi_;
    InfoLabels memory_;
    InfoLabels storage_;
    InfoLabels uptime_;
    InfoLabels firmware_;
    InfoLabels chip_;
    InfoLabels heap_detail_;
    InfoLabels runtime_;
    InfoLabels buses_;
    InfoLabels camera_;
    InfoLabels audio_;
    InfoLabels motion_;
    InfoLabels web_;
    InfoLabels voice_;
    InfoLabels buttons_;

    bool storage_checked_ = false;
    bool storage_mounted_ = false;
    rodakos::FileService::Capacity storage_capacity_;
};

void RegisterSystemInfoApp(PhoneAppRegistry& registry);
