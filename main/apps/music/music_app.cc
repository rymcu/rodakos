#include "apps/music/music_app.h"

#include "phone_os/music_player_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <cstdio>
#include <inttypes.h>
#include <string>

#include <esp_log.h>

namespace {
constexpr const char* TAG = "MusicApp";

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        context->navigation().ReturnHome();
    }
}

void RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<MusicApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->RefreshState();
    }
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

void StyleRoundButton(lv_obj_t* button, lv_coord_t size, bool primary) {
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, size, size);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, primary ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, rodakos_theme_bg_secondary(), LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(button, 1, LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
}

const char* StatusText(rodakos::AudioPlaybackStatus status) {
    switch (status) {
        case rodakos::AudioPlaybackStatus::kLoading:
            return "Loading";
        case rodakos::AudioPlaybackStatus::kPlaying:
            return "Playing";
        case rodakos::AudioPlaybackStatus::kPaused:
            return "Paused";
        case rodakos::AudioPlaybackStatus::kStopped:
            return "Stopped";
        case rodakos::AudioPlaybackStatus::kCompleted:
            return "Completed";
        case rodakos::AudioPlaybackStatus::kError:
            return "Error";
        case rodakos::AudioPlaybackStatus::kIdle:
        default:
            return "Ready";
    }
}

std::string FormatTrackSize(size_t bytes) {
    char text[24] = {};
    if (bytes < 1024 * 1024) {
        std::snprintf(text, sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return text;
}

}  // namespace

MusicApp::~MusicApp() {
    OnDestroy();
}

bool MusicApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    music_player_ = context.services().music_player();

    if (music_player_ != nullptr) {
        music_player_->Init();
    }

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    CreateUi();
    RebuildTrackList();
    RefreshState();
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, 500, this);

    ESP_LOGI(TAG, "Music app created with %zu tracks",
             music_player_ != nullptr ? music_player_->track_count() : 0);
    return true;
}

void MusicApp::OnShow() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
            lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(root_);
            RefreshState();
        }
    }
}

void MusicApp::OnHide() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked() && root_ != nullptr && lv_obj_is_valid(root_)) {
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void MusicApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            DestroyUi();
        }
    }

    context_ = nullptr;
    ui_ = nullptr;
    music_player_ = nullptr;
}

bool MusicApp::OnThemeChanged(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    const bool was_hidden = root_ != nullptr && lv_obj_is_valid(root_) &&
                            lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    const bool track_picker_was_visible = track_picker_ != nullptr &&
                                          !lv_obj_has_flag(track_picker_, LV_OBJ_FLAG_HIDDEN);
    const bool volume_panel_was_visible = volume_panel_ != nullptr &&
                                          !lv_obj_has_flag(volume_panel_, LV_OBJ_FLAG_HIDDEN);

    DestroyUi();
    CreateUi();
    RebuildTrackList();
    RefreshState();
    if (track_picker_was_visible) {
        ShowTrackPicker();
    } else if (volume_panel_was_visible) {
        ShowVolumePanel();
    }
    if (was_hidden && root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, 500, this);
    return true;
}

void MusicApp::DestroyUi() {
    if (refresh_timer_ != nullptr) {
        lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
    ResetUiPointers();
}

void MusicApp::ResetUiPointers() {
    root_ = nullptr;
    track_title_label_ = nullptr;
    status_label_ = nullptr;
    progress_bar_ = nullptr;
    progress_label_ = nullptr;
    play_icon_label_ = nullptr;
    action_bar_ = nullptr;
    playback_mode_label_ = nullptr;
    playback_mode_badge_label_ = nullptr;
    volume_panel_ = nullptr;
    volume_slider_ = nullptr;
    volume_value_label_ = nullptr;
    track_count_label_ = nullptr;
    track_picker_ = nullptr;
    track_list_ = nullptr;
}

void MusicApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "Music", [](lv_event_t* e) {
        auto* self = static_cast<MusicApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, [](lv_event_t* e) {
        auto* self = static_cast<MusicApp*>(lv_event_get_user_data(e));
        self->NavigateHome();
    }, this);

    auto* now_card = lv_obj_create(root_);
    lv_obj_remove_style_all(now_card);
    lv_obj_set_size(now_card, 300, 58);
    lv_obj_set_pos(now_card, 10, 48);
    lv_obj_set_style_bg_color(now_card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(now_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(now_card, 8, 0);
    lv_obj_set_style_pad_all(now_card, 0, 0);
    lv_obj_clear_flag(now_card, LV_OBJ_FLAG_SCROLLABLE);

    auto* music_icon = CreateText(now_card, FONT_AWESOME_MUSIC, PhoneIconFontLarge(), rodakos_theme_primary());
    lv_obj_align(music_icon, LV_ALIGN_LEFT_MID, 14, 0);

    track_title_label_ = CreateText(now_card, "No track", &phone_font_18, rodakos_theme_text_primary());
    lv_obj_set_width(track_title_label_, 160);
    lv_label_set_long_mode(track_title_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(track_title_label_, LV_ALIGN_TOP_LEFT, 50, 8);

    status_label_ = CreateText(now_card, "Load audio files", &phone_font_12,
                               rodakos_theme_text_secondary());
    lv_obj_set_width(status_label_, 160);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(status_label_, LV_ALIGN_TOP_LEFT, 50, 35);

    track_count_label_ = CreateText(now_card, "0 songs", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(track_count_label_, 66);
    lv_obj_set_style_text_align(track_count_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(track_count_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(track_count_label_, LV_ALIGN_TOP_RIGHT, -12, 10);

    progress_bar_ = lv_bar_create(root_);
    lv_obj_remove_style_all(progress_bar_);
    lv_obj_set_size(progress_bar_, 300, 8);
    lv_obj_set_pos(progress_bar_, 10, 124);
    lv_obj_set_style_radius(progress_bar_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(progress_bar_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(progress_bar_, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(progress_bar_, rodakos_theme_primary(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(progress_bar_, 0, 100);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);

    progress_label_ = CreateText(root_, "0%", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(progress_label_, 300);
    lv_obj_set_style_text_align(progress_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(progress_label_, 10, 108);

    auto* controls = lv_obj_create(root_);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, 300, 42);
    lv_obj_set_pos(controls, 10, 136);
    lv_obj_set_layout(controls, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls, 11, 0);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    auto* prev_btn = lv_btn_create(controls);
    StyleRoundButton(prev_btn, 34, false);
    auto* prev_icon = lv_label_create(prev_btn);
    lv_label_set_text(prev_icon, FONT_AWESOME_BACKWARD_STEP);
    lv_obj_set_style_text_font(prev_icon, PhoneIconFont(), 0);
    lv_obj_set_style_text_color(prev_icon, rodakos_theme_text_primary(), 0);
    lv_obj_center(prev_icon);
    lv_obj_add_event_cb(prev_btn, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->PlayPrevious();
    }, LV_EVENT_CLICKED, this);

    auto* mode_btn = lv_btn_create(controls);
    StyleRoundButton(mode_btn, 34, false);
    playback_mode_label_ = lv_label_create(mode_btn);
    lv_label_set_text(playback_mode_label_, PlaybackModeIconText());
    lv_obj_set_style_text_font(playback_mode_label_, &phone_font_18, 0);
    lv_obj_set_style_text_color(playback_mode_label_, rodakos_theme_text_primary(), 0);
    lv_obj_center(playback_mode_label_);
    playback_mode_badge_label_ = lv_label_create(mode_btn);
    lv_label_set_text(playback_mode_badge_label_, "1");
    lv_obj_set_style_text_font(playback_mode_badge_label_, &phone_font_12, 0);
    lv_obj_set_style_text_color(playback_mode_badge_label_, rodakos_theme_primary(), 0);
    lv_obj_align(playback_mode_badge_label_, LV_ALIGN_TOP_RIGHT, -7, 4);
    lv_obj_add_flag(playback_mode_badge_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(mode_btn, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->TogglePlaybackMode();
    }, LV_EVENT_CLICKED, this);

    auto* play_btn = lv_btn_create(controls);
    StyleRoundButton(play_btn, 42, true);
    play_icon_label_ = lv_label_create(play_btn);
    lv_label_set_text(play_icon_label_, FONT_AWESOME_PLAY);
    lv_obj_set_style_text_font(play_icon_label_, PhoneIconFont(), 0);
    lv_obj_set_style_text_color(play_icon_label_, lv_color_white(), 0);
    lv_obj_center(play_icon_label_);
    lv_obj_add_event_cb(play_btn, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->TogglePlayPause();
    }, LV_EVENT_CLICKED, this);

    auto* volume_btn = lv_btn_create(controls);
    StyleRoundButton(volume_btn, 34, false);
    auto* control_volume_icon = lv_label_create(volume_btn);
    lv_label_set_text(control_volume_icon, FONT_AWESOME_VOLUME_HIGH);
    lv_obj_set_style_text_font(control_volume_icon, PhoneIconFont(), 0);
    lv_obj_set_style_text_color(control_volume_icon, rodakos_theme_text_primary(), 0);
    lv_obj_center(control_volume_icon);
    lv_obj_add_event_cb(volume_btn, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->ShowVolumePanel();
    }, LV_EVENT_CLICKED, this);

    auto* next_btn = lv_btn_create(controls);
    StyleRoundButton(next_btn, 34, false);
    auto* next_icon = lv_label_create(next_btn);
    lv_label_set_text(next_icon, FONT_AWESOME_FORWARD_STEP);
    lv_obj_set_style_text_font(next_icon, PhoneIconFont(), 0);
    lv_obj_set_style_text_color(next_icon, rodakos_theme_text_primary(), 0);
    lv_obj_center(next_icon);
    lv_obj_add_event_cb(next_btn, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->PlayNext();
    }, LV_EVENT_CLICKED, this);

    action_bar_ = lv_obj_create(root_);
    lv_obj_remove_style_all(action_bar_);
    lv_obj_set_size(action_bar_, 300, 46);
    lv_obj_set_pos(action_bar_, 10, 184);
    lv_obj_set_style_bg_color(action_bar_, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(action_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(action_bar_, 8, 0);
    lv_obj_set_style_pad_all(action_bar_, 0, 0);
    lv_obj_clear_flag(action_bar_, LV_OBJ_FLAG_SCROLLABLE);

    auto* songs_btn = lv_btn_create(action_bar_);
    lv_obj_remove_style_all(songs_btn);
    lv_obj_set_size(songs_btn, 280, 30);
    lv_obj_align(songs_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(songs_btn, 8, 0);
    lv_obj_set_style_bg_color(songs_btn, rodakos_theme_primary(), 0);
    lv_obj_set_style_bg_opa(songs_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(songs_btn, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(songs_btn, 1, LV_STATE_PRESSED);
    lv_obj_clear_flag(songs_btn, LV_OBJ_FLAG_SCROLLABLE);
    auto* songs_label = CreateText(songs_btn, "Songs", &phone_font_14, lv_color_white());
    lv_obj_center(songs_label);
    lv_obj_add_event_cb(songs_btn, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->ShowTrackPicker();
    }, LV_EVENT_CLICKED, this);

    volume_panel_ = lv_obj_create(root_);
    lv_obj_remove_style_all(volume_panel_);
    lv_obj_set_size(volume_panel_, 300, 46);
    lv_obj_set_pos(volume_panel_, 10, 184);
    lv_obj_set_style_bg_color(volume_panel_, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(volume_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(volume_panel_, 8, 0);
    lv_obj_set_style_pad_all(volume_panel_, 0, 0);
    lv_obj_clear_flag(volume_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(volume_panel_, LV_OBJ_FLAG_HIDDEN);

    auto* volume_icon = CreateText(volume_panel_, FONT_AWESOME_VOLUME_HIGH, PhoneIconFont(),
                                   rodakos_theme_primary());
    lv_obj_align(volume_icon, LV_ALIGN_LEFT_MID, 12, 0);

    volume_slider_ = lv_slider_create(volume_panel_);
    lv_obj_set_size(volume_slider_, 166, 8);
    lv_obj_align(volume_slider_, LV_ALIGN_LEFT_MID, 42, 0);
    lv_slider_set_range(volume_slider_, 0, 100);
    lv_slider_set_value(volume_slider_, music_player_ != nullptr ? music_player_->volume() : 60, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(volume_slider_, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider_, rodakos_theme_primary(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider_, rodakos_theme_primary(), LV_PART_KNOB);
    lv_obj_add_event_cb(volume_slider_, [](lv_event_t* e) {
        auto* self = static_cast<MusicApp*>(lv_event_get_user_data(e));
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const int value = lv_slider_get_value(slider);
        if (self->music_player_ != nullptr) {
            self->music_player_->SetVolume(value);
        }
        if (self->volume_value_label_ != nullptr) {
            lv_label_set_text_fmt(self->volume_value_label_, "%d%%", value);
        }
    }, LV_EVENT_VALUE_CHANGED, this);

    volume_value_label_ = CreateText(volume_panel_, "60%", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(volume_value_label_, 38);
    lv_obj_set_style_text_align(volume_value_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(volume_value_label_, LV_ALIGN_RIGHT_MID, -42, 0);

    auto* volume_done_btn = lv_btn_create(volume_panel_);
    lv_obj_remove_style_all(volume_done_btn);
    lv_obj_set_size(volume_done_btn, 30, 30);
    lv_obj_align(volume_done_btn, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_radius(volume_done_btn, 8, 0);
    lv_obj_set_style_bg_color(volume_done_btn, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(volume_done_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(volume_done_btn, rodakos_theme_primary(), LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(volume_done_btn, 1, LV_STATE_PRESSED);
    lv_obj_clear_flag(volume_done_btn, LV_OBJ_FLAG_SCROLLABLE);
    auto* volume_done_icon = CreateText(volume_done_btn, FONT_AWESOME_XMARK, PhoneIconFont(),
                                        rodakos_theme_text_primary());
    lv_obj_center(volume_done_icon);
    lv_obj_add_event_cb(volume_done_btn, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->HideVolumePanel();
    }, LV_EVENT_CLICKED, this);

    track_picker_ = lv_obj_create(root_);
    lv_obj_remove_style_all(track_picker_);
    lv_obj_set_size(track_picker_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(track_picker_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(track_picker_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(track_picker_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(track_picker_, LV_OBJ_FLAG_HIDDEN);

    CreateAppHeader(track_picker_, "Songs", [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->HideTrackPicker();
    }, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->NavigateHome();
    }, this);

    track_list_ = lv_obj_create(track_picker_);
    lv_obj_remove_style_all(track_list_);
    lv_obj_set_size(track_list_, 300, 192);
    lv_obj_set_pos(track_list_, 10, 44);
    lv_obj_set_flex_flow(track_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(track_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(track_list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(track_list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(track_list_, 6, 0);
}

void MusicApp::UpdateTrackCountLabel() {
    if (track_count_label_ != nullptr) {
        lv_label_set_text_fmt(track_count_label_, "%zu songs",
                              music_player_ != nullptr ? music_player_->track_count() : 0);
    }
}

void MusicApp::RebuildTrackList() {
    if (track_list_ == nullptr) {
        return;
    }
    lv_obj_clean(track_list_);

    UpdateTrackCountLabel();

    const auto tracks = music_player_ != nullptr ? music_player_->GetTracks() : std::vector<rodakos::MusicTrack>{};
    if (tracks.empty()) {
        auto* empty = CreateText(track_list_, "No supported audio files", &phone_font_12,
                                 rodakos_theme_text_tertiary());
        lv_obj_set_width(empty, 300);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    for (size_t i = 0; i < tracks.size(); ++i) {
        auto* row = lv_btn_create(track_list_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 300, 42);
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        auto* title = CreateText(row, tracks[i].title.c_str(), &phone_font_14,
                                 rodakos_theme_text_primary());
        lv_obj_set_width(title, 218);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 5);

        const auto size_text = FormatTrackSize(tracks[i].size);
        auto* detail = CreateText(row, size_text.c_str(), &phone_font_12,
                                  rodakos_theme_text_tertiary());
        lv_obj_set_width(detail, 218);
        lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
        lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 12, 24);

        auto* play_hint = CreateText(row, FONT_AWESOME_PLAY, PhoneIconFont(), rodakos_theme_primary());
        lv_obj_align(play_hint, LV_ALIGN_RIGHT_MID, -12, 0);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            auto* self = static_cast<MusicApp*>(lv_event_get_user_data(e));
            const uintptr_t index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(e))));
            self->PlayTrack(static_cast<size_t>(index));
            self->HideTrackPicker();
        }, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(i));
    }
}

void MusicApp::ShowTrackPicker() {
    if (track_picker_ == nullptr) {
        return;
    }
    HideVolumePanel();
    lv_obj_move_foreground(track_picker_);
    lv_obj_clear_flag(track_picker_, LV_OBJ_FLAG_HIDDEN);
}

void MusicApp::HideTrackPicker() {
    if (track_picker_ == nullptr) {
        return;
    }
    lv_obj_add_flag(track_picker_, LV_OBJ_FLAG_HIDDEN);
}

void MusicApp::ShowVolumePanel() {
    if (volume_panel_ == nullptr) {
        return;
    }
    if (action_bar_ != nullptr) {
        lv_obj_add_flag(action_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_move_foreground(volume_panel_);
    lv_obj_clear_flag(volume_panel_, LV_OBJ_FLAG_HIDDEN);
}

void MusicApp::HideVolumePanel() {
    if (volume_panel_ != nullptr) {
        lv_obj_add_flag(volume_panel_, LV_OBJ_FLAG_HIDDEN);
    }
    if (action_bar_ != nullptr) {
        lv_obj_clear_flag(action_bar_, LV_OBJ_FLAG_HIDDEN);
    }
}

void MusicApp::TogglePlaybackMode() {
    if (music_player_ == nullptr) {
        return;
    }
    music_player_->TogglePlaybackMode();
    UpdatePlaybackModeLabel();
    if (ui_ != nullptr) {
        ui_->ShowToastUnlocked(PlaybackModeToastText());
    }
}

void MusicApp::UpdatePlaybackModeLabel() {
    if (playback_mode_label_ != nullptr) {
        lv_label_set_text(playback_mode_label_, PlaybackModeIconText());
    }
    if (playback_mode_badge_label_ != nullptr) {
        if (PlaybackModeShowsBadge()) {
            lv_obj_clear_flag(playback_mode_badge_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(playback_mode_badge_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

const char* MusicApp::PlaybackModeIconText() const {
    const auto mode = music_player_ != nullptr
        ? music_player_->playback_mode()
        : rodakos::MusicPlaybackMode::kSequential;
    switch (mode) {
        case rodakos::MusicPlaybackMode::kShuffle:
            return LV_SYMBOL_SHUFFLE;
        case rodakos::MusicPlaybackMode::kRepeatOne:
            return LV_SYMBOL_LOOP;
        case rodakos::MusicPlaybackMode::kSequential:
        default:
            return FONT_AWESOME_ARROW_RIGHT;
    }
}

bool MusicApp::PlaybackModeShowsBadge() const {
    return music_player_ != nullptr &&
           music_player_->playback_mode() == rodakos::MusicPlaybackMode::kRepeatOne;
}

const char* MusicApp::PlaybackModeToastText() const {
    const auto mode = music_player_ != nullptr
        ? music_player_->playback_mode()
        : rodakos::MusicPlaybackMode::kSequential;
    switch (mode) {
        case rodakos::MusicPlaybackMode::kShuffle:
            return "Random play";
        case rodakos::MusicPlaybackMode::kRepeatOne:
            return "Repeat one";
        case rodakos::MusicPlaybackMode::kSequential:
        default:
            return "Sequential play";
    }
}

void MusicApp::PlayTrack(size_t index) {
    if (music_player_ == nullptr) {
        return;
    }
    if (!music_player_->PlayTrack(index)) {
        ui_->ShowToastUnlocked("Cannot play this file");
    }
    RefreshState();
}

void MusicApp::PlayPrevious() {
    if (music_player_ == nullptr) {
        return;
    }
    if (!music_player_->PlayPrevious()) {
        ui_->ShowToastUnlocked("No tracks");
    }
    RefreshState();
}

void MusicApp::PlayNext() {
    if (music_player_ == nullptr) {
        return;
    }
    if (!music_player_->PlayNext()) {
        ui_->ShowToastUnlocked("No tracks");
    }
    RefreshState();
}

void MusicApp::TogglePlayPause() {
    if (music_player_ == nullptr) {
        return;
    }
    if (!music_player_->TogglePlayPause()) {
        ui_->ShowToastUnlocked("No tracks");
    }
    RefreshState();
}

void MusicApp::StopPlayback() {
    if (music_player_ != nullptr) {
        music_player_->Stop();
    }
    RefreshState();
}

void MusicApp::RefreshState() {
    if (music_player_ == nullptr || track_title_label_ == nullptr) {
        return;
    }

    UpdateTrackCountLabel();

    const auto player_state = music_player_->GetState();
    const auto& state = player_state.audio;

    if (!state.title.empty()) {
        lv_label_set_text(track_title_label_, state.title.c_str());
    } else if (player_state.track_count == 0) {
        lv_label_set_text(track_title_label_, "No track");
    } else if (!player_state.current_title.empty()) {
        lv_label_set_text(track_title_label_, player_state.current_title.c_str());
    } else {
        lv_label_set_text(track_title_label_, "Ready");
    }

    char status_text[96] = {};
    if (state.sample_rate > 0) {
        std::snprintf(status_text, sizeof(status_text), "%s %d%%",
                      StatusText(state.status), state.progress_percent);
    } else if (!state.message.empty()) {
        std::snprintf(status_text, sizeof(status_text), "%s", state.message.c_str());
    } else if (player_state.track_count == 0) {
        std::snprintf(status_text, sizeof(status_text), "Put audio files in /music");
    } else {
        std::snprintf(status_text, sizeof(status_text), "Ready");
    }
    lv_label_set_text(status_label_, status_text);

    lv_bar_set_value(progress_bar_, state.progress_percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(progress_label_, "%d%%", state.progress_percent);
    const bool show_pause = state.status == rodakos::AudioPlaybackStatus::kPlaying ||
                            state.status == rodakos::AudioPlaybackStatus::kLoading;
    lv_label_set_text(play_icon_label_, show_pause ? FONT_AWESOME_PAUSE : FONT_AWESOME_PLAY);

    if (volume_slider_ != nullptr) {
        lv_slider_set_value(volume_slider_, state.volume, LV_ANIM_OFF);
    }
    if (volume_value_label_ != nullptr) {
        lv_label_set_text_fmt(volume_value_label_, "%d%%", state.volume);
    }
    UpdatePlaybackModeLabel();
}

void MusicApp::NavigateHome() {
    lv_async_call(DeferReturnHome, context_);
}

void RegisterMusicApp(PhoneAppRegistry& registry) {
    PhoneAppDescriptor descriptor;
    descriptor.id = "music";
    descriptor.title = "Music";
    descriptor.icon = FONT_AWESOME_MUSIC;
    descriptor.category = PhoneAppCategory::kMedia;
    descriptor.capabilities = PhoneCapability::kStorage |
                              PhoneCapability::kAudioPlayback |
                              PhoneCapability::kBackgroundTick;
    descriptor.create = []() -> std::unique_ptr<PhoneApp> {
        return std::make_unique<MusicApp>();
    };
    registry.Register(std::move(descriptor));
}
