#pragma once

#include "phone_os/phone_app.h"

#include <string>
#include <vector>

#include <lvgl.h>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

namespace rodakos {
class AudioService;
class FileService;
}

class MusicApp : public PhoneApp {
public:
    ~MusicApp() override;

    const char* id() const override { return "music"; }
    bool OnCreate(PhoneAppContext& context) override;
    void OnShow() override;
    void OnHide() override;
    void OnDestroy() override;
    void OnTick() override {}
    void RefreshState();

private:
    struct Track {
        std::string title;
        std::string path;
        size_t size = 0;
    };

    enum class PlaybackMode {
        kSequential,
        kShuffle,
        kRepeatOne,
    };

    void CreateUi();
    void ScanTracks();
    void ScanDirectory(const std::string& path, int depth);
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
    void LoadPlaybackState();
    void SavePlaybackState();
    size_t PickRandomTrackIndex() const;
    void SyncCurrentIndexFromPath(const std::string& path);
    bool PlayFromCompletedState();
    bool HandlePlaybackCompleted();
    void PlayTrack(size_t index);
    void PlayPrevious();
    void PlayNext();
    void TogglePlayPause();
    void StopPlayback();
    void NavigateHome();

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::FileService* file_service_ = nullptr;
    rodakos::AudioService* audio_service_ = nullptr;

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

    std::vector<Track> tracks_;
    int current_index_ = -1;
    PlaybackMode playback_mode_ = PlaybackMode::kSequential;
    bool completion_handled_ = false;
    bool queue_paused_ = false;
};

void RegisterMusicApp(PhoneAppRegistry& registry);
