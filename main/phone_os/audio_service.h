#pragma once

#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace rodakos {

enum class AudioPlaybackStatus {
    kIdle,
    kLoading,
    kPlaying,
    kPaused,
    kStopped,
    kCompleted,
    kError,
};

struct AudioPlaybackState {
    AudioPlaybackStatus status = AudioPlaybackStatus::kIdle;
    std::string file_path;
    std::string title;
    std::string message;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    size_t bytes_played = 0;
    size_t data_bytes = 0;
    int progress_percent = 0;
    int volume = 60;
};

class AudioService {
public:
    AudioService();
    ~AudioService();

    bool Init();
    void Deinit();

    bool PlayFile(const std::string& path, const std::string& title = "");
    void Stop();
    void Pause();
    void Resume();
    void TogglePause();

    bool SetVolume(int volume);
    int volume() const;

    AudioPlaybackState GetState();
    bool IsReady() const { return initialized_; }
    bool IsBusy();

    static bool IsSupportedAudioFile(const std::string& name);

private:
    static void PlaybackTaskEntry(void* arg);
    void PlaybackTask();
    bool PlayWavFile(FILE* fp, const std::string& path, bool& stopped);
    bool PlayMp3File(FILE* fp, const std::string& path, bool& stopped);
    bool ShouldStopOrPause(bool& should_pause);
    void SetState(AudioPlaybackStatus status, const char* message = nullptr);
    void SetGenericPlaybackErrorIfNeeded();
    void UpdateProgress(size_t bytes_played, size_t data_bytes);
    void MarkPlaybackTaskStarting();
    void StorePlaybackTaskHandle(TaskHandle_t task);
    void ClearPlaybackTask();
    bool HasPlaybackTask();
    void SetCodecOpen(bool open);
    bool IsCodecOpen();
    bool JoinPlaybackTask(uint32_t timeout_ms);

    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t playback_task_ = nullptr;
    bool playback_task_active_ = false;
    bool initialized_ = false;
    bool codec_open_ = false;
    bool stop_requested_ = false;
    bool pause_requested_ = false;
    int volume_ = 60;
    void* dac_handle_ = nullptr;
    AudioPlaybackState state_;
};

}  // namespace rodakos
