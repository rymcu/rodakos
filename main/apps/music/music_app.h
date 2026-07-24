#pragma once

#include "phone_os/phone_app.h"

#include <string>
#include <vector>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class MusicPlayerService;
}

class MusicApp : public PhoneApp {
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
    void UpdateTrackCountLabel();
    void RebuildTrackList();
    void ShowTrackPicker();
    void HideTrackPicker();
    void ShowVolumePanel();
    void HideVolumePanel();
    void TogglePlaybackMode();
    void UpdatePlaybackModeLabel();
    const char* PlaybackModeIconText() const;
    bool PlaybackModeShowsBadge() const;
    const char* PlaybackModeToastText() const;
    void PlayTrack(size_t index);
    void PlayPrevious();
    void PlayNext();
    void TogglePlayPause();
    void StopPlayback();
    void NavigateHome();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::MusicPlayerService* music_player_ = nullptr;

    lv_obj_t* root_ = nullptr;
    lv_obj_t* track_title_label_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* progress_bar_ = nullptr;
    lv_obj_t* progress_label_ = nullptr;
    lv_obj_t* play_icon_label_ = nullptr;
    lv_obj_t* action_bar_ = nullptr;
    lv_obj_t* playback_mode_label_ = nullptr;
    lv_obj_t* playback_mode_badge_label_ = nullptr;
    lv_obj_t* volume_panel_ = nullptr;
    lv_obj_t* volume_slider_ = nullptr;
    lv_obj_t* volume_value_label_ = nullptr;
    lv_obj_t* track_count_label_ = nullptr;
    lv_obj_t* track_picker_ = nullptr;
    lv_obj_t* track_list_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
};

void RegisterMusicApp(PhoneAppRegistry& registry);
