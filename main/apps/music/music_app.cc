#include "apps/music/music_app.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <algorithm>
#include <cstdio>
#include <string_view>

namespace {
using rodakos::AudioPlaybackStatus;
using rodakos::MusicLibraryStatus;
using rodakos::MusicPlaybackMode;

// Geometry follows the approved 320x240 design (.codex-temp/design/media-ui.html).
constexpr lv_coord_t kHeader = kRodakosAppHeaderHeight;
constexpr lv_coord_t kCoverSize = 96;
constexpr lv_coord_t kCoverX = 16;
constexpr lv_coord_t kCoverY = 52;
constexpr lv_coord_t kInfoX = 124;
constexpr lv_coord_t kInfoWidth = 180;
constexpr lv_coord_t kChipY = 112;
constexpr lv_coord_t kChipWidth = 56;
constexpr lv_coord_t kChipHeight = 36;
constexpr lv_coord_t kChipGap = 6;
constexpr lv_coord_t kBarY = 158;
constexpr lv_coord_t kTimeY = 166;
constexpr lv_coord_t kTransportY = 186;
constexpr lv_coord_t kPlaySize = 52;
constexpr lv_coord_t kRowHeight = 40;
constexpr lv_coord_t kListHeight = 200;
constexpr lv_coord_t kSheetHeight = 92;

void ReturnHome(void* data) {
    static_cast<PhoneAppContext*>(data)->navigation().ReturnHome();
}

void SetText(lv_obj_t* label, const char* text) {
    if (label != nullptr && std::string_view(lv_label_get_text(label)) != text) lv_label_set_text(label, text);
}

void SetVisible(lv_obj_t* object, bool visible) {
    if (object == nullptr) return;
    if (visible) lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
}

void SetEnabled(lv_obj_t* object, bool enabled) {
    if (object == nullptr) return;
    if (enabled) lv_obj_remove_state(object, LV_STATE_DISABLED);
    else lv_obj_add_state(object, LV_STATE_DISABLED);
}

void FormatTime(char* out, size_t size, uint32_t ms) {
    const uint32_t seconds = ms / 1000;
    std::snprintf(out, size, "%u:%02u", static_cast<unsigned>(seconds / 60), static_cast<unsigned>(seconds % 60));
}

bool IsActive(AudioPlaybackStatus status) {
    return status == AudioPlaybackStatus::kLoading || status == AudioPlaybackStatus::kPlaying;
}

const char* LibraryStatus(MusicLibraryStatus status) {
    switch (status) {
        case MusicLibraryStatus::kNotScanned: return "尚未读取歌曲";
        case MusicLibraryStatus::kScanning: return "正在读取歌曲";
        case MusicLibraryStatus::kReady: return "";
        case MusicLibraryStatus::kEmpty: return "暂无歌曲";
        case MusicLibraryStatus::kServiceUnavailable: return "文件服务不可用";
        case MusicLibraryStatus::kStorageUnavailable: return "读不到 SD 卡";
        case MusicLibraryStatus::kReadError: return "无法读取歌曲目录";
    }
    return "";
}

const char* LibraryHint(MusicLibraryStatus status) {
    switch (status) {
        case MusicLibraryStatus::kEmpty: return "放进 SD 卡的 music 文件夹";
        case MusicLibraryStatus::kStorageUnavailable:
        case MusicLibraryStatus::kReadError: return "检查 SD 卡后重试";
        default: return "";
    }
}

const char* PlaybackError(std::string_view message) {
    if (message == "Playback is busy with another app") return "音频被其他应用占用";
    if (message == "Music library changed; choose a song again") return "曲库已更新 · 请重新选歌";
    if (message == "Cannot open file") return "无法打开歌曲";
    if (message == "Audio output failed") return "音频输出失败";
    if (message == "Codec open failed" || message == "Cannot resume audio hardware" ||
        message == "Audio DAC unavailable" || message == "Audio hardware unavailable") {
        return "音频设备无法启动";
    }
    if (message == "Music is busy; retry shortly" || message == "Playback is starting" ||
        message == "Previous playback busy" || message == "Music is stopping") {
        return "播放器忙 · 请重试";
    }
    if (message == "Unsupported audio file" || message.find("WAV") != std::string_view::npos ||
        message.find("MP3") != std::string_view::npos) return "无法解码这首歌";
    return "播放失败";
}

bool WorkerUnavailable(const rodakos::MusicPlayerState& state) {
    return state.operation_error == "Music worker unavailable; retry";
}

// "歌名-歌手" file names carry both parts; anything else is a title only.
void SplitTitle(const std::string& title, std::string& song, std::string& artist) {
    const auto dash = title.rfind('-');
    if (dash == std::string::npos || dash == 0 || dash + 1 >= title.size()) {
        song = title;
        artist.clear();
        return;
    }
    song = title.substr(0, dash);
    artist = title.substr(dash + 1);
    while (!song.empty() && song.back() == ' ') song.pop_back();
    while (!artist.empty() && artist.front() == ' ') artist.erase(artist.begin());
}

lv_obj_t* CreateBox(lv_obj_t* parent) {
    auto* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    return box;
}

lv_obj_t* CreateLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* CreateGlyph(lv_obj_t* parent, const char* icon, lv_color_t color, const lv_font_t* font) {
    auto* label = CreateLabel(parent, font, color);
    lv_label_set_text(label, icon);
    return label;
}

lv_obj_t* CreateFlatButton(lv_obj_t* parent, lv_coord_t width, lv_coord_t height, lv_coord_t radius,
                           lv_color_t pressed) {
    auto* button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, radius, 0);
    lv_obj_set_style_bg_color(button, pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    return button;
}
}  // namespace

bool MusicApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    music_player_ = context.services().music_player();
    paused_ = false;
    if (music_player_ != nullptr) music_player_->Init();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;
    ShowPage(Page::kPlayer);
    refresh_timer_ = lv_timer_create([](lv_timer_t* timer) {
        static_cast<MusicApp*>(lv_timer_get_user_data(timer))->RefreshState();
    }, 500, this);
    return true;
}

void MusicApp::OnPause() {
    PhoneUiLock lock(*ui_);
    if (!lock.locked() || root_ == nullptr) return;
    paused_ = true;
    SetVisible(root_, false);
}

void MusicApp::OnResume() {
    PhoneUiLock lock(*ui_);
    if (!lock.locked() || root_ == nullptr) return;
    paused_ = false;
    SetVisible(root_, true);
    lv_obj_move_foreground(root_);
    RefreshState();
}

void MusicApp::OnDestroy() {
    if (root_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return;
    if (refresh_timer_ != nullptr) {
        lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
    }
    DestroyPage();
}

bool MusicApp::OnThemeChanged(PhoneAppContext& context) {
    PhoneUiLock lock(context.ui());
    if (!lock.locked()) return false;
    context_ = &context;
    ui_ = &context.ui();
    ShowPage(page_ == Page::kVolume ? Page::kPlayer : page_);
    return true;
}

bool MusicApp::OnBackRequested() {
    if (page_ == Page::kPlayer || root_ == nullptr) return false;
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return true;
    if (page_ == Page::kVolume) CloseVolume();
    else ShowPage(Page::kPlayer);
    return true;
}

void MusicApp::DestroyPage() {
    if (root_ != nullptr) lv_obj_delete(root_);
    root_ = nullptr;
    recovery_ = Recovery::kNone;
    back_button_ = songs_button_ = wake_hint_ = cover_ = cover_icon_ = nullptr;
    track_title_label_ = detail_label_ = nullptr;
    mode_button_ = mode_icon_ = mode_badge_ = volume_button_ = volume_chip_label_ = stop_button_ = nullptr;
    progress_bar_ = elapsed_label_ = duration_label_ = nullptr;
    previous_button_ = play_button_ = play_icon_ = next_button_ = nullptr;
    volume_scrim_ = volume_slider_ = volume_value_label_ = nullptr;
    songs_title_ = refresh_library_button_ = track_list_ = list_extent_ = nullptr;
    list_message_ = list_retry_button_ = nullptr;
    song_rows_ = {};
    std::vector<rodakos::MusicTrack>().swap(tracks_);
    displayed_library_revision_ = 0;
    current_song_index_ = -1;
}

void MusicApp::ShowPage(Page page) {
    DestroyPage();
    page_ = page;
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    SetVisible(root_, !paused_);

    if (page == Page::kSongs) {
        CreateSongs();
        if (music_player_ != nullptr) tracks_ = music_player_->GetTracks(&displayed_library_revision_);
    } else {
        CreatePlayer();
        if (page == Page::kVolume) {
            page_ = Page::kPlayer;
            OpenVolume();
        }
    }
    RefreshState();
}

lv_obj_t* MusicApp::CreateChip(int x, lv_event_cb_t callback) {
    auto* chip = CreateFlatButton(root_, kChipWidth, kChipHeight, 8, rodakos_theme_bg_tertiary());
    lv_obj_set_style_bg_color(chip, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_pos(chip, x, kChipY);
    lv_obj_add_event_cb(chip, callback, LV_EVENT_CLICKED, this);
    return chip;
}

void MusicApp::CreatePlayer() {
    auto* header = CreateAppHeader(root_, "音乐", [](lv_event_t* e) {
        auto* self = static_cast<MusicApp*>(lv_event_get_user_data(e));
        lv_async_call(ReturnHome, self->context_);
    }, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->ShowPage(Page::kSongs);
    }, this, nullptr, ICON_MUSIC, &songs_button_);
    back_button_ = lv_obj_get_child(header, 0);
    lv_obj_set_style_opa(songs_button_, LV_OPA_40, LV_STATE_DISABLED);
    wake_hint_ = CreateGlyph(header, ICON_MICROPHONE, rodakos_theme_text_tertiary(), PhoneIconFont());
    lv_obj_align(wake_hint_, LV_ALIGN_RIGHT_MID, -50, 0);
    // A diagonal stroke over the outline mic reads as "listening off" at 20 px.
    auto* slash = CreateBox(wake_hint_);
    lv_obj_add_flag(slash, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(slash, 2, 24);
    lv_obj_set_style_bg_color(slash, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_bg_opa(slash, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(slash, 1, 0);
    lv_obj_set_style_transform_rotation(slash, 450, 0);
    lv_obj_set_style_transform_pivot_x(slash, 1, 0);
    lv_obj_set_style_transform_pivot_y(slash, 12, 0);
    lv_obj_align(slash, LV_ALIGN_CENTER, 0, 0);

    cover_ = CreateBox(root_);
    lv_obj_set_size(cover_, kCoverSize, kCoverSize);
    lv_obj_set_pos(cover_, kCoverX, kCoverY);
    lv_obj_set_style_radius(cover_, 12, 0);
    lv_obj_set_style_bg_opa(cover_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(cover_, LV_GRAD_DIR_VER, 0);
    cover_icon_ = CreateGlyph(cover_, ICON_MUSIC, lv_color_white(), PhoneIconFontLarge());
    lv_obj_center(cover_icon_);

    track_title_label_ = CreateLabel(root_, &phone_font_18, rodakos_theme_text_primary());
    lv_label_set_long_mode(track_title_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_size(track_title_label_, kInfoWidth, 22);
    lv_obj_set_pos(track_title_label_, kInfoX, 54);
    detail_label_ = CreateLabel(root_, &phone_font_14, rodakos_theme_text_secondary());
    lv_label_set_long_mode(detail_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_size(detail_label_, kInfoWidth, 20);
    lv_obj_set_pos(detail_label_, kInfoX, 80);

    mode_button_ = CreateChip(kInfoX, [](lv_event_t* e) {
        auto* self = static_cast<MusicApp*>(lv_event_get_user_data(e));
        if (self->music_player_ != nullptr) self->music_player_->TogglePlaybackMode();
        self->RefreshState();
    });
    mode_icon_ = CreateGlyph(mode_button_, ICON_REPEAT, rodakos_theme_text_primary(), PhoneIconFont());
    lv_obj_center(mode_icon_);
    mode_badge_ = CreateLabel(mode_button_, &phone_font_12, rodakos_theme_primary());
    lv_label_set_text(mode_badge_, "1");
    lv_obj_align(mode_badge_, LV_ALIGN_CENTER, 15, -8);

    volume_button_ = CreateChip(kInfoX + kChipWidth + kChipGap, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->OpenVolume();
    });
    lv_obj_set_flex_flow(volume_button_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(volume_button_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(volume_button_, 2, 0);
    CreateGlyph(volume_button_, ICON_VOLUME, rodakos_theme_text_primary(), PhoneIconFont());
    volume_chip_label_ = CreateLabel(volume_button_, &phone_font_12, rodakos_theme_text_primary());

    stop_button_ = CreateChip(kInfoX + 2 * (kChipWidth + kChipGap), [](lv_event_t* e) {
        auto* self = static_cast<MusicApp*>(lv_event_get_user_data(e));
        if (self->music_player_ != nullptr) self->music_player_->Stop();
        self->RefreshState();
    });
    lv_obj_center(CreateGlyph(stop_button_, ICON_STOP, rodakos_theme_text_primary(), PhoneIconFont()));

    progress_bar_ = lv_bar_create(root_);
    lv_obj_set_size(progress_bar_, 288, 4);
    lv_obj_set_pos(progress_bar_, 16, kBarY);
    lv_obj_set_style_radius(progress_bar_, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(progress_bar_, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(progress_bar_, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar_, rodakos_theme_primary(), LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(progress_bar_, 0, 0);
    lv_bar_set_range(progress_bar_, 0, 1000);
    lv_obj_remove_flag(progress_bar_, LV_OBJ_FLAG_CLICKABLE);
    elapsed_label_ = CreateLabel(root_, &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_pos(elapsed_label_, 16, kTimeY);
    duration_label_ = CreateLabel(root_, &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_align(duration_label_, LV_ALIGN_TOP_RIGHT, -16, kTimeY);

    const auto transport = [&](const char* icon, lv_coord_t x, lv_event_cb_t callback) {
        auto* button = CreateFlatButton(root_, 56, 44, 10, rodakos_theme_bg_secondary());
        lv_obj_set_pos(button, x, kTransportY);
        lv_obj_center(CreateGlyph(button, icon, rodakos_theme_text_primary(), PhoneIconFont()));
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
        return button;
    };
    previous_button_ = transport(ICON_SKIP_BACK, 68, [](lv_event_t* e) {
        auto* self = static_cast<MusicApp*>(lv_event_get_user_data(e));
        if (self->music_player_ != nullptr) self->music_player_->PlayPrevious();
        self->RefreshState();
    });
    next_button_ = transport(ICON_SKIP_FORWARD, 196, [](lv_event_t* e) {
        auto* self = static_cast<MusicApp*>(lv_event_get_user_data(e));
        if (self->music_player_ != nullptr) self->music_player_->PlayNext();
        self->RefreshState();
    });

    play_button_ = lv_button_create(root_);
    lv_obj_remove_style_all(play_button_);
    lv_obj_set_size(play_button_, kPlaySize, kPlaySize);
    lv_obj_set_pos(play_button_, 134, 182);
    lv_obj_set_style_radius(play_button_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(play_button_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(play_button_, rodakos_theme_primary(), 0);
    lv_obj_set_style_bg_color(play_button_, lv_color_darken(rodakos_theme_primary(), 40), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(play_button_, rodakos_theme_bg_tertiary(), LV_STATE_DISABLED);
    lv_obj_set_ext_click_area(play_button_, 4);
    lv_obj_remove_flag(play_button_, LV_OBJ_FLAG_SCROLLABLE);
    play_icon_ = CreateGlyph(play_button_, ICON_PLAY_FILLED, ui_->theme().accent_text, PhoneIconFontLarge());
    lv_obj_align(play_icon_, LV_ALIGN_CENTER, 2, 0);
    lv_obj_add_event_cb(play_button_, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->PressPlay();
    }, LV_EVENT_CLICKED, this);
}

void MusicApp::OpenVolume() {
    if (page_ != Page::kPlayer || volume_scrim_ != nullptr) return;
    page_ = Page::kVolume;
    volume_scrim_ = lv_obj_create(root_);
    lv_obj_remove_style_all(volume_scrim_);
    lv_obj_set_size(volume_scrim_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(volume_scrim_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(volume_scrim_, LV_OPA_50, 0);
    lv_obj_remove_flag(volume_scrim_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(volume_scrim_, [](lv_event_t* e) {
        if (lv_event_get_target_obj(e) != lv_event_get_current_target_obj(e)) return;
        static_cast<MusicApp*>(lv_event_get_user_data(e))->CloseVolume();
    }, LV_EVENT_CLICKED, this);

    auto* sheet = lv_obj_create(volume_scrim_);
    lv_obj_remove_style_all(sheet);
    lv_obj_set_size(sheet, LV_PCT(100), kSheetHeight);
    lv_obj_align(sheet, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(sheet, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sheet, 16, 0);
    lv_obj_remove_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    // The sheet absorbs taps so only the scrim above it closes the volume control.
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);

    auto* grip = CreateBox(sheet);
    lv_obj_set_size(grip, 32, 4);
    lv_obj_set_style_radius(grip, 2, 0);
    lv_obj_set_style_bg_color(grip, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(grip, LV_OPA_COVER, 0);
    lv_obj_align(grip, LV_ALIGN_TOP_MID, 0, 8);

    auto* icon = CreateGlyph(sheet, ICON_VOLUME, rodakos_theme_text_secondary(), PhoneIconFont());
    lv_obj_set_pos(icon, 16, 36);
    volume_slider_ = lv_slider_create(sheet);
    lv_obj_set_size(volume_slider_, 188, 8);
    lv_obj_set_pos(volume_slider_, 56, 42);
    lv_obj_remove_flag(volume_slider_, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_set_ext_click_area(volume_slider_, 18);
    lv_obj_set_style_bg_color(volume_slider_, rodakos_theme_bg_tertiary(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider_, rodakos_theme_primary(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider_, rodakos_theme_primary(), LV_PART_KNOB);
    lv_obj_set_style_border_color(volume_slider_, rodakos_theme_bg_secondary(), LV_PART_KNOB);
    lv_obj_set_style_border_width(volume_slider_, 4, LV_PART_KNOB);
    lv_obj_set_style_pad_all(volume_slider_, 4, LV_PART_KNOB);
    lv_obj_set_style_anim_duration(volume_slider_, 0, 0);
    lv_slider_set_range(volume_slider_, 0, 100);
    SetEnabled(volume_slider_, music_player_ != nullptr);
    lv_obj_add_event_cb(volume_slider_, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->ApplyVolume();
    }, LV_EVENT_VALUE_CHANGED, this);
    volume_value_label_ = CreateLabel(sheet, &lv_font_montserrat_26, rodakos_theme_text_primary());
    lv_obj_align(volume_value_label_, LV_ALIGN_TOP_RIGHT, -16, 32);

    SetEnabled(volume_button_, false);
    lv_obj_set_style_opa(volume_button_, LV_OPA_COVER, LV_STATE_DISABLED);
    RefreshState();
}

void MusicApp::CloseVolume() {
    if (volume_scrim_ == nullptr) return;
    lv_obj_delete(volume_scrim_);
    volume_scrim_ = volume_slider_ = volume_value_label_ = nullptr;
    page_ = Page::kPlayer;
    lv_obj_set_style_opa(volume_button_, LV_OPA_40, LV_STATE_DISABLED);
    SetEnabled(volume_button_, true);
    RefreshState();
}

void MusicApp::ApplyVolume() {
    if (music_player_ == nullptr || volume_slider_ == nullptr) return;
    const bool applied = music_player_->SetVolume(lv_slider_get_value(volume_slider_));
    DisplayVolume(music_player_->volume());
    if (!applied) ui_->ShowToastUnlocked("音量设置失败");
}

void MusicApp::DisplayVolume(int value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%d", value);
    SetText(volume_chip_label_, text);
    if (volume_slider_ == nullptr) return;
    if (lv_slider_get_value(volume_slider_) != value) lv_slider_set_value(volume_slider_, value, LV_ANIM_OFF);
    std::snprintf(text, sizeof(text), "%d%%", value);
    SetText(volume_value_label_, text);
}

void MusicApp::CreateSongs() {
    CreateAppHeader(root_, "歌曲", [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->OnBackRequested();
    }, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->RefreshLibrary();
    }, this, &songs_title_, ICON_REFRESH, &refresh_library_button_);
    lv_obj_set_style_opa(refresh_library_button_, LV_OPA_40, LV_STATE_DISABLED);

    track_list_ = lv_obj_create(root_);
    lv_obj_remove_style_all(track_list_);
    lv_obj_set_size(track_list_, LV_PCT(100), kListHeight);
    lv_obj_set_pos(track_list_, 0, kHeader);
    lv_obj_set_scroll_dir(track_list_, LV_DIR_VER);
    lv_obj_remove_flag(track_list_, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(track_list_, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_width(track_list_, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(track_list_, rodakos_theme_primary(), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(track_list_, LV_OPA_60, LV_PART_SCROLLBAR);

    // The extent keeps the full scroll range while only the row slots exist.
    list_extent_ = CreateBox(track_list_);
    lv_obj_set_size(list_extent_, 1, 0);

    for (auto& row : song_rows_) {
        row.button = lv_button_create(track_list_);
        lv_obj_remove_style_all(row.button);
        lv_obj_set_size(row.button, LV_PCT(100), kRowHeight);
        lv_obj_remove_flag(row.button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row.button, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_style_bg_opa(row.button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row.button, rodakos_theme_bg_primary(), 0);
        lv_obj_set_style_bg_color(row.button, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_set_style_border_side(row.button, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row.button, 1, 0);
        lv_obj_set_style_border_color(row.button, rodakos_theme_divider(), 0);
        row.lead = CreateLabel(row.button, &phone_font_14, rodakos_theme_text_tertiary());
        lv_obj_set_width(row.lead, 24);
        lv_obj_align(row.lead, LV_ALIGN_LEFT_MID, 14, 0);
        row.title = CreateLabel(row.button, &phone_font_14, rodakos_theme_text_primary());
        lv_label_set_long_mode(row.title, LV_LABEL_LONG_DOT);
        lv_obj_set_size(row.title, 168, lv_font_get_line_height(&phone_font_14));
        lv_obj_align(row.title, LV_ALIGN_LEFT_MID, 44, 0);
        row.artist = CreateLabel(row.button, &phone_font_14, rodakos_theme_text_tertiary());
        lv_label_set_long_mode(row.artist, LV_LABEL_LONG_DOT);
        lv_obj_set_size(row.artist, 88, lv_font_get_line_height(&phone_font_14));
        lv_obj_set_style_text_align(row.artist, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(row.artist, LV_ALIGN_RIGHT_MID, -16, 0);
        const auto select = [](lv_event_t* e) {
            auto* self = static_cast<MusicApp*>(lv_event_get_user_data(e));
            self->SelectSong(lv_event_get_target_obj(e), lv_event_get_code(e));
        };
        lv_obj_add_event_cb(row.button, select, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(row.button, select, LV_EVENT_PRESS_LOST, this);
        lv_obj_add_event_cb(row.button, select, LV_EVENT_CLICKED, this);
    }
    lv_obj_add_event_cb(track_list_, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->BindSongRows();
    }, LV_EVENT_SCROLL, this);

    list_message_ = CreateLabel(root_, &phone_font_14, rodakos_theme_text_secondary());
    lv_obj_set_width(list_message_, 288);
    lv_obj_set_style_text_align(list_message_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(list_message_, LV_ALIGN_TOP_MID, 0, 104);
    list_retry_button_ = CreateFlatButton(root_, 120, 40, 10, lv_color_darken(rodakos_theme_primary(), 40));
    lv_obj_set_style_bg_color(list_retry_button_, rodakos_theme_primary(), 0);
    lv_obj_set_style_bg_opa(list_retry_button_, LV_OPA_COVER, 0);
    lv_obj_align(list_retry_button_, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_flex_flow(list_retry_button_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(list_retry_button_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(list_retry_button_, 6, 0);
    CreateGlyph(list_retry_button_, ICON_REFRESH, ui_->theme().accent_text, PhoneIconFont());
    lv_label_set_text(CreateLabel(list_retry_button_, &phone_font_14, ui_->theme().accent_text), "重试");
    lv_obj_add_event_cb(list_retry_button_, [](lv_event_t* e) {
        static_cast<MusicApp*>(lv_event_get_user_data(e))->RefreshLibrary();
    }, LV_EVENT_CLICKED, this);
}

void MusicApp::RefreshLibrary() {
    if (music_player_ == nullptr) return;
    music_player_->RequestLibraryScan();
    RefreshState();
}

void MusicApp::PressPlay() {
    if (recovery_ == Recovery::kLibrary) {
        RefreshLibrary();
        return;
    }
    if (recovery_ == Recovery::kPlayback) {
        RetryPlayback();
        return;
    }
    if (music_player_ != nullptr) music_player_->TogglePlayPause();
    RefreshState();
}

void MusicApp::RetryPlayback() {
    if (music_player_ == nullptr || recovery_ != Recovery::kPlayback) return;
    const auto state = music_player_->GetState();
    if (state.audio.status == AudioPlaybackStatus::kError && !state.audio.file_path.empty()) {
        music_player_->RetryTrack(state.audio.file_path);
    } else if (state.audio.status == AudioPlaybackStatus::kPaused) {
        music_player_->TogglePlayPause();
    } else if (playback_error_revision_ == state.library_revision &&
               !state.operation_error.empty() && !state.audio.file_path.empty()) {
        music_player_->RetryTrack(state.audio.file_path);
    } else if (playback_error_revision_ == state.library_revision && state.current_index >= 0 &&
               !state.operation_error.empty() && state.library_status == MusicLibraryStatus::kReady) {
        music_player_->PlayTrack(static_cast<size_t>(state.current_index), playback_error_revision_);
    } else {
        music_player_->TogglePlayPause();
    }
    RefreshState();
}

void MusicApp::RefreshState() {
    if (root_ == nullptr || paused_) return;
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return;

    auto state = music_player_ != nullptr ? music_player_->GetState() : rodakos::MusicPlayerState{};
    if (music_player_ == nullptr) state.library_status = MusicLibraryStatus::kServiceUnavailable;
    if (page_ == Page::kSongs) RefreshSongs(state);
    else RefreshPlayer(state);
}

void MusicApp::RefreshPlayer(const rodakos::MusicPlayerState& state) {
    const auto& audio = state.audio;
    const bool active = IsActive(audio.status);
    const bool library_ready = state.library_status == MusicLibraryStatus::kReady;
    const bool scanning = state.library_status == MusicLibraryStatus::kScanning;
    const bool paused = audio.status == AudioPlaybackStatus::kPaused;
    const std::string& full_title = audio.title.empty() ? state.current_title : audio.title;
    std::string song;
    std::string artist;
    SplitTitle(full_title, song, artist);

    const auto previous_recovery = recovery_;
    recovery_ = Recovery::kNone;
    const char* error = nullptr;
    if (audio.status == AudioPlaybackStatus::kError ||
        (library_ready && !state.operation_error.empty() && !WorkerUnavailable(state))) {
        error = PlaybackError(state.operation_error.empty() || WorkerUnavailable(state)
                                  ? audio.message : state.operation_error);
        // A rejected tap while a track plays or is paused leaves that track in charge; turning the
        // play key into Retry would restart the song on the next tap.
        if (audio.status == AudioPlaybackStatus::kError || (!active && !paused)) {
            recovery_ = Recovery::kPlayback;
            if (previous_recovery != Recovery::kPlayback) playback_error_revision_ = state.library_revision;
        }
    } else if (WorkerUnavailable(state)) {
        recovery_ = Recovery::kLibrary;
        error = "曲库服务无法启动";
    }
    const bool no_library = !library_ready && !active && !paused && recovery_ == Recovery::kNone;
    if (no_library && !scanning) recovery_ = Recovery::kLibrary;

    // Title line: song, or the library state when there is nothing to play.
    if (no_library || (recovery_ == Recovery::kLibrary && full_title.empty())) {
        SetText(track_title_label_, music_player_ == nullptr ? "音乐服务不可用" : LibraryStatus(state.library_status));
    } else {
        SetText(track_title_label_, song.empty() ? "请选择歌曲" : song.c_str());
    }

    // Detail line: an error in the error color, else the artist or a library hint.
    const bool show_error = error != nullptr;
    lv_obj_set_style_text_color(detail_label_, show_error ? rodakos_theme_error() : rodakos_theme_text_secondary(), 0);
    if (show_error) SetText(detail_label_, error);
    else if (no_library) SetText(detail_label_, LibraryHint(state.library_status));
    else SetText(detail_label_, artist.c_str());

    const bool empty_cover = no_library || recovery_ == Recovery::kLibrary;
    const lv_color_t media = rodakos_theme_icon_color(1);
    lv_obj_set_style_bg_color(cover_, empty_cover ? rodakos_theme_bg_tertiary() : media, 0);
    lv_obj_set_style_bg_grad_color(cover_, empty_cover ? rodakos_theme_bg_tertiary() : lv_color_darken(media, 90), 0);
    lv_obj_set_style_text_color(cover_icon_, empty_cover ? rodakos_theme_text_tertiary() : lv_color_white(), 0);
    lv_obj_set_style_opa(cover_, recovery_ == Recovery::kPlayback ? LV_OPA_40 : LV_OPA_COVER, 0);

    SetVisible(wake_hint_, active);

    const bool repeat_one = state.mode == MusicPlaybackMode::kRepeatOne;
    const bool shuffle = state.mode == MusicPlaybackMode::kShuffle;
    SetText(mode_icon_, shuffle ? ICON_SHUFFLE : ICON_REPEAT);
    lv_obj_set_style_text_color(mode_icon_, shuffle || repeat_one ? rodakos_theme_primary() : rodakos_theme_text_primary(), 0);
    SetVisible(mode_badge_, repeat_one);
    DisplayVolume(audio.volume);
    SetEnabled(mode_button_, !empty_cover);
    if (page_ != Page::kVolume) SetEnabled(volume_button_, true);
    SetEnabled(stop_button_, active || paused || audio.status == AudioPlaybackStatus::kCompleted);
    SetEnabled(songs_button_, true);

    const bool timed = !empty_cover && audio.duration_ms > 0;
    const int32_t permille = timed ? static_cast<int32_t>(std::min<uint64_t>(
        1000, static_cast<uint64_t>(audio.position_ms) * 1000 / audio.duration_ms)) : 0;
    if (lv_bar_get_value(progress_bar_) != permille) lv_bar_set_value(progress_bar_, permille, LV_ANIM_OFF);
    char text[16];
    FormatTime(text, sizeof(text), timed ? audio.position_ms : 0);
    SetText(elapsed_label_, text);
    if (timed) FormatTime(text, sizeof(text), audio.duration_ms);
    SetText(duration_label_, timed ? text : "--:--");
    SetVisible(elapsed_label_, !empty_cover);
    SetVisible(duration_label_, !empty_cover);

    const bool show_play = recovery_ == Recovery::kNone && !active;
    const char* play_icon = recovery_ != Recovery::kNone ? ICON_REFRESH : active ? ICON_PAUSE_FILLED : ICON_PLAY_FILLED;
    SetText(play_icon_, play_icon);
    // The play triangle's visual center sits left of its box; nudge it to look centered.
    lv_obj_align(play_icon_, LV_ALIGN_CENTER, show_play ? 2 : 0, 0);
    SetEnabled(play_button_, !scanning && music_player_ != nullptr);
    SetEnabled(previous_button_, library_ready);
    SetEnabled(next_button_, library_ready);
}

void MusicApp::RefreshSongs(const rodakos::MusicPlayerState& state) {
    if (displayed_library_revision_ != state.library_revision && music_player_ != nullptr) {
        tracks_ = music_player_->GetTracks(&displayed_library_revision_);
        lv_obj_scroll_to_y(track_list_, 0, LV_ANIM_OFF);
    }
    const bool ready = state.library_status == MusicLibraryStatus::kReady && !WorkerUnavailable(state);
    const bool scanning = state.library_status == MusicLibraryStatus::kScanning;
    const bool error = WorkerUnavailable(state) ||
                       (!ready && !scanning && state.library_status != MusicLibraryStatus::kEmpty);
    char title[24];
    if (ready) std::snprintf(title, sizeof(title), "歌曲 · %zu", tracks_.size());
    else std::snprintf(title, sizeof(title), "歌曲");
    SetText(songs_title_, title);
    SetEnabled(refresh_library_button_, !scanning);
    SetVisible(track_list_, ready);
    SetVisible(list_message_, !ready);
    SetVisible(list_retry_button_, error && music_player_ != nullptr);
    if (!ready) {
        for (auto& row : song_rows_) row.pressed = false;
        if (WorkerUnavailable(state)) {
            SetText(list_message_, "曲库服务无法启动");
        } else {
            std::string message = LibraryStatus(state.library_status);
            const char* hint = LibraryHint(state.library_status);
            if (hint[0] != '\0') message.append("\n").append(hint);
            SetText(list_message_, message.c_str());
        }
        return;
    }

    current_song_index_ = displayed_library_revision_ == state.library_revision ? state.current_index : -1;
    const auto height = static_cast<int32_t>(tracks_.size() * kRowHeight);
    if (lv_obj_get_style_height(list_extent_, 0) != height) lv_obj_set_height(list_extent_, height);
    BindSongRows();
}

void MusicApp::BindSongRows() {
    const size_t first = static_cast<size_t>(std::max<int32_t>(0, lv_obj_get_scroll_y(track_list_)) / kRowHeight);
    for (size_t slot = 0; slot < song_rows_.size(); ++slot) {
        auto& row = song_rows_[slot];
        const size_t index = first + slot;
        SetVisible(row.button, index < tracks_.size());
        if (index >= tracks_.size()) {
            row.pressed = false;
            row.revision = 0;
            continue;
        }

        const bool current = current_song_index_ >= 0 && index == static_cast<size_t>(current_song_index_);
        if (row.index != index || row.revision != displayed_library_revision_ || row.current != current) {
            if (row.index != index || row.revision != displayed_library_revision_) row.pressed = false;
            row.index = index;
            row.revision = displayed_library_revision_;
            row.current = current;
            lv_obj_set_pos(row.button, 0, static_cast<int32_t>(index * kRowHeight));
            std::string song;
            std::string artist;
            SplitTitle(tracks_[index].title, song, artist);
            SetText(row.title, song.c_str());
            SetText(row.artist, artist.c_str());
            if (current) {
                SetText(row.lead, ICON_PLAY);
                lv_obj_set_style_text_font(row.lead, PhoneIconFont(), 0);
                lv_obj_set_style_text_color(row.lead, rodakos_theme_primary(), 0);
            } else {
                char number[24];
                std::snprintf(number, sizeof(number), "%zu", index + 1);
                lv_obj_set_style_text_font(row.lead, &phone_font_14, 0);
                lv_obj_set_style_text_color(row.lead, rodakos_theme_text_tertiary(), 0);
                SetText(row.lead, number);
            }
            lv_obj_set_style_text_color(row.title, current ? rodakos_theme_primary() : rodakos_theme_text_primary(), 0);
            lv_obj_set_style_bg_color(row.button, current ? rodakos_theme_bg_secondary() : rodakos_theme_bg_primary(), 0);
        }
    }
}

void MusicApp::SelectSong(lv_obj_t* button, lv_event_code_t event) {
    if (music_player_ == nullptr) return;
    auto row = std::find_if(song_rows_.begin(), song_rows_.end(),
                            [button](const auto& slot) { return slot.button == button; });
    if (row == song_rows_.end()) return;
    if (event == LV_EVENT_PRESS_LOST) {
        row->pressed = false;
        return;
    }
    const auto state = music_player_->GetState();
    const bool valid = row->revision != 0 && row->revision == state.library_revision &&
                       state.library_status == MusicLibraryStatus::kReady && row->index < tracks_.size();
    if (event == LV_EVENT_PRESSED) {
        row->pressed = valid;
        return;
    }

    const bool select = row->pressed && valid;
    row->pressed = false;
    if (select && music_player_->PlayTrack(row->index, row->revision)) {
        ShowPage(Page::kPlayer);
        return;
    }
    if (select) ui_->ShowToastUnlocked(PlaybackError(music_player_->GetState().operation_error));
    RefreshState();
}

void RegisterMusicApp(PhoneAppRegistry& registry) {
    PhoneAppDescriptor descriptor;
    descriptor.id = "music";
    descriptor.title = "Music";
    descriptor.icon = ICON_MUSIC;
    descriptor.category = PhoneAppCategory::kMedia;
    descriptor.capabilities = PhoneCapability::kStorage | PhoneCapability::kAudioPlayback;
    descriptor.create = []() -> std::unique_ptr<PhoneApp> {
        return std::make_unique<MusicApp>();
    };
    registry.Register(std::move(descriptor));
}
