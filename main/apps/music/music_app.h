#pragma once

#include "phone_os/music_player_service.h"
#include "phone_os/phone_app.h"

#include <lvgl.h>
#include <array>
#include <string>
#include <vector>

class PhoneAppContext;
class PhoneAppRegistry;
class PhoneUi;

class MusicApp : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext& context) override;
    void OnResume() override;
    void OnPause() override;
    void OnDestroy() override;
    bool OnThemeChanged(PhoneAppContext& context) override;
    bool OnBackRequested() override;
    void RefreshState();

private:
    // kVolume is the player with the volume sheet raised over it.
    enum class Page { kPlayer, kSongs, kVolume };
    enum class Recovery { kNone, kLibrary, kPlayback };

    struct SongRow {
        lv_obj_t* button = nullptr;
        lv_obj_t* lead = nullptr;
        lv_obj_t* title = nullptr;
        lv_obj_t* artist = nullptr;
        size_t index = 0;
        uint64_t revision = 0;
        bool pressed = false;
        bool current = false;
    };

    void ShowPage(Page page);
    void DestroyPage();
    void CreatePlayer();
    void CreateSongs();
    void OpenVolume();
    void CloseVolume();
    void RefreshPlayer(const rodakos::MusicPlayerState& state);
    void RefreshSongs(const rodakos::MusicPlayerState& state);
    void BindSongRows();
    void SelectSong(lv_obj_t* button, lv_event_code_t event);
    void RefreshLibrary();
    void RetryPlayback();
    void PressPlay();
    void ApplyVolume();
    void DisplayVolume(int value);
    lv_obj_t* CreateChip(int x, lv_event_cb_t callback);

    PhoneAppContext* context_ = nullptr;
    PhoneUi* ui_ = nullptr;
    rodakos::MusicPlayerService* music_player_ = nullptr;
    lv_timer_t* refresh_timer_ = nullptr;
    lv_obj_t* root_ = nullptr;
    bool paused_ = false;
    Page page_ = Page::kPlayer;
    Recovery recovery_ = Recovery::kNone;
    uint64_t playback_error_revision_ = 0;

    // Player.
    lv_obj_t* back_button_ = nullptr;
    lv_obj_t* songs_button_ = nullptr;
    lv_obj_t* wake_hint_ = nullptr;
    lv_obj_t* cover_ = nullptr;
    lv_obj_t* cover_icon_ = nullptr;
    lv_obj_t* track_title_label_ = nullptr;
    lv_obj_t* detail_label_ = nullptr;
    lv_obj_t* mode_button_ = nullptr;
    lv_obj_t* mode_icon_ = nullptr;
    lv_obj_t* mode_badge_ = nullptr;
    lv_obj_t* volume_button_ = nullptr;
    lv_obj_t* volume_chip_label_ = nullptr;
    lv_obj_t* stop_button_ = nullptr;
    lv_obj_t* progress_bar_ = nullptr;
    lv_obj_t* elapsed_label_ = nullptr;
    lv_obj_t* duration_label_ = nullptr;
    lv_obj_t* previous_button_ = nullptr;
    lv_obj_t* play_button_ = nullptr;
    lv_obj_t* play_icon_ = nullptr;
    lv_obj_t* next_button_ = nullptr;

    // Volume sheet, created only while raised.
    lv_obj_t* volume_scrim_ = nullptr;
    lv_obj_t* volume_slider_ = nullptr;
    lv_obj_t* volume_value_label_ = nullptr;

    // Song list: a fixed pool of row slots rebound while scrolling.
    lv_obj_t* songs_title_ = nullptr;
    lv_obj_t* refresh_library_button_ = nullptr;
    lv_obj_t* track_list_ = nullptr;
    lv_obj_t* list_extent_ = nullptr;
    lv_obj_t* list_message_ = nullptr;
    lv_obj_t* list_retry_button_ = nullptr;
    std::array<SongRow, 7> song_rows_{};
    std::vector<rodakos::MusicTrack> tracks_;
    uint64_t displayed_library_revision_ = 0;
    int current_song_index_ = -1;
};

void RegisterMusicApp(PhoneAppRegistry& registry);
