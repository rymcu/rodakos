#include "phone_os/audio_focus_service.h"

#include "phone_os/music_player_service.h"

#include <algorithm>
#include <inttypes.h>

#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "AudioFocusService";

const char* GainName(AudioFocusGain gain) {
    switch (gain) {
        case AudioFocusGain::kDuck:
            return "duck";
        case AudioFocusGain::kPause:
            return "pause";
        case AudioFocusGain::kExclusive:
            return "exclusive";
        default:
            return "unknown";
    }
}

bool IsPlayingStatus(AudioPlaybackStatus status) {
    return status == AudioPlaybackStatus::kLoading || status == AudioPlaybackStatus::kPlaying;
}

}  // namespace

AudioFocusService::AudioFocusService(MusicPlayerService& music_player)
    : music_player_(music_player) {
    mutex_ = xSemaphoreCreateMutex();
}

AudioFocusService::~AudioFocusService() {
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool AudioFocusService::RequestFocus(const AudioFocusRequest& request, uint32_t& token) {
    if (request.owner.empty()) {
        ESP_LOGW(TAG, "Rejecting focus request with empty owner");
        return false;
    }

    const int duck_volume = std::clamp(request.duck_volume, 0, 100);
    AudioFocusRequest sanitized = request;
    sanitized.duck_volume = duck_volume;

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (active_) {
            RestoreFocusLocked();
            ClearFocusLocked();
        }

        active_ = true;
        active_token_ = next_token_++;
        if (next_token_ == 0) {
            next_token_ = 1;
        }
        owner_ = sanitized.owner;
        gain_ = sanitized.gain;
        resume_on_release_ = sanitized.resume_on_release;
        token = active_token_;
        ApplyFocusLocked(sanitized);
        xSemaphoreGive(mutex_);
    } else {
        token = 0;
        return false;
    }

    ESP_LOGI(TAG, "Audio focus granted: owner=%s gain=%s token=%" PRIu32,
             sanitized.owner.c_str(), GainName(sanitized.gain), token);
    return true;
}

bool AudioFocusService::ReleaseFocus(uint32_t token) {
    bool released = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (active_ && active_token_ == token) {
            RestoreFocusLocked();
            ClearFocusLocked();
            released = true;
        }
        xSemaphoreGive(mutex_);
    }
    if (released) {
        ESP_LOGI(TAG, "Audio focus released: token=%" PRIu32, token);
    }
    return released;
}

void AudioFocusService::ReleaseOwner(const std::string& owner) {
    if (owner.empty() || mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (active_ && owner_ == owner) {
        RestoreFocusLocked();
        ClearFocusLocked();
        ESP_LOGI(TAG, "Audio focus released by owner: %s", owner.c_str());
    }
    xSemaphoreGive(mutex_);
}

AudioFocusState AudioFocusService::GetState() {
    AudioFocusState state;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state.active = active_;
        state.token = active_token_;
        state.owner = owner_;
        state.gain = gain_;
        xSemaphoreGive(mutex_);
    }
    return state;
}

void AudioFocusService::ApplyFocusLocked(const AudioFocusRequest& request) {
    const auto state = music_player_.GetState();
    music_was_playing_ = IsPlayingStatus(state.audio.status);
    music_was_paused_ = state.audio.status == AudioPlaybackStatus::kPaused;
    previous_volume_ = state.audio.volume;

    switch (request.gain) {
        case AudioFocusGain::kDuck:
            if (music_was_playing_ || music_was_paused_) {
                music_player_.SetVolume(request.duck_volume);
            }
            break;
        case AudioFocusGain::kPause:
        case AudioFocusGain::kExclusive:
            if (music_was_playing_) {
                music_player_.Pause();
            }
            break;
        default:
            break;
    }
}

void AudioFocusService::RestoreFocusLocked() {
    if (!active_) {
        return;
    }

    if (gain_ == AudioFocusGain::kDuck) {
        music_player_.SetVolume(previous_volume_);
        return;
    }

    if (resume_on_release_ && music_was_playing_) {
        music_player_.Resume();
    }
}

void AudioFocusService::ClearFocusLocked() {
    active_ = false;
    active_token_ = 0;
    owner_.clear();
    gain_ = AudioFocusGain::kPause;
    resume_on_release_ = true;
    music_was_playing_ = false;
    music_was_paused_ = false;
    previous_volume_ = music_player_.volume();
}

}  // namespace rodakos
