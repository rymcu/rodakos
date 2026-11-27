#pragma once

#include "phone_os/phone_app.h"
#include "phone_os/recording_service.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class AudioService;
class AudioOutputService;
}

// Two pages, like Music: a recording page (timer, live level, record button, speaker test and
// replay of the latest recording) and a list page (play, progress, delete). Recording errors and
// list scan errors stay separate (docs/media-save.md).
class RecorderApp final : public PhoneApp {
public:
    static constexpr size_t kLevelBars = 20;

    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override;
    void OnPause() override;
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;
    bool OnBackRequested() override;

    void RefreshState();

private:
    enum class View { kRecord, kList };

    void CreateUi();
    void DestroyUi();
    void ResetUiPointers();
    void CreateRecordView();
    void CreateListView();
    void ShowView(View view);
    void RebuildRecordingList();
    void RefreshRecordView(const rodakos::RecordingState& state);
    void RefreshListPlayback();
    void PushLevel(uint8_t level);
    void ToggleRecording();
    void StartSpeakerTest();
    void ToggleLatestPlayback();
    void PlayRecording(size_t index);
    void AskDelete(size_t index);
    void CloseDeleteDialog();
    void ConfirmDelete();
    void BoostPlaybackVolume();
    void RestorePlaybackVolume();
    void UpdateRecordButton(bool recording, bool enabled);
    void SetRefreshPeriod(bool recording);
    void NavigateHome();
    bool IsPlaying(const rodakos::RecordingEntry& entry) const;
    static void RefreshTimerCallback(lv_timer_t* timer);

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::RecordingService* recording_ = nullptr;
    rodakos::AudioService* audio_ = nullptr;
    rodakos::AudioOutputService* audio_output_ = nullptr;
    rodakos::RecordingStatus last_status_ = rodakos::RecordingStatus::kIdle;
    // Volume to restore after recording playback raised it; -1 when no boost is pending.
    int restore_volume_ = -1;
    std::string last_library_error_;
    std::string last_recording_error_;
    std::vector<rodakos::RecordingEntry> displayed_recordings_;
    View view_ = View::kRecord;
    size_t pending_delete_ = 0;
    bool fast_refresh_ = false;
    std::array<uint8_t, kLevelBars> levels_{};
    std::string info_detail_text_;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* list_button_ = nullptr;
    lv_obj_t* wake_hint_ = nullptr;

    lv_obj_t* record_view_ = nullptr;
    lv_obj_t* status_dot_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* duration_label_ = nullptr;
    lv_obj_t* stage_ = nullptr;
    std::array<lv_obj_t*, kLevelBars> level_bars_{};
    lv_obj_t* info_box_ = nullptr;
    lv_obj_t* info_icon_ = nullptr;
    lv_obj_t* info_title_label_ = nullptr;
    lv_obj_t* info_detail_label_ = nullptr;
    lv_obj_t* info_progress_ = nullptr;
    lv_obj_t* info_progress_fill_ = nullptr;
    lv_obj_t* record_button_ = nullptr;
    lv_obj_t* record_core_ = nullptr;
    lv_obj_t* tone_button_ = nullptr;
    lv_obj_t* replay_button_ = nullptr;
    lv_obj_t* replay_icon_ = nullptr;
    lv_obj_t* replay_label_ = nullptr;

    lv_obj_t* list_view_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t* list_status_ = nullptr;
    lv_obj_t* delete_layer_ = nullptr;

    lv_timer_t* refresh_timer_ = nullptr;
};

void RegisterRecorderApp(PhoneAppRegistry& registry);
