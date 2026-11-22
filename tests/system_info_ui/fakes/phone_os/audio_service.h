#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace rodakos {
enum class AudioPlaybackStatus { kIdle, kLoading, kPlaying, kPaused, kStopped, kCompleted, kError };
struct AudioPlaybackState {
    AudioPlaybackStatus status = AudioPlaybackStatus::kIdle;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    int volume = 60;
};
class AudioService {
public:
    AudioPlaybackState GetState() { return state; }
    AudioPlaybackState state;
};
}  // namespace rodakos
