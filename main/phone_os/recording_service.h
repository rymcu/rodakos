#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace rodakos {

class AudioCodecInput;
class AudioFocusService;
class FileService;

enum class RecordingStatus {
    kIdle,
    kStarting,
    kRecording,
    kStopping,
    kCompleted,
    kError,
};

struct RecordingConfig {
    uint32_t sample_rate = 16000;
    uint16_t channels = 1;
    uint16_t bits_per_sample = 16;
    int gain = 30;
    uint16_t input_channels = 4;
    uint16_t input_channel_mask = 1U << 1;
};

struct RecordingState {
    RecordingStatus status = RecordingStatus::kIdle;
    std::string path;
    std::string full_path;
    std::string title;
    std::string message = "Ready";
    std::string last_error;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    size_t bytes_written = 0;
    size_t file_size = 0;
    uint32_t duration_ms = 0;
    int gain = 0;
};

struct RecordingEntry {
    std::string title;
    std::string path;
    std::string full_path;
    size_t size = 0;
    uint32_t duration_ms = 0;
    uint64_t modified_time = 0;
};

class RecordingService {
public:
    RecordingService(AudioCodecInput& input,
                     FileService* file_service,
                     AudioFocusService* audio_focus);
    ~RecordingService();

    bool Start(const RecordingConfig& config = RecordingConfig{});
    void Stop();
    RecordingState GetState();
    std::vector<RecordingEntry> GetRecordings();
    bool RefreshRecordings();
    bool DeleteRecording(const std::string& path);

    static bool IsRecordingFile(const std::string& name);

private:
    static void RecordingTaskEntry(void* arg);
    void RecordingTask();

    bool PrepareStorage();
    std::string BuildRecordingPath();
    std::string FullPath(const std::string& path) const;
    bool RequestAudioFocus();
    void ReleaseAudioFocus();
    bool ShouldStop() const;
    bool HasTask() const;
    bool JoinTask(uint32_t timeout_ms);
    void MarkTaskStarting();
    void StoreTaskHandle(TaskHandle_t task);
    void ClearTask();
    void SetState(RecordingStatus status, const char* message = nullptr);
    void SetError(const std::string& error);
    void UpdateProgress(size_t bytes_written);
    uint32_t DurationForBytes(size_t bytes) const;

    AudioCodecInput& input_;
    FileService* file_service_ = nullptr;
    AudioFocusService* audio_focus_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    bool task_active_ = false;
    bool stop_requested_ = false;
    uint32_t audio_focus_token_ = 0;
    RecordingConfig active_config_;
    RecordingState state_;
    std::vector<RecordingEntry> recordings_;
};

}  // namespace rodakos
