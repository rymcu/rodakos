#pragma once

#include "phone_os/phone_app.h"

#include <cstdint>
#include <string>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class VoiceAssistantService;
class VoiceWakeService;
class DeviceCloudConfigService;
class VoiceStatusLightService;
}

// Status and settings page for the voice assistant (docs/voice-assistant.md). Not a Talk/Stop
// surface: wake is the only way to start a turn.
class AssistantApp final : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override;
    void OnPause() override;
    void OnDestroy() override;
    bool OnBackRequested() override;

    void RefreshState();

private:
    // What the status card shows; computed from the services, then drawn.
    enum class Tone { kOk, kActive, kIdle, kWarn, kError };
    enum class Fix { kNone, kWiFi, kCloud };
    struct View {
        const char* icon = nullptr;
        Tone tone = Tone::kOk;
        bool badge_filled = false;
        std::string title;
        std::string subtitle;
        std::string wake_note;
        const char* cloud_text = "已连接";
        Tone cloud_tone = Tone::kOk;
        Fix fix = Fix::kNone;
    };

    void CreateUi();
    void CreateDetail();
    void CloseDetail();
    View ComputeView() const;
    void ApplyView(const View& view);
    void RefreshDetail();
    void ToggleWakeListening();
    void ToggleStatusLight();
    void OpenFix();
    void NavigateHome();
    void OpenSettingsPage(const char* page);
    static void DeferOpenSettings(void* user_data);

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::VoiceAssistantService* assistant_ = nullptr;
    rodakos::VoiceWakeService* wake_ = nullptr;
    rodakos::DeviceCloudConfigService* cloud_ = nullptr;
    rodakos::VoiceStatusLightService* status_light_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
    lv_obj_t* badge_ = nullptr;
    lv_obj_t* badge_icon_ = nullptr;
    lv_obj_t* status_dot_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* subtitle_label_ = nullptr;
    lv_obj_t* fix_button_ = nullptr;
    lv_obj_t* fix_label_ = nullptr;
    lv_obj_t* wake_row_ = nullptr;
    lv_obj_t* wake_switch_ = nullptr;
    lv_obj_t* wake_note_label_ = nullptr;
    lv_obj_t* light_row_ = nullptr;
    lv_obj_t* light_switch_ = nullptr;
    lv_obj_t* light_note_label_ = nullptr;
    lv_obj_t* cloud_row_ = nullptr;
    lv_obj_t* cloud_dot_ = nullptr;
    lv_obj_t* cloud_label_ = nullptr;

    lv_obj_t* detail_ = nullptr;
    lv_obj_t* detail_values_[5] = {};
    lv_obj_t* detail_raw_label_ = nullptr;

    Fix fix_ = Fix::kNone;
    // Settings page ("wifi" / "cloud") for the queued launch; static strings only.
    const char* pending_page_ = nullptr;
    // Last drawn text; labels change only when it does (the page polls every 500 ms).
    std::string drawn_key_;
};

void RegisterAssistantApp(PhoneAppRegistry& registry);
