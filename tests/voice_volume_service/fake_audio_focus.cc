#include "phone_os/audio_focus_service.h"

namespace rodakos {
// 音乐抢占不在此 fixture 范围；保留真实公共契约，隔离音乐/硬件依赖。
AudioFocusService::AudioFocusService(MusicPlayerService& music, AudioOutputService& output)
    : music_player_(music), audio_output_(output) {}
AudioFocusService::~AudioFocusService() = default;
bool AudioFocusService::RequestFocus(const AudioFocusRequest&, uint32_t& token) {
    token = next_token_++;
    active_token_ = token;
    return true;
}
bool AudioFocusService::ReleaseFocus(uint32_t token) {
    const bool current = token == active_token_;
    if (current) active_token_ = 0;
    return current;
}
}
