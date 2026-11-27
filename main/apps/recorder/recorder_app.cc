#include "apps/recorder/recorder_app.h"

#include "phone_os/audio_output_service.h"
#include "phone_os/audio_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_components.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "phone_ui/rodakos_theme.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <memory>
#include <string>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr const char* TAG = "RecorderApp";
constexpr uint32_t kIdleRefreshMs = 500;
// While recording the level meter scrolls; 150 ms keeps it lively without redrawing every frame.
constexpr uint32_t kRecordingRefreshMs = 150;
constexpr uint32_t kToneTaskStackWords = 4096;
constexpr uint32_t kToneSampleRate = 16000;
constexpr uint32_t kToneDurationMs = 1200;
constexpr uint32_t kToneFrequencyHz = 880;
constexpr int16_t kToneAmplitude = 20000;
constexpr int kRecordingPlaybackVolume = 90;
constexpr const char* kToneOutputOwner = "recorder-tone";
constexpr lv_coord_t kListRowHeight = 48;
bool s_tone_running = false;

struct ToneTaskPayload {
    rodakos::AudioOutputService* output = nullptr;
};

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) context->navigation().ReturnHome();
}

lv_obj_t* CreatePlain(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

void SetHidden(lv_obj_t* obj, bool hidden) {
    if (obj == nullptr) return;
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void ToneTask(void* arg) {
    auto* payload = static_cast<ToneTaskPayload*>(arg);
    auto* output = payload != nullptr ? payload->output : nullptr;
    delete payload;

    if (output != nullptr && output->OpenForOwner(kToneOutputOwner, kToneSampleRate, 1, 16)) {
        const int previous_volume = output->volume();
        const bool boosted = previous_volume < kRecordingPlaybackVolume &&
                             output->SetVolume(kRecordingPlaybackVolume);
        constexpr size_t kFramesPerChunk = 240;
        int16_t samples[kFramesPerChunk] = {};
        uint32_t frame_index = 0;
        const uint32_t total_frames = (kToneSampleRate * kToneDurationMs) / 1000;
        const uint32_t half_period = kToneSampleRate / (kToneFrequencyHz * 2);

        ESP_LOGI(TAG, "Speaker test tone started: %" PRIu32 " Hz mono", kToneSampleRate);
        while (frame_index < total_frames) {
            const uint32_t frames_left = total_frames - frame_index;
            const size_t frames = frames_left < kFramesPerChunk ? frames_left : kFramesPerChunk;
            for (size_t i = 0; i < frames; ++i) {
                const uint32_t phase = half_period > 0 ? ((frame_index + i) / half_period) : 0;
                samples[i] = (phase & 1U) ? -kToneAmplitude : kToneAmplitude;
            }
            if (!output->WriteForOwner(kToneOutputOwner, samples, static_cast<int>(frames * sizeof(int16_t)))) {
                ESP_LOGW(TAG, "Speaker test tone write failed");
                break;
            }
            frame_index += frames;
        }
        output->CloseForOwner(kToneOutputOwner);
        // Keep a volume chosen elsewhere (MQTT, voice, Music) while the tone played.
        if (boosted && output->volume() == kRecordingPlaybackVolume) output->SetVolume(previous_volume);
        ESP_LOGI(TAG, "Speaker test tone ended");
    } else {
        ESP_LOGW(TAG, "Speaker test tone could not open audio output");
    }

    s_tone_running = false;
    vTaskDelete(nullptr);
}

bool IsRecordingActive(rodakos::RecordingStatus status) {
    return status == rodakos::RecordingStatus::kStarting ||
           status == rodakos::RecordingStatus::kRecording ||
           status == rodakos::RecordingStatus::kStopping;
}

bool IsPlaybackActive(rodakos::AudioPlaybackStatus status) {
    return status == rodakos::AudioPlaybackStatus::kLoading ||
           status == rodakos::AudioPlaybackStatus::kPlaying ||
           status == rodakos::AudioPlaybackStatus::kPaused;
}

std::string RecordingError(const rodakos::RecordingState& state) {
    if (state.status != rodakos::RecordingStatus::kError) return {};
    if (!state.last_error.empty()) return state.last_error;
    return state.message.empty() ? "Recording failed" : state.message;
}

std::string FormatDuration(uint32_t duration_ms) {
    const uint32_t total_seconds = duration_ms / 1000;
    const uint32_t minutes = total_seconds / 60;
    const uint32_t seconds = total_seconds % 60;
    char buffer[24] = {};
    if (minutes >= 60) {
        std::snprintf(buffer, sizeof(buffer), "%lu:%02lu:%02lu", static_cast<unsigned long>(minutes / 60),
                      static_cast<unsigned long>(minutes % 60), static_cast<unsigned long>(seconds));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lu:%02lu", static_cast<unsigned long>(minutes),
                      static_cast<unsigned long>(seconds));
    }
    return buffer;
}

std::string FormatSize(size_t bytes) {
    char buffer[24] = {};
    if (bytes < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%u B", static_cast<unsigned>(bytes));
    } else if (bytes < 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%u KB", static_cast<unsigned>((bytes + 512) / 1024));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buffer;
}

// REC_20261127_101508 -> "11月27日 10:15". A same-second recording gets a "_0001" suffix from
// the service; it keeps that number so two rows never read the same. Any other name
// (REC_BOOT_…) is shown as is.
std::string DisplayTitle(const std::string& title) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, consumed = 0;
    if (std::sscanf(title.c_str(), "REC_%4d%2d%2d_%2d%2d%2d%n", &year, &month, &day, &hour, &minute, &second,
                    &consumed) == 6 &&
        consumed == 19 && month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour < 24 && minute < 60) {
        int suffix = 0, suffix_len = 0;
        const char* rest = title.c_str() + consumed;
        if (*rest != '\0' && !(std::sscanf(rest, "_%4d%n", &suffix, &suffix_len) == 1 && suffix_len == 5 &&
                                rest[suffix_len] == '\0')) {
            return title;
        }
        char buffer[40];
        if (suffix > 0) {
            std::snprintf(buffer, sizeof(buffer), "%d月%d日 %02d:%02d (%d)", month, day, hour, minute, suffix + 1);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%d月%d日 %02d:%02d", month, day, hour, minute);
        }
        return buffer;
    }
    return title;
}

// User-facing Chinese text for the service's fixed error strings. The original text stays on
// the detail line so a technical reason is never lost.
const char* RecordingErrorTitle(const std::string& error) {
    if (error.find("WAV header") != std::string::npos || error.find("flush") != std::string::npos ||
        error.find("close") != std::string::npos || error.find("seek") != std::string::npos) {
        return "无法完成文件保存";
    }
    if (error.find("Audio") != std::string::npos || error.find("ADC") != std::string::npos) {
        return "麦克风读取失败";
    }
    if (error.find("SD") != std::string::npos || error.find("card") != std::string::npos ||
        error.find("mount") != std::string::npos) {
        return "读不到 SD 卡";
    }
    if (error.find("size limit") != std::string::npos) return "录音已达文件大小上限";
    return "录音没有完成";
}

}  // namespace

bool RecorderApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    recording_ = context.services().recording();
    audio_ = context.services().audio();
    audio_output_ = context.services().audio_output();

    if (recording_ != nullptr) recording_->RefreshRecordings();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;

    CreateUi();
    RebuildRecordingList();
    RefreshState();
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, kIdleRefreshMs, this);
    if (refresh_timer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create recorder refresh timer");
        return false;
    }
    ESP_LOGI(TAG, "Recorder app created with %zu recordings", displayed_recordings_.size());
    return true;
}

void RecorderApp::OnResume() {
    if (ui_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return;
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    }
    if (refresh_timer_ != nullptr) {
        lv_timer_resume(refresh_timer_);
        lv_timer_reset(refresh_timer_);
    }
    RefreshState();
}

void RecorderApp::OnPause() {
    if (ui_ == nullptr) return;
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return;
    if (refresh_timer_ != nullptr) lv_timer_pause(refresh_timer_);
    if (root_ != nullptr && lv_obj_is_valid(root_)) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void RecorderApp::OnDestroy() {
    if (ui_ != nullptr) {
        // The refresh timer must be removed before its App userdata is released.
        PhoneUiLock lock(*ui_, 0);
        if (lock.locked()) {
            if (context_ != nullptr) lv_async_call_cancel(DeferReturnHome, context_);
            DestroyUi();
        }
    }
    if (recording_ != nullptr) recording_->Stop();
    RestorePlaybackVolume();
    context_ = nullptr;
    ui_ = nullptr;
    recording_ = nullptr;
    audio_ = nullptr;
    audio_output_ = nullptr;
    displayed_recordings_.clear();
}

bool RecorderApp::OnThemeChanged(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    recording_ = context.services().recording();
    audio_ = context.services().audio();
    audio_output_ = context.services().audio_output();

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) return false;
    const bool was_hidden = root_ != nullptr && lv_obj_is_valid(root_) && lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    DestroyUi();
    CreateUi();
    RebuildRecordingList();
    RefreshState();
    if (was_hidden && root_ != nullptr) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, fast_refresh_ ? kRecordingRefreshMs : kIdleRefreshMs, this);
    if (refresh_timer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create recorder refresh timer after theme change");
        return false;
    }
    return true;
}

bool RecorderApp::OnBackRequested() {
    if (delete_layer_ != nullptr) {
        CloseDeleteDialog();
        return true;
    }
    if (view_ == View::kList) {
        ShowView(View::kRecord);
        return true;
    }
    NavigateHome();
    return true;
}

void RecorderApp::CreateUi() {
    root_ = CreatePlain(ui_->screen());
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);

    auto* header = CreateAppHeader(root_, "录音", [](lv_event_t* e) {
        static_cast<RecorderApp*>(lv_event_get_user_data(e))->OnBackRequested();
    }, [](lv_event_t* e) {
        auto* self = static_cast<RecorderApp*>(lv_event_get_user_data(e));
        if (self->view_ == View::kRecord) self->ShowView(View::kList);
    }, this, &title_label_, ICON_LIST, &list_button_);

    // Same "listening off" mark as Music: wake gives the shared I2S bus up while recording.
    wake_hint_ = CreateText(header, ICON_MICROPHONE, PhoneIconFont(), rodakos_theme_text_tertiary());
    lv_obj_align(wake_hint_, LV_ALIGN_RIGHT_MID, -50, 0);
    auto* slash = CreatePlain(wake_hint_);
    lv_obj_add_flag(slash, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(slash, 2, 24);
    lv_obj_set_style_bg_color(slash, rodakos_theme_text_tertiary(), 0);
    lv_obj_set_style_bg_opa(slash, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(slash, 1, 0);
    lv_obj_set_style_transform_rotation(slash, 450, 0);
    lv_obj_set_style_transform_pivot_x(slash, 1, 0);
    lv_obj_set_style_transform_pivot_y(slash, 12, 0);
    lv_obj_align(slash, LV_ALIGN_CENTER, 0, 0);
    SetHidden(wake_hint_, true);

    CreateRecordView();
    CreateListView();
    ShowView(view_);
}

void RecorderApp::CreateRecordView() {
    record_view_ = CreatePlain(root_);
    lv_obj_set_size(record_view_, 320, 200);
    lv_obj_set_pos(record_view_, 0, kRodakosAppHeaderHeight);

    auto* status_row = CreatePlain(record_view_);
    lv_obj_set_size(status_row, 300, 22);
    lv_obj_set_pos(status_row, 10, 8);
    lv_obj_set_flex_flow(status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status_row, 6, 0);
    status_dot_ = CreatePlain(status_row);
    lv_obj_set_size(status_dot_, 8, 8);
    lv_obj_set_style_radius(status_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(status_dot_, LV_OPA_COVER, 0);
    status_label_ = CreateText(status_row, "", &phone_font_14, rodakos_theme_text_secondary());
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(status_label_, 280, 0);

    duration_label_ = CreateText(record_view_, "0:00", &lv_font_montserrat_26, rodakos_theme_text_primary());
    lv_obj_set_width(duration_label_, 300);
    lv_obj_set_style_text_align(duration_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(duration_label_, 10, 34);

    stage_ = CreatePlain(record_view_);
    lv_obj_set_size(stage_, 296, 48);
    lv_obj_set_pos(stage_, 12, 72);
    lv_obj_set_style_bg_color(stage_, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(stage_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(stage_, 10, 0);
    lv_obj_set_style_clip_corner(stage_, true, 0);

    // 20 bars, 8 px wide, across 260 px; heights follow the input level.
    for (size_t i = 0; i < kLevelBars; ++i) {
        auto* bar = CreatePlain(stage_);
        lv_obj_set_size(bar, 8, 4);
        lv_obj_set_pos(bar, static_cast<lv_coord_t>(18 + i * 13), 22);
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_set_style_bg_color(bar, rodakos_theme_bg_tertiary(), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        level_bars_[i] = bar;
    }

    info_box_ = CreatePlain(stage_);
    lv_obj_set_size(info_box_, 296, 48);
    info_icon_ = CreateText(info_box_, ICON_MICROPHONE, PhoneIconFont(), rodakos_theme_primary());
    lv_obj_set_pos(info_icon_, 12, 14);
    info_title_label_ = CreateText(info_box_, "", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_size(info_title_label_, 244, 20);
    lv_label_set_long_mode(info_title_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(info_title_label_, 40, 5);
    info_detail_label_ = CreateText(info_box_, "", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_size(info_detail_label_, 244, 16);
    lv_label_set_long_mode(info_detail_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(info_detail_label_, 40, 27);
    info_progress_ = CreatePlain(info_box_);
    lv_obj_set_size(info_progress_, 296, 3);
    lv_obj_set_pos(info_progress_, 0, 45);
    lv_obj_set_style_bg_color(info_progress_, rodakos_theme_bg_tertiary(), 0);
    lv_obj_set_style_bg_opa(info_progress_, LV_OPA_COVER, 0);
    info_progress_fill_ = CreatePlain(info_progress_);
    lv_obj_set_size(info_progress_fill_, 0, 3);
    lv_obj_set_style_bg_color(info_progress_fill_, rodakos_theme_primary(), 0);
    lv_obj_set_style_bg_opa(info_progress_fill_, LV_OPA_COVER, 0);

    // Shutter-ring record button: a ring with a red dot that turns into a rounded square.
    record_button_ = lv_btn_create(record_view_);
    lv_obj_remove_style_all(record_button_);
    lv_obj_set_size(record_button_, 52, 52);
    lv_obj_set_pos(record_button_, 134, 134);
    lv_obj_set_style_radius(record_button_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(record_button_, 3, 0);
    lv_obj_set_style_border_color(record_button_, rodakos_theme_text_primary(), 0);
    lv_obj_set_style_opa(record_button_, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_ext_click_area(record_button_, 6);
    lv_obj_clear_flag(record_button_, LV_OBJ_FLAG_SCROLLABLE);
    record_core_ = CreatePlain(record_button_);
    lv_obj_set_style_bg_color(record_core_, rodakos_theme_error(), 0);
    lv_obj_set_style_bg_opa(record_core_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(record_core_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(record_button_, [](lv_event_t* e) {
        static_cast<RecorderApp*>(lv_event_get_user_data(e))->ToggleRecording();
    }, LV_EVENT_CLICKED, this);

    auto make_chip = [&](lv_coord_t x, const char* icon, const char* text, lv_obj_t** icon_out,
                         lv_obj_t** text_out) {
        auto* chip = lv_btn_create(record_view_);
        lv_obj_remove_style_all(chip);
        lv_obj_set_size(chip, 78, 36);
        lv_obj_set_pos(chip, x, 142);
        lv_obj_set_style_radius(chip, 8, 0);
        lv_obj_set_style_bg_color(chip, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(chip, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_set_style_opa(chip, LV_OPA_40, LV_STATE_DISABLED);
        lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(chip, 5, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        auto* glyph = CreateText(chip, icon, PhoneIconFont(), rodakos_theme_text_primary());
        auto* label = CreateText(chip, text, &phone_font_14, rodakos_theme_text_primary());
        if (icon_out != nullptr) *icon_out = glyph;
        if (text_out != nullptr) *text_out = label;
        return chip;
    };
    tone_button_ = make_chip(26, ICON_VOLUME, "试音", nullptr, nullptr);
    lv_obj_add_event_cb(tone_button_, [](lv_event_t* e) {
        static_cast<RecorderApp*>(lv_event_get_user_data(e))->StartSpeakerTest();
    }, LV_EVENT_CLICKED, this);
    replay_button_ = make_chip(216, ICON_PLAY, "回放", &replay_icon_, &replay_label_);
    lv_obj_add_event_cb(replay_button_, [](lv_event_t* e) {
        static_cast<RecorderApp*>(lv_event_get_user_data(e))->ToggleLatestPlayback();
    }, LV_EVENT_CLICKED, this);
}

void RecorderApp::CreateListView() {
    list_view_ = CreatePlain(root_);
    lv_obj_set_size(list_view_, 320, 200);
    lv_obj_set_pos(list_view_, 0, kRodakosAppHeaderHeight);

    list_ = CreatePlain(list_view_);
    lv_obj_set_size(list_, 320, 200);
    lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);

    list_status_ = CreatePlain(list_view_);
    lv_obj_set_size(list_status_, 320, 200);
}

void RecorderApp::ShowView(View view) {
    view_ = view;
    SetHidden(record_view_, view != View::kRecord);
    SetHidden(list_view_, view != View::kList);
    SetHidden(list_button_, view != View::kRecord);
    if (view == View::kList) RebuildRecordingList();
    RefreshState();
}

void RecorderApp::DestroyUi() {
    if (refresh_timer_ != nullptr) {
        lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
    }
    if (delete_layer_ != nullptr && lv_obj_is_valid(delete_layer_)) lv_obj_delete(delete_layer_);
    if (root_ != nullptr && lv_obj_is_valid(root_)) lv_obj_delete(root_);
    ResetUiPointers();
}

void RecorderApp::ResetUiPointers() {
    root_ = title_label_ = list_button_ = wake_hint_ = nullptr;
    record_view_ = status_dot_ = status_label_ = duration_label_ = stage_ = nullptr;
    level_bars_.fill(nullptr);
    info_box_ = info_icon_ = info_title_label_ = info_detail_label_ = info_progress_ = info_progress_fill_ = nullptr;
    record_button_ = record_core_ = tone_button_ = replay_button_ = replay_icon_ = replay_label_ = nullptr;
    list_view_ = list_ = list_status_ = delete_layer_ = nullptr;
}

bool RecorderApp::IsPlaying(const rodakos::RecordingEntry& entry) const {
    if (audio_ == nullptr) return false;
    const auto state = audio_->GetState();
    return state.file_path == entry.full_path && IsPlaybackActive(state.status);
}

void RecorderApp::RebuildRecordingList() {
    const auto state = recording_ != nullptr ? recording_->GetState() : rodakos::RecordingState{};
    last_library_error_ = recording_ != nullptr ? state.library_error : "Recording service missing";
    last_recording_error_ = RecordingError(state);
    displayed_recordings_ =
        recording_ != nullptr ? recording_->GetRecordings() : std::vector<rodakos::RecordingEntry>{};
    if (list_ == nullptr) return;

    lv_obj_clean(list_);
    lv_obj_clean(list_status_);
    if (title_label_ != nullptr && view_ == View::kList) {
        if (last_library_error_.empty()) {
            lv_label_set_text_fmt(title_label_, "录音 · %u", static_cast<unsigned>(displayed_recordings_.size()));
        } else {
            lv_label_set_text(title_label_, "录音列表");
        }
    }

    // A failed scan is never shown as an empty list (docs/media-save.md).
    if (!last_library_error_.empty() || displayed_recordings_.empty()) {
        SetHidden(list_, true);
        SetHidden(list_status_, false);
        const bool failed = !last_library_error_.empty();
        const bool card = failed && last_library_error_ != "Failed to list recordings" &&
                          last_library_error_ != "Recording service missing";
        auto* icon = CreateText(list_status_, failed ? (card ? ICON_STORAGE : ICON_FOLDER) : ICON_MICROPHONE,
                                PhoneIconFontLarge(), rodakos_theme_text_tertiary());
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 26);
        auto* title = CreateText(list_status_,
                                 recording_ == nullptr ? "录音服务不可用"
                                 : card ? "读不到 SD 卡"
                                 : failed ? "无法读取录音列表" : "还没有录音",
                                 &phone_font_14, rodakos_theme_text_primary());
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 66);
        auto* hint = CreateText(list_status_,
                                card ? "请插入或重新插拔 SD 卡后重试"
                                : failed ? last_library_error_.c_str() : "回到录音页按红钮开始录制",
                                &phone_font_12, rodakos_theme_text_secondary());
        lv_obj_set_width(hint, 288);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 90);
        auto* button = lv_btn_create(list_status_);
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, 120, 38);
        lv_obj_align(button, LV_ALIGN_TOP_MID, 0, 122);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_bg_color(button, failed ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(button, 6, 0);
        const lv_color_t fg = failed ? rodakos_theme_on_primary() : rodakos_theme_text_primary();
        if (failed) CreateText(button, ICON_REFRESH, PhoneIconFont(), fg);
        CreateText(button, failed ? "重试" : "返回录音", &phone_font_14, fg);
        lv_obj_add_event_cb(button, [](lv_event_t* e) {
            auto* self = static_cast<RecorderApp*>(lv_event_get_user_data(e));
            if (!self->last_library_error_.empty()) {
                if (self->recording_ != nullptr) self->recording_->RefreshRecordings();
                self->RebuildRecordingList();
                self->RefreshState();
            } else {
                self->ShowView(View::kRecord);
            }
        }, LV_EVENT_CLICKED, this);
        return;
    }

    SetHidden(list_, false);
    SetHidden(list_status_, true);
    for (size_t i = 0; i < displayed_recordings_.size(); ++i) {
        const auto& entry = displayed_recordings_[i];
        auto* row = lv_btn_create(list_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 320, kListRowHeight);
        lv_obj_set_pos(row, 0, static_cast<lv_coord_t>(i * kListRowHeight));
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, rodakos_theme_divider(), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));

        auto* icon = CreateText(row, ICON_PLAY, PhoneIconFont(), rodakos_theme_primary());
        lv_obj_set_pos(icon, 12, 14);
        auto* title = CreateText(row, DisplayTitle(entry.title).c_str(), &phone_font_14, rodakos_theme_text_primary());
        lv_obj_set_size(title, 224, 20);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(title, 42, 5);
        const std::string detail = FormatDuration(entry.duration_ms) + " · " + FormatSize(entry.size);
        auto* meta = CreateText(row, detail.c_str(), &phone_font_12, rodakos_theme_text_tertiary());
        lv_obj_set_size(meta, 224, 16);
        lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(meta, 42, 27);

        auto* del = lv_btn_create(row);
        lv_obj_remove_style_all(del);
        lv_obj_set_size(del, 36, 36);
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_set_style_radius(del, 6, 0);
        lv_obj_set_style_bg_color(del, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(del, LV_OPA_COVER, LV_STATE_PRESSED);
        auto* trash = CreateText(del, ICON_TRASH, PhoneIconFont(), rodakos_theme_text_tertiary());
        lv_obj_center(trash);
        lv_obj_set_user_data(del, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(del, [](lv_event_t* e) {
            auto* self = static_cast<RecorderApp*>(lv_event_get_user_data(e));
            auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
            self->AskDelete(static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target))));
            lv_event_stop_bubbling(e);
        }, LV_EVENT_CLICKED, this);

        auto* bar = CreatePlain(row);
        lv_obj_set_size(bar, 320, 2);
        lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_set_style_bg_color(bar, rodakos_theme_bg_tertiary(), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        auto* fill = CreatePlain(bar);
        lv_obj_set_size(fill, 0, 2);
        lv_obj_set_style_bg_color(fill, rodakos_theme_primary(), 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);

        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            auto* self = static_cast<RecorderApp*>(lv_event_get_user_data(e));
            const auto index = reinterpret_cast<uintptr_t>(
                lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(e))));
            self->PlayRecording(static_cast<size_t>(index));
        }, LV_EVENT_CLICKED, this);
    }
    RefreshListPlayback();
}

void RecorderApp::RefreshListPlayback() {
    if (list_ == nullptr || lv_obj_has_flag(list_, LV_OBJ_FLAG_HIDDEN)) return;
    const auto audio_state = audio_ != nullptr ? audio_->GetState() : rodakos::AudioPlaybackState{};
    const uint32_t rows = lv_obj_get_child_count(list_);
    for (uint32_t i = 0; i < rows && i < displayed_recordings_.size(); ++i) {
        auto* row = lv_obj_get_child(list_, static_cast<int32_t>(i));
        auto* icon = lv_obj_get_child(row, 0);
        auto* title = lv_obj_get_child(row, 1);
        auto* meta = lv_obj_get_child(row, 2);
        auto* bar = lv_obj_get_child(row, 4);
        const auto& entry = displayed_recordings_[i];
        const bool current = audio_state.file_path == entry.full_path && IsPlaybackActive(audio_state.status);
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_opa(row, current ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_label_set_text(icon, current ? ICON_STOP : ICON_PLAY);
        const lv_color_t text = current ? rodakos_theme_primary() : rodakos_theme_text_primary();
        lv_obj_set_style_text_color(title, text, 0);
        lv_obj_set_style_text_color(meta, current ? rodakos_theme_primary() : rodakos_theme_text_tertiary(), 0);
        if (current) {
            const uint32_t total = audio_state.duration_ms > 0 ? audio_state.duration_ms : entry.duration_ms;
            const std::string detail = FormatDuration(audio_state.position_ms) + " / " + FormatDuration(total) +
                                       " · " + FormatSize(entry.size);
            lv_label_set_text(meta, detail.c_str());
            const int32_t width = total > 0 ? static_cast<int32_t>(
                std::min<uint64_t>(320, static_cast<uint64_t>(audio_state.position_ms) * 320 / total)) : 0;
            lv_obj_set_width(lv_obj_get_child(bar, 0), width);
        } else {
            const std::string detail = FormatDuration(entry.duration_ms) + " · " + FormatSize(entry.size);
            lv_label_set_text(meta, detail.c_str());
            lv_obj_set_width(lv_obj_get_child(bar, 0), 0);
        }
        SetHidden(bar, !current);
    }
}

void RecorderApp::PushLevel(uint8_t level) {
    std::rotate(levels_.begin(), levels_.begin() + 1, levels_.end());
    levels_.back() = level;
}

void RecorderApp::RefreshRecordView(const rodakos::RecordingState& state) {
    if (status_label_ == nullptr) return;
    const bool active = IsRecordingActive(state.status);
    const auto recording_error = RecordingError(state);
    const bool library_error = !state.library_error.empty();

    // Status line: one short phrase with a coloured dot.
    std::string status;
    lv_color_t tone = rodakos_theme_text_tertiary();
    bool neutral = false;
    if (recording_ == nullptr) {
        status = "录音服务不可用";
        tone = rodakos_theme_error();
    } else if (!recording_error.empty()) {
        status = "录音失败";
        tone = rodakos_theme_error();
    } else if (state.status == rodakos::RecordingStatus::kStarting) {
        status = "正在启动麦克风";
        tone = rodakos_theme_primary();
    } else if (state.status == rodakos::RecordingStatus::kRecording) {
        status = "正在录音 · " + FormatSize(state.file_size);
        tone = rodakos_theme_error();
    } else if (state.status == rodakos::RecordingStatus::kStopping) {
        status = "正在保存";
        tone = rodakos_theme_primary();
    } else if (state.status == rodakos::RecordingStatus::kCompleted) {
        status = "已保存 · " + FormatDuration(state.duration_ms) + " · " + FormatSize(state.file_size);
        tone = rodakos_theme_success();
    } else if (library_error) {
        status = "准备就绪 · 列表不可用";
        neutral = true;
    } else {
        status = "准备就绪 · 已有 " + std::to_string(displayed_recordings_.size()) + " 条录音";
        neutral = true;
    }
    lv_label_set_text(status_label_, status.c_str());
    lv_obj_set_style_text_color(status_label_, neutral ? rodakos_theme_text_secondary() : tone, 0);
    lv_obj_set_style_bg_color(status_dot_, tone, 0);

    const uint32_t shown_ms = active || state.status == rodakos::RecordingStatus::kCompleted ||
                              state.status == rodakos::RecordingStatus::kError ? state.duration_ms : 0;
    lv_label_set_text(duration_label_, FormatDuration(shown_ms).c_str());
    lv_obj_set_style_text_color(duration_label_, active ? rodakos_theme_error() : rodakos_theme_text_primary(), 0);
    SetHidden(wake_hint_, !active);

    // Middle stage: live level while recording, otherwise one information line.
    if (state.status == rodakos::RecordingStatus::kRecording) PushLevel(state.input_level);
    SetHidden(info_box_, active);
    for (size_t i = 0; i < kLevelBars; ++i) {
        SetHidden(level_bars_[i], !active);
        if (!active) continue;
        const uint8_t level = levels_[i];
        const lv_coord_t height = std::max<lv_coord_t>(4, static_cast<lv_coord_t>(level * 34 / 100));
        lv_obj_set_height(level_bars_[i], height);
        lv_obj_set_y(level_bars_[i], static_cast<lv_coord_t>(7 + (34 - height) / 2));
        lv_obj_set_style_bg_color(level_bars_[i],
            level >= 95 ? rodakos_theme_error() : level >= 80 ? rodakos_theme_warning()
            : level > 0 ? rodakos_theme_primary() : rodakos_theme_bg_tertiary(), 0);
    }
    if (!active) {
        const auto audio_state = audio_ != nullptr ? audio_->GetState() : rodakos::AudioPlaybackState{};
        const rodakos::RecordingEntry* latest = displayed_recordings_.empty() ? nullptr : &displayed_recordings_.front();
        const bool replaying = latest != nullptr && audio_state.file_path == latest->full_path &&
                               IsPlaybackActive(audio_state.status);
        lv_color_t icon_color = rodakos_theme_primary();
        const char* icon = ICON_MICROPHONE;
        std::string title, detail;
        lv_color_t detail_color = rodakos_theme_text_tertiary();
        if (!recording_error.empty()) {
            icon = ICON_CLOSE;
            icon_color = rodakos_theme_error();
            title = RecordingErrorTitle(recording_error);
            detail = "点红钮重试 · " + recording_error;
            detail_color = rodakos_theme_error();
        } else if (library_error && state.status == rodakos::RecordingStatus::kCompleted) {
            // The recording itself is saved; only the folder scan afterwards failed.
            icon = ICON_REFRESH;
            icon_color = rodakos_theme_warning();
            title = "列表读取失败，录音已正常保存";
            detail = "点右上重试 · " + state.library_error;
            detail_color = rodakos_theme_warning();
        } else if (library_error) {
            icon = ICON_STORAGE;
            icon_color = rodakos_theme_warning();
            title = "录音列表不可用";
            detail = state.library_error;
            detail_color = rodakos_theme_warning();
        } else if (replaying) {
            icon = ICON_VOLUME;
            title = "正在回放 · " + DisplayTitle(latest->title);
            const uint32_t total = audio_state.duration_ms > 0 ? audio_state.duration_ms : latest->duration_ms;
            detail = FormatDuration(audio_state.position_ms) + " / " + FormatDuration(total) +
                     (restore_volume_ >= 0 ? " · 自动提高音量" : "");
            detail_color = rodakos_theme_primary();
        } else if (latest != nullptr) {
            icon = state.status == rodakos::RecordingStatus::kCompleted ? ICON_CHECK : ICON_MICROPHONE;
            icon_color = state.status == rodakos::RecordingStatus::kCompleted ? rodakos_theme_success()
                                                                              : rodakos_theme_primary();
            title = "最近 · " + DisplayTitle(latest->title);
            detail = FormatDuration(latest->duration_ms) + " · " + FormatSize(latest->size) + " · 16 kHz WAV";
        } else {
            title = "还没有录音";
            detail = "按下方红钮开始录制";
        }
        lv_label_set_text(info_icon_, icon);
        lv_obj_set_style_text_color(info_icon_, icon_color, 0);
        lv_label_set_text(info_title_label_, title.c_str());
        // LONG_DOT rewrites the label text in place; keep the full line for logs and tests.
        info_detail_text_ = detail;
        lv_label_set_text(info_detail_label_, detail.c_str());
        lv_obj_set_style_text_color(info_detail_label_, detail_color, 0);
        SetHidden(info_progress_, !replaying);
        if (replaying) {
            const uint32_t total = audio_state.duration_ms > 0 ? audio_state.duration_ms : latest->duration_ms;
            lv_obj_set_width(info_progress_fill_, total > 0 ? static_cast<int32_t>(
                std::min<uint64_t>(296, static_cast<uint64_t>(audio_state.position_ms) * 296 / total)) : 0);
        }

        const bool can_replay = latest != nullptr && audio_ != nullptr;
        if (can_replay) {
            lv_obj_remove_state(replay_button_, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(replay_button_, LV_STATE_DISABLED);
        }
        lv_label_set_text(replay_icon_, replaying ? ICON_STOP : ICON_PLAY);
        lv_label_set_text(replay_label_, replaying ? "停止" : "回放");
        lv_obj_set_style_text_color(replay_icon_, replaying ? rodakos_theme_primary() : rodakos_theme_text_primary(), 0);
        lv_obj_set_style_text_color(replay_label_, replaying ? rodakos_theme_primary() : rodakos_theme_text_primary(), 0);
    } else {
        lv_obj_add_state(replay_button_, LV_STATE_DISABLED);
    }
    if (active) {
        lv_obj_add_state(tone_button_, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(tone_button_, LV_STATE_DISABLED);
    }
    UpdateRecordButton(active, recording_ != nullptr);
}

void RecorderApp::UpdateRecordButton(bool recording, bool enabled) {
    if (record_button_ == nullptr) return;
    if (enabled) {
        lv_obj_remove_state(record_button_, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(record_button_, LV_STATE_DISABLED);
    }
    lv_obj_set_style_border_color(record_button_, recording ? rodakos_theme_error() : rodakos_theme_text_primary(), 0);
    const lv_coord_t core = recording ? 20 : 34;
    lv_obj_set_size(record_core_, core, core);
    lv_obj_set_style_radius(record_core_, recording ? 5 : LV_RADIUS_CIRCLE, 0);
    lv_obj_center(record_core_);
}

void RecorderApp::SetRefreshPeriod(bool recording) {
    if (refresh_timer_ == nullptr || fast_refresh_ == recording) return;
    fast_refresh_ = recording;
    lv_timer_set_period(refresh_timer_, recording ? kRecordingRefreshMs : kIdleRefreshMs);
    if (!recording) levels_.fill(0);
}

void RecorderApp::ToggleRecording() {
    if (recording_ == nullptr) {
        if (ui_ != nullptr) ui_->ShowToastUnlocked("录音服务不可用");
        return;
    }
    const auto state = recording_->GetState();
    if (IsRecordingActive(state.status)) {
        recording_->Stop();
        recording_->RefreshRecordings();
        RebuildRecordingList();
        RefreshState();
        return;
    }
    if (audio_ != nullptr) audio_->ReleasePlaybackHardware();
    RestorePlaybackVolume();
    levels_.fill(0);
    if (!recording_->Start()) {
        if (ui_ != nullptr) ui_->ShowToastUnlocked("无法开始录音");
    }
    RefreshState();
}

void RecorderApp::StartSpeakerTest() {
    if (audio_output_ == nullptr || s_tone_running) return;
    if (recording_ != nullptr && IsRecordingActive(recording_->GetState().status)) {
        if (ui_ != nullptr) ui_->ShowToastUnlocked("请先停止录音");
        return;
    }
    if (audio_ != nullptr) audio_->ReleasePlaybackHardware();
    // The tone task samples the current volume as the value it restores afterwards.
    RestorePlaybackVolume();

    auto* payload = new ToneTaskPayload();
    payload->output = audio_output_;
    s_tone_running = true;
    if (xTaskCreate(ToneTask, "rec_tone", kToneTaskStackWords, payload, 4, nullptr) != pdPASS) {
        s_tone_running = false;
        delete payload;
        ESP_LOGW(TAG, "Failed to create speaker test tone task");
        if (ui_ != nullptr) ui_->ShowToastUnlocked("暂时无法试音");
    }
}

void RecorderApp::ToggleLatestPlayback() {
    if (displayed_recordings_.empty()) return;
    PlayRecording(0);
    RefreshState();
}

void RecorderApp::PlayRecording(size_t index) {
    if (index >= displayed_recordings_.size()) return;
    if (recording_ != nullptr && IsRecordingActive(recording_->GetState().status)) {
        if (ui_ != nullptr) ui_->ShowToastUnlocked("请先停止录音");
        return;
    }
    if (audio_ == nullptr) {
        if (ui_ != nullptr) ui_->ShowToastUnlocked("无法播放录音");
        return;
    }
    const auto& entry = displayed_recordings_[index];
    if (IsPlaying(entry)) {
        audio_->Stop();
        RefreshListPlayback();
        return;
    }
    BoostPlaybackVolume();
    if (!audio_->PlayFile(entry.full_path, entry.title)) {
        RestorePlaybackVolume();
        if (ui_ != nullptr) ui_->ShowToastUnlocked("无法播放录音");
    }
    RefreshListPlayback();
}

void RecorderApp::AskDelete(size_t index) {
    if (index >= displayed_recordings_.size() || delete_layer_ != nullptr) return;
    pending_delete_ = index;
    const auto& entry = displayed_recordings_[index];

    delete_layer_ = CreatePlain(lv_layer_top());
    lv_obj_set_size(delete_layer_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(delete_layer_, rodakos_theme_overlay(), 0);
    lv_obj_set_style_bg_opa(delete_layer_, LV_OPA_70, 0);
    lv_obj_add_flag(delete_layer_, LV_OBJ_FLAG_CLICKABLE);
    auto* box = CreatePlain(delete_layer_);
    lv_obj_set_size(box, 296, 136);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_border_color(box, rodakos_theme_border(), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    auto* title = CreateText(box, "删除这条录音？", &phone_font_18, rodakos_theme_text_primary());
    lv_obj_set_pos(title, 14, 12);
    const std::string file = entry.path.substr(entry.path.find_last_of('/') + 1);
    auto* message = CreateText(box, "删除后无法恢复", &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_pos(message, 14, 42);
    auto* name = CreateText(box, file.c_str(), &phone_font_12, rodakos_theme_text_secondary());
    lv_obj_set_width(name, 266);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(name, 14, 60);

    auto make_button = [&](lv_coord_t x, const char* text, bool danger, lv_event_cb_t cb) {
        auto* button = lv_btn_create(box);
        lv_obj_remove_style_all(button);
        lv_obj_set_size(button, 128, 38);
        lv_obj_set_pos(button, x, 86);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_bg_color(button, danger ? rodakos_theme_error() : rodakos_theme_bg_tertiary(), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        auto* label = CreateText(button, text, &phone_font_14,
                                 danger ? rodakos_theme_on_error() : rodakos_theme_text_primary());
        lv_obj_center(label);
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, this);
    };
    make_button(14, "取消", false, [](lv_event_t* e) {
        static_cast<RecorderApp*>(lv_event_get_user_data(e))->CloseDeleteDialog();
    });
    make_button(154, "删除", true, [](lv_event_t* e) {
        static_cast<RecorderApp*>(lv_event_get_user_data(e))->ConfirmDelete();
    });
}

void RecorderApp::CloseDeleteDialog() {
    // Deleting from inside one of its own button callbacks is not safe; defer it.
    if (delete_layer_ != nullptr && lv_obj_is_valid(delete_layer_)) lv_obj_delete_async(delete_layer_);
    delete_layer_ = nullptr;
}

void RecorderApp::ConfirmDelete() {
    CloseDeleteDialog();
    if (recording_ == nullptr || pending_delete_ >= displayed_recordings_.size()) return;
    const auto entry = displayed_recordings_[pending_delete_];
    // Stop first: the playback task holds the file open.
    if (IsPlaying(entry)) audio_->Stop();
    if (!recording_->DeleteRecording(entry.path)) {
        if (ui_ != nullptr) ui_->ShowToastUnlocked("删除失败");
    } else if (ui_ != nullptr) {
        ui_->ShowToastUnlocked("已删除");
    }
    RebuildRecordingList();
    RefreshState();
}

void RecorderApp::BoostPlaybackVolume() {
    const int current = audio_->volume();
    if (current < kRecordingPlaybackVolume && audio_->SetVolume(kRecordingPlaybackVolume)) restore_volume_ = current;
}

void RecorderApp::RestorePlaybackVolume() {
    if (restore_volume_ < 0 || audio_ == nullptr) return;
    // Keep a volume chosen elsewhere (MQTT, voice, Music) while the recording played.
    if (audio_->volume() == kRecordingPlaybackVolume) audio_->SetVolume(restore_volume_);
    restore_volume_ = -1;
}

void RecorderApp::RefreshState() {
    if (restore_volume_ >= 0 && audio_ != nullptr && !IsPlaybackActive(audio_->GetState().status)) {
        RestorePlaybackVolume();
    }
    if (root_ == nullptr) return;

    const auto state = recording_ != nullptr ? recording_->GetState() : rodakos::RecordingState{};
    SetRefreshPeriod(IsRecordingActive(state.status));

    const auto recording_error = RecordingError(state);
    const bool library_changed = last_library_error_ != (recording_ != nullptr ? state.library_error : "Recording service missing") ||
                                 last_recording_error_ != recording_error;
    if (last_status_ != state.status || library_changed) {
        if (!IsRecordingActive(state.status) || library_changed) RebuildRecordingList();
        last_status_ = state.status;
    }
    if (view_ == View::kRecord) {
        if (title_label_ != nullptr) lv_label_set_text(title_label_, "录音");
        RefreshRecordView(state);
    } else {
        RefreshListPlayback();
    }
}

void RecorderApp::RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<RecorderApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) self->RefreshState();
}

void RecorderApp::NavigateHome() {
    if (context_ == nullptr) return;
    lv_async_call_cancel(DeferReturnHome, context_);
    lv_async_call(DeferReturnHome, context_);
}

void RegisterRecorderApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "recorder",
        .title = "Recorder",
        .icon = ICON_MICROPHONE,
        .category = PhoneAppCategory::kMedia,
        .capabilities = PhoneCapability::kStorage | PhoneCapability::kAudioPlayback |
                        PhoneCapability::kAudioRecording,
        .show_on_home = true,
        .aliases = {"record", "recorder", "recording", "voice memo", "memo", "录音"},
        .create = []() { return std::make_unique<RecorderApp>(); },
    });
}
