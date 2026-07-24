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

#include <cstdio>
#include <cstdint>
#include <inttypes.h>
#include <memory>
#include <string>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr const char* TAG = "RecorderApp";
constexpr uint32_t kRefreshPeriodMs = 500;
constexpr uint32_t kToneTaskStackWords = 4096;
constexpr uint32_t kToneSampleRate = 16000;
constexpr uint32_t kToneDurationMs = 1200;
constexpr uint32_t kToneFrequencyHz = 880;
constexpr int16_t kToneAmplitude = 20000;
constexpr int kRecordingPlaybackVolume = 90;
bool s_tone_running = false;

struct ToneTaskPayload {
    rodakos::AudioOutputService* output = nullptr;
};

void DeferReturnHome(void* user_data) {
    auto* context = static_cast<PhoneAppContext*>(user_data);
    if (context != nullptr) {
        context->navigation().ReturnHome();
    }
}

lv_obj_t* CreateText(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

void StyleRoundButton(lv_obj_t* button, lv_coord_t size, lv_color_t color) {
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, size, size);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(button, rodakos_theme_bg_tertiary(), LV_STATE_DISABLED);
    lv_obj_set_style_translate_y(button, 1, LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
}

void ToneTask(void* arg) {
    auto* payload = static_cast<ToneTaskPayload*>(arg);
    auto* output = payload != nullptr ? payload->output : nullptr;
    delete payload;

    if (output != nullptr && output->Open(kToneSampleRate, 1, 16)) {
        output->SetVolume(90);
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
                const int16_t value = (phase & 1U) ? -kToneAmplitude : kToneAmplitude;
                samples[i] = value;
            }
            if (!output->Write(samples, static_cast<int>(frames * sizeof(int16_t)))) {
                ESP_LOGW(TAG, "Speaker test tone write failed");
                break;
            }
            frame_index += frames;
        }
        output->Close();
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

const char* StatusText(rodakos::RecordingStatus status) {
    switch (status) {
        case rodakos::RecordingStatus::kStarting:
            return "Starting";
        case rodakos::RecordingStatus::kRecording:
            return "Recording";
        case rodakos::RecordingStatus::kStopping:
            return "Stopping";
        case rodakos::RecordingStatus::kCompleted:
            return "Saved";
        case rodakos::RecordingStatus::kError:
            return "Error";
        case rodakos::RecordingStatus::kIdle:
        default:
            return "Ready";
    }
}

std::string FormatDuration(uint32_t duration_ms) {
    const uint32_t total_seconds = duration_ms / 1000;
    const uint32_t minutes = total_seconds / 60;
    const uint32_t seconds = total_seconds % 60;
    char buffer[24] = {};
    if (minutes >= 60) {
        const uint32_t hours = minutes / 60;
        std::snprintf(buffer, sizeof(buffer), "%lu:%02lu:%02lu",
                      static_cast<unsigned long>(hours),
                      static_cast<unsigned long>(minutes % 60),
                      static_cast<unsigned long>(seconds));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lu:%02lu",
                      static_cast<unsigned long>(minutes),
                      static_cast<unsigned long>(seconds));
    }
    return buffer;
}

std::string FormatSize(size_t bytes) {
    char buffer[24] = {};
    if (bytes < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%u B", static_cast<unsigned>(bytes));
    } else if (bytes < 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buffer;
}

}  // namespace

bool RecorderApp::OnCreate(PhoneAppContext& context) {
    context_ = &context;
    ui_ = &context.ui();
    recording_ = context.services().recording();
    audio_ = context.services().audio();
    audio_output_ = context.services().audio_output();

    if (recording_ != nullptr) {
        recording_->RefreshRecordings();
    }

    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return false;
    }

    CreateUi();
    RebuildRecordingList();
    RefreshState();
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, kRefreshPeriodMs, this);

    ESP_LOGI(TAG, "Recorder app created with %zu recordings", displayed_recordings_.size());
    return true;
}

void RecorderApp::OnResume() {
    if (ui_ == nullptr) {
        return;
    }
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return;
    }
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
    if (ui_ == nullptr) {
        return;
    }
    PhoneUiLock lock(*ui_);
    if (!lock.locked()) {
        return;
    }
    if (refresh_timer_ != nullptr) {
        lv_timer_pause(refresh_timer_);
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void RecorderApp::OnDestroy() {
    if (ui_ != nullptr) {
        PhoneUiLock lock(*ui_);
        if (lock.locked()) {
            DestroyUi();
        }
    }

    if (recording_ != nullptr) {
        recording_->Stop();
    }

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
    if (!lock.locked()) {
        return false;
    }

    const bool was_hidden = root_ != nullptr && lv_obj_is_valid(root_) &&
                            lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    DestroyUi();
    CreateUi();
    RebuildRecordingList();
    RefreshState();
    if (was_hidden && root_ != nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
    refresh_timer_ = lv_timer_create(RefreshTimerCallback, kRefreshPeriodMs, this);
    return true;
}

void RecorderApp::CreateUi() {
    root_ = lv_obj_create(ui_->screen());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, rodakos_theme_bg_primary(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    CreateAppHeader(root_, "Recorder", [](lv_event_t* e) {
        static_cast<RecorderApp*>(lv_event_get_user_data(e))->NavigateHome();
    }, [](lv_event_t* e) {
        static_cast<RecorderApp*>(lv_event_get_user_data(e))->NavigateHome();
    }, this);

    auto* status_card = lv_obj_create(root_);
    lv_obj_remove_style_all(status_card);
    lv_obj_set_size(status_card, 300, 72);
    lv_obj_set_pos(status_card, 10, 46);
    lv_obj_set_style_bg_color(status_card, rodakos_theme_bg_secondary(), 0);
    lv_obj_set_style_bg_opa(status_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(status_card, 8, 0);
    lv_obj_set_style_pad_all(status_card, 0, 0);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    auto* mic_icon = CreateText(status_card, FONT_AWESOME_MICROPHONE, PhoneIconFontLarge(),
                                rodakos_theme_primary());
    lv_obj_align(mic_icon, LV_ALIGN_LEFT_MID, 14, 0);

    title_label_ = CreateText(status_card, "Ready", &phone_font_18, rodakos_theme_text_primary());
    lv_obj_set_width(title_label_, 142);
    lv_label_set_long_mode(title_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(title_label_, LV_ALIGN_TOP_LEFT, 50, 10);

    status_label_ = CreateText(status_card, "Ready", &phone_font_12,
                               rodakos_theme_text_secondary());
    lv_obj_set_width(status_label_, 142);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(status_label_, LV_ALIGN_TOP_LEFT, 50, 35);

    duration_label_ = CreateText(status_card, "0:00", &phone_font_18, rodakos_theme_text_primary());
    lv_obj_set_width(duration_label_, 62);
    lv_obj_set_style_text_align(duration_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(duration_label_, LV_ALIGN_TOP_RIGHT, -74, 10);

    size_label_ = CreateText(status_card, "0 B", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(size_label_, 62);
    lv_obj_set_style_text_align(size_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(size_label_, LV_LABEL_LONG_DOT);
    lv_obj_align(size_label_, LV_ALIGN_TOP_RIGHT, -74, 38);

    record_button_ = lv_btn_create(status_card);
    StyleRoundButton(record_button_, 48, rodakos_theme_primary());
    lv_obj_align(record_button_, LV_ALIGN_RIGHT_MID, -12, 0);
    record_icon_label_ = lv_label_create(record_button_);
    lv_label_set_text(record_icon_label_, FONT_AWESOME_MICROPHONE);
    lv_obj_set_style_text_font(record_icon_label_, PhoneIconFont(), 0);
    lv_obj_set_style_text_color(record_icon_label_, lv_color_white(), 0);
    lv_obj_center(record_icon_label_);
    lv_obj_add_event_cb(record_button_, [](lv_event_t* e) {
        static_cast<RecorderApp*>(lv_event_get_user_data(e))->ToggleRecording();
    }, LV_EVENT_CLICKED, this);

    tone_button_ = lv_btn_create(root_);
    StyleRoundButton(tone_button_, 28, rodakos_theme_bg_tertiary());
    lv_obj_set_pos(tone_button_, 178, 120);
    auto* tone_icon = CreateText(tone_button_, FONT_AWESOME_VOLUME_HIGH, PhoneIconFont(),
                                 rodakos_theme_primary());
    lv_obj_center(tone_icon);
    lv_obj_add_event_cb(tone_button_, [](lv_event_t* e) {
        static_cast<RecorderApp*>(lv_event_get_user_data(e))->StartSpeakerTest();
    }, LV_EVENT_CLICKED, this);

    auto* list_title = CreateText(root_, "Recordings", &phone_font_14, rodakos_theme_text_primary());
    lv_obj_set_pos(list_title, 12, 126);

    count_label_ = CreateText(root_, "0 files", &phone_font_12, rodakos_theme_text_tertiary());
    lv_obj_set_width(count_label_, 86);
    lv_obj_set_style_text_align(count_label_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(count_label_, 224, 128);

    list_ = lv_obj_create(root_);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, 300, 86);
    lv_obj_set_pos(list_, 10, 146);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(list_, 5, 0);
}

void RecorderApp::DestroyUi() {
    if (refresh_timer_ != nullptr) {
        lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
    }
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_);
    }
    ResetUiPointers();
}

void RecorderApp::ResetUiPointers() {
    root_ = nullptr;
    title_label_ = nullptr;
    status_label_ = nullptr;
    duration_label_ = nullptr;
    size_label_ = nullptr;
    record_button_ = nullptr;
    record_icon_label_ = nullptr;
    tone_button_ = nullptr;
    count_label_ = nullptr;
    list_ = nullptr;
}

void RecorderApp::RebuildRecordingList() {
    if (list_ == nullptr) {
        return;
    }
    lv_obj_clean(list_);
    displayed_recordings_ =
        recording_ != nullptr ? recording_->GetRecordings() : std::vector<rodakos::RecordingEntry>{};

    if (count_label_ != nullptr) {
        lv_label_set_text_fmt(count_label_, "%zu files", displayed_recordings_.size());
    }

    if (displayed_recordings_.empty()) {
        auto* empty = CreateText(list_, "No recordings", &phone_font_12,
                                 rodakos_theme_text_tertiary());
        lv_obj_set_width(empty, 300);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    for (size_t i = 0; i < displayed_recordings_.size(); ++i) {
        const auto& entry = displayed_recordings_[i];
        auto* row = lv_btn_create(list_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 300, 38);
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_secondary(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, rodakos_theme_bg_tertiary(), LV_STATE_PRESSED);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        auto* title = CreateText(row, entry.title.c_str(), &phone_font_14,
                                 rodakos_theme_text_primary());
        lv_obj_set_width(title, 220);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 4);

        const std::string detail =
            FormatDuration(entry.duration_ms) + "  " + FormatSize(entry.size);
        auto* detail_label = CreateText(row, detail.c_str(), &phone_font_12,
                                        rodakos_theme_text_tertiary());
        lv_obj_set_width(detail_label, 220);
        lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
        lv_obj_align(detail_label, LV_ALIGN_TOP_LEFT, 12, 21);

        auto* play_icon = CreateText(row, FONT_AWESOME_PLAY, PhoneIconFont(),
                                     rodakos_theme_primary());
        lv_obj_align(play_icon, LV_ALIGN_RIGHT_MID, -13, 0);

        lv_obj_set_user_data(row, reinterpret_cast<void*>(i));
        lv_obj_add_event_cb(row, [](lv_event_t* e) {
            auto* self = static_cast<RecorderApp*>(lv_event_get_user_data(e));
            const auto index = reinterpret_cast<uintptr_t>(
                lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_current_target(e))));
            self->PlayRecording(static_cast<size_t>(index));
        }, LV_EVENT_CLICKED, this);
    }
}

void RecorderApp::ToggleRecording() {
    if (recording_ == nullptr) {
        if (ui_ != nullptr) {
            ui_->ShowToastUnlocked("Recorder unavailable");
        }
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

    if (audio_ != nullptr) {
        audio_->ReleasePlaybackHardware();
    }
    if (!recording_->Start()) {
        if (ui_ != nullptr) {
            const auto next_state = recording_->GetState();
            ui_->ShowToastUnlocked(next_state.message.empty() ? "Recording failed"
                                                              : next_state.message.c_str());
        }
    }
    RefreshState();
}

void RecorderApp::StartSpeakerTest() {
    if (audio_output_ == nullptr || s_tone_running) {
        return;
    }
    if (recording_ != nullptr && IsRecordingActive(recording_->GetState().status)) {
        if (ui_ != nullptr) {
            ui_->ShowToastUnlocked("Stop recording first");
        }
        return;
    }
    if (audio_ != nullptr) {
        audio_->ReleasePlaybackHardware();
    }

    auto* payload = new ToneTaskPayload();
    payload->output = audio_output_;
    s_tone_running = true;
    const BaseType_t ret =
        xTaskCreate(ToneTask, "rec_tone", kToneTaskStackWords, payload, 4, nullptr);
    if (ret != pdPASS) {
        s_tone_running = false;
        delete payload;
        ESP_LOGW(TAG, "Failed to create speaker test tone task");
        if (ui_ != nullptr) {
            ui_->ShowToastUnlocked("Tone unavailable");
        }
    }
}

void RecorderApp::PlayRecording(size_t index) {
    if (index >= displayed_recordings_.size()) {
        return;
    }
    if (recording_ != nullptr && IsRecordingActive(recording_->GetState().status)) {
        if (ui_ != nullptr) {
            ui_->ShowToastUnlocked("Stop recording first");
        }
        return;
    }
    if (audio_ == nullptr) {
        if (ui_ != nullptr) {
            ui_->ShowToastUnlocked("Cannot play recording");
        }
        return;
    }

    const auto& entry = displayed_recordings_[index];
    const auto audio_state = audio_->GetState();
    if (audio_state.file_path == entry.full_path && IsPlaybackActive(audio_state.status)) {
        audio_->Stop();
        return;
    }

    if (audio_->volume() < kRecordingPlaybackVolume) {
        audio_->SetVolume(kRecordingPlaybackVolume);
    }
    if (!audio_->PlayFile(entry.full_path, entry.title)) {
        if (ui_ != nullptr) {
            ui_->ShowToastUnlocked("Cannot play recording");
        }
    }
}

void RecorderApp::RefreshState() {
    if (title_label_ == nullptr || status_label_ == nullptr) {
        return;
    }

    if (recording_ == nullptr) {
        lv_label_set_text(title_label_, "Unavailable");
        lv_label_set_text(status_label_, "Recording service missing");
        lv_obj_set_style_text_color(status_label_, rodakos_theme_error(), 0);
        lv_label_set_text(duration_label_, "0:00");
        lv_label_set_text(size_label_, "0 B");
        if (record_button_ != nullptr) {
            lv_obj_add_state(record_button_, LV_STATE_DISABLED);
        }
        return;
    }

    const auto state = recording_->GetState();
    const bool active = IsRecordingActive(state.status);

    if (!state.title.empty() && state.status != rodakos::RecordingStatus::kIdle) {
        lv_label_set_text(title_label_, state.title.c_str());
    } else {
        lv_label_set_text(title_label_, "Ready");
    }

    const char* status_text = !state.message.empty() ? state.message.c_str() : StatusText(state.status);
    lv_label_set_text(status_label_, status_text);
    lv_obj_set_style_text_color(status_label_,
                                state.status == rodakos::RecordingStatus::kError
                                    ? rodakos_theme_error()
                                    : rodakos_theme_text_secondary(),
                                0);

    const auto duration = FormatDuration(state.duration_ms);
    const auto size = FormatSize(state.file_size);
    lv_label_set_text(duration_label_, duration.c_str());
    lv_label_set_text(size_label_, size.c_str());
    UpdateRecordButton(active);

    if (last_status_ != state.status) {
        if (last_status_ != rodakos::RecordingStatus::kIdle &&
            !IsRecordingActive(state.status)) {
            RebuildRecordingList();
        }
        last_status_ = state.status;
    }
}

void RecorderApp::UpdateRecordButton(bool recording) {
    if (record_button_ == nullptr || record_icon_label_ == nullptr) {
        return;
    }
    lv_obj_clear_state(record_button_, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(record_button_,
                              recording ? rodakos_theme_error() : rodakos_theme_primary(),
                              0);
    lv_label_set_text(record_icon_label_, recording ? LV_SYMBOL_STOP : FONT_AWESOME_MICROPHONE);
    lv_obj_center(record_icon_label_);
}

void RecorderApp::RefreshTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<RecorderApp*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->RefreshState();
    }
}

void RecorderApp::NavigateHome() {
    lv_async_call(DeferReturnHome, context_);
}

void RegisterRecorderApp(PhoneAppRegistry& registry) {
    registry.Register(PhoneAppDescriptor{
        .id = "recorder",
        .title = "Recorder",
        .icon = FONT_AWESOME_MICROPHONE,
        .category = PhoneAppCategory::kMedia,
        .capabilities = PhoneCapability::kStorage |
                        PhoneCapability::kAudioPlayback |
                        PhoneCapability::kAudioRecording,
        .show_on_home = true,
        .aliases = {"record", "recorder", "recording", "voice memo", "memo"},
        .create = []() { return std::make_unique<RecorderApp>(); },
    });
}
