#pragma once

#include "phone_os/phone_app.h"
#include "phone_os/recording_service.h"

#include <cstddef>
#include <vector>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class AudioService;
class AudioOutputService;
}

class RecorderApp final : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override;
    void OnPause() override;
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;

    void RefreshState();

private:
    void CreateUi();
    void DestroyUi();
    void ResetUiPointers();
    void RebuildRecordingList();
    void ToggleRecording();
    void StartSpeakerTest();
    void PlayRecording(size_t index);
    void UpdateRecordButton(bool recording);
    void NavigateHome();
    static void RefreshTimerCallback(lv_timer_t* timer);

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::RecordingService* recording_ = nullptr;
    rodakos::AudioService* audio_ = nullptr;
    rodakos::AudioOutputService* audio_output_ = nullptr;
    rodakos::RecordingStatus last_status_ = rodakos::RecordingStatus::kIdle;
    std::vector<rodakos::RecordingEntry> displayed_recordings_;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* duration_label_ = nullptr;
    lv_obj_t* size_label_ = nullptr;
    lv_obj_t* record_button_ = nullptr;
    lv_obj_t* record_icon_label_ = nullptr;
    lv_obj_t* tone_button_ = nullptr;
    lv_obj_t* count_label_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
};

void RegisterRecorderApp(PhoneAppRegistry& registry);
