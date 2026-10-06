#pragma once
#include <string>
namespace rodakos {
enum class AudioPlaybackStatus { kIdle, kLoading, kPlaying, kPaused, kStopped, kCompleted, kError };
struct AudioPlaybackState {
    AudioPlaybackStatus status = AudioPlaybackStatus::kIdle;
    std::string file_path;
};
class AudioService {
public:
    bool ReleasePlaybackHardware() { ++releases; return true; }
    AudioPlaybackState GetState() { return state; }
    void Stop() { ++stops; state.status = AudioPlaybackStatus::kStopped; }
    int volume() { return configured_volume; }
    bool SetVolume(int volume) { configured_volume = volume; return true; }
    bool PlayFile(const std::string& path, const std::string& title) {
        ++plays;
        state.file_path = path;
        last_title = title;
        state.status = AudioPlaybackStatus::kPlaying;
        return true;
    }
    AudioPlaybackState state;
    std::string last_title;
    int releases = 0, stops = 0, plays = 0, configured_volume = 60;
};
}
