#pragma once

#include "phone_os/audio_service.h"

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace rodakos {

class FileService;

struct MusicTrack {
    std::string title;
    std::string path;
    size_t size = 0;
};

enum class MusicPlaybackMode {
    kSequential,
    kShuffle,
    kRepeatOne,
};

enum class MusicLibraryStatus {
    kNotScanned,
    kScanning,
    kReady,
    kEmpty,
    kServiceUnavailable,
    kStorageUnavailable,
    kReadError,
};

struct MusicPlayerState {
    AudioPlaybackState audio;
    MusicPlaybackMode mode = MusicPlaybackMode::kSequential;
    size_t track_count = 0;
    int current_index = -1;
    std::string current_title;
    bool queue_paused = false;
    MusicLibraryStatus library_status = MusicLibraryStatus::kNotScanned;
    std::string library_message = "Music library not scanned";
    uint64_t library_revision = 0;
    std::string operation_error;
};

class MusicPlayerService {
public:
    MusicPlayerService(AudioService& audio, FileService* file_service);
    ~MusicPlayerService();

    bool Init();
    void Deinit();

    bool ScanLibrary();
    bool RequestLibraryScan();
    std::vector<MusicTrack> GetTracks(uint64_t* library_revision = nullptr);
    size_t track_count();

    MusicPlayerState GetState();
    MusicPlaybackMode playback_mode();
    MusicPlaybackMode TogglePlaybackMode();

    bool PlayTrack(size_t index, uint64_t library_revision = 0);
    bool PlayPrevious();
    bool PlayNext();
    bool TogglePlayPause();
    void Pause();
    void Resume();
    bool SuspendPlaybackHardware();
    bool SetPlaybackBlocked(bool blocked);
    void Stop();
    bool ReleasePlaybackHardware();
    bool Refresh();

    bool SetVolume(int volume);
    int volume() const;

private:
    bool ScanLibrary(bool load_saved_state);
    bool PlayTrackLocked(size_t index, uint64_t library_revision);
    bool StartMonitor();
    bool FailLibrary(MusicLibraryStatus status, const char* message);
    void SetOperationError(const std::string& message);
    static void MonitorTaskEntry(void* arg);
    void MonitorTask();

    bool ScanDirectory(const std::string& path, int depth, std::vector<MusicTrack>& tracks);
    void LoadPlaybackState(const std::vector<MusicTrack>& tracks, int& index, MusicPlaybackMode& mode);
    void SavePlaybackState();
    void SyncCurrentIndexFromPath(const std::string& path);
    int FindTrackIndexByPath(const std::vector<MusicTrack>& tracks, const std::string& path) const;
    int PickRandomTrackIndexLocked() const;
    int NextIndexForCompletedLocked() const;
    int NextIndexForManualNextLocked() const;

    AudioService& audio_;
    FileService* file_service_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t monitor_task_ = nullptr;
    bool initialized_ = false;
    bool monitor_stop_requested_ = false;
    bool monitor_task_active_ = false;
    bool playback_starting_ = false;
    bool playback_blocked_ = false;
    bool scan_requested_ = false;
    bool stopping_ = false;
    std::mutex operation_mutex_;
    std::mutex scan_mutex_;
    MusicLibraryStatus library_status_ = MusicLibraryStatus::kNotScanned;
    std::string library_message_ = "Music library not scanned";
    uint64_t library_revision_ = 0;
    std::string operation_error_;
    std::vector<MusicTrack> tracks_;
    int current_index_ = -1;
    MusicPlaybackMode playback_mode_ = MusicPlaybackMode::kSequential;
    bool completion_handled_ = false;
    bool queue_paused_ = false;
};

}  // namespace rodakos
