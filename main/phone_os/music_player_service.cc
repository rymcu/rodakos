#include "phone_os/music_player_service.h"

#include "rodakos_adapters/file_service.h"
#include "settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <esp_log.h>
#include <esp_random.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "MusicPlayerService";
constexpr const char* kMusicNamespace = "music";
constexpr const char* kModeKey = "mode";
constexpr const char* kTrackPathKey = "track";
constexpr const char* kTrackIndexKey = "idx";
constexpr uint32_t kMonitorTaskStackWords = 3072;

std::string NormalizePathKey(const std::string& path) {
    std::string key;
    key.reserve(path.size());
    for (char ch : path) {
        key.push_back(ch == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return key;
}

bool TrackPathLess(const MusicTrack& a, const MusicTrack& b) {
    return NormalizePathKey(a.path) < NormalizePathKey(b.path);
}

bool SameTrackPath(const MusicTrack& a, const MusicTrack& b) {
    return NormalizePathKey(a.path) == NormalizePathKey(b.path);
}

const char* ModeName(MusicPlaybackMode mode) {
    switch (mode) {
        case MusicPlaybackMode::kShuffle:
            return "shuffle";
        case MusicPlaybackMode::kRepeatOne:
            return "repeat-one";
        case MusicPlaybackMode::kSequential:
        default:
            return "sequential";
    }
}

}  // namespace

MusicPlayerService::MusicPlayerService(AudioService& audio, FileService* file_service)
    : audio_(audio), file_service_(file_service) {
    mutex_ = xSemaphoreCreateMutex();
}

MusicPlayerService::~MusicPlayerService() {
    Deinit();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool MusicPlayerService::Init() {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    stopping_ = false;
    const bool load_saved = !initialized_;
    xSemaphoreGive(mutex_);
    audio_.Init();
    const bool scanned = ScanLibrary(load_saved);
    const bool monitored = StartMonitor();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    initialized_ = monitored;
    xSemaphoreGive(mutex_);
    return scanned && monitored;
}

bool MusicPlayerService::StartMonitor() {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (stopping_ || monitor_task_active_) {
        const bool active = !stopping_ && monitor_task_active_;
        xSemaphoreGive(mutex_);
        return active;
    }
    monitor_stop_requested_ = false;
    monitor_task_active_ = true;
    xSemaphoreGive(mutex_);
    TaskHandle_t handle = nullptr;
    const BaseType_t task_ret = xTaskCreate(
        MonitorTaskEntry, "music_player", kMonitorTaskStackWords, this, 4, &handle);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (task_ret != pdPASS) {
        monitor_task_active_ = false;
        monitor_task_ = nullptr;
        scan_requested_ = false;
        operation_error_ = "Music worker unavailable; retry";
    } else if (monitor_task_active_) {
        monitor_task_ = handle;
        operation_error_.clear();
    }
    xSemaphoreGive(mutex_);
    return task_ret == pdPASS;
}

bool MusicPlayerService::RequestLibraryScan() {
    if (!StartMonitor()) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (stopping_ || library_status_ == MusicLibraryStatus::kScanning) {
        xSemaphoreGive(mutex_);
        return false;
    }
    scan_requested_ = true;
    operation_error_.clear();
    xSemaphoreGive(mutex_);
    return true;
}

void MusicPlayerService::Deinit() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        stopping_ = true;
        scan_requested_ = false;
        monitor_stop_requested_ = true;
        xSemaphoreGive(mutex_);
    }
    Stop();
    bool monitor_active = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        monitor_active = monitor_task_active_;
        monitor_stop_requested_ = true;
        xSemaphoreGive(mutex_);
    }
    if (monitor_active) {
        while (monitor_active) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (mutex_ != nullptr) {
                xSemaphoreTake(mutex_, portMAX_DELAY);
                monitor_active = monitor_task_active_;
                xSemaphoreGive(mutex_);
            }
            if (!monitor_active) {
                break;
            }
        }
    }
    std::lock_guard<std::mutex> scan_lock(scan_mutex_);
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    initialized_ = false;
}

bool MusicPlayerService::ScanLibrary() {
    return ScanLibrary(false);
}

bool MusicPlayerService::ScanLibrary(bool load_saved_state) {
    std::lock_guard<std::mutex> scan_lock(scan_mutex_);
    std::unique_lock<std::mutex> operation_lock(operation_mutex_);
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (stopping_) {
        xSemaphoreGive(mutex_);
        return false;
    }
    library_status_ = MusicLibraryStatus::kScanning;
    library_message_ = "Scanning music...";
    ++library_revision_;
    operation_error_.clear();
    xSemaphoreGive(mutex_);
    operation_lock.unlock();
    std::vector<MusicTrack> tracks;
    if (file_service_ == nullptr) {
        return FailLibrary(MusicLibraryStatus::kServiceUnavailable, "File service unavailable");
    }
    if (!file_service_->IsMounted() && !file_service_->Init()) {
        return FailLibrary(MusicLibraryStatus::kStorageUnavailable, "SD card unavailable; insert and retry");
    }

    std::vector<FileEntry> root_entries;
    if (!file_service_->ListDirectory("/", root_entries)) {
        return FailLibrary(MusicLibraryStatus::kReadError, "Cannot read music folders; retry");
    }
    for (const auto& entry : root_entries) {
        if (entry.is_directory && NormalizePathKey(entry.name) == "music" &&
            !ScanDirectory("/" + entry.name, 3, tracks)) {
            return FailLibrary(MusicLibraryStatus::kReadError, "Cannot read music folders; retry");
        }
    }
    if (tracks.empty() && !ScanDirectory("/", 3, tracks)) {
        return FailLibrary(MusicLibraryStatus::kReadError, "Cannot read music folders; retry");
    }

    std::sort(tracks.begin(), tracks.end(), TrackPathLess);
    const auto unique_end = std::unique(tracks.begin(), tracks.end(), SameTrackPath);
    tracks.erase(unique_end, tracks.end());

    std::sort(tracks.begin(), tracks.end(), [](const MusicTrack& a, const MusicTrack& b) {
        return a.title < b.title;
    });

    operation_lock.lock();
    int loaded_index = -1;
    MusicPlaybackMode loaded_mode = MusicPlaybackMode::kSequential;
    if (load_saved_state) {
        LoadPlaybackState(tracks, loaded_index, loaded_mode);
    } else if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        loaded_mode = playback_mode_;
        if (current_index_ >= 0 && current_index_ < static_cast<int>(tracks_.size())) {
            loaded_index = FindTrackIndexByPath(tracks, tracks_[current_index_].path);
        }
        xSemaphoreGive(mutex_);
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (stopping_) {
            xSemaphoreGive(mutex_);
            return false;
        }
        tracks_ = std::move(tracks);
        library_status_ = tracks_.empty() ? MusicLibraryStatus::kEmpty : MusicLibraryStatus::kReady;
        library_message_ = tracks_.empty() ? "No supported audio files; add WAV or MP3" : "Ready";
        operation_error_.clear();
        ++library_revision_;
        playback_mode_ = loaded_mode;
        current_index_ = loaded_index;
        xSemaphoreGive(mutex_);
    }

    ESP_LOGI(TAG, "Music scan found %zu tracks", track_count());
    return true;
}

bool MusicPlayerService::FailLibrary(MusicLibraryStatus status, const char* message) {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    const auto audio = audio_.GetState();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool stop_music = FindTrackIndexByPath(tracks_, audio.file_path) >= 0;
    tracks_.clear();
    current_index_ = -1;
    completion_handled_ = true;
    queue_paused_ = true;
    library_status_ = status;
    library_message_ = message;
    operation_error_.clear();
    ++library_revision_;
    xSemaphoreGive(mutex_);
    if (stop_music) {
        audio_.Stop();
    }
    return false;
}

void MusicPlayerService::SetOperationError(const std::string& message) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        operation_error_ = message;
        xSemaphoreGive(mutex_);
    }
}

std::vector<MusicTrack> MusicPlayerService::GetTracks(uint64_t* library_revision) {
    std::vector<MusicTrack> copy;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (library_revision != nullptr) {
            *library_revision = library_revision_;
        }
        if (library_status_ == MusicLibraryStatus::kReady) {
            copy = tracks_;
        }
        xSemaphoreGive(mutex_);
    }
    return copy;
}

size_t MusicPlayerService::track_count() {
    if (mutex_ == nullptr) {
        return 0;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const size_t count = tracks_.size();
    xSemaphoreGive(mutex_);
    return count;
}

MusicPlayerState MusicPlayerService::GetState() {
    const auto audio_state = audio_.GetState();
    if (!audio_state.file_path.empty()) {
        SyncCurrentIndexFromPath(audio_state.file_path);
    }

    MusicPlayerState state;
    state.audio = audio_state;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state.mode = playback_mode_;
        state.track_count = tracks_.size();
        state.current_index = current_index_;
        state.queue_paused = queue_paused_;
        state.library_status = library_status_;
        state.library_message = library_message_;
        state.library_revision = library_revision_;
        state.operation_error = operation_error_;
        if (current_index_ >= 0 && current_index_ < static_cast<int>(tracks_.size())) {
            state.current_title = tracks_[current_index_].title;
        }
        xSemaphoreGive(mutex_);
    }
    return state;
}

MusicPlaybackMode MusicPlayerService::playback_mode() {
    if (mutex_ == nullptr) {
        return MusicPlaybackMode::kSequential;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const auto mode = playback_mode_;
    xSemaphoreGive(mutex_);
    return mode;
}

MusicPlaybackMode MusicPlayerService::TogglePlaybackMode() {
    MusicPlaybackMode mode = MusicPlaybackMode::kSequential;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        switch (playback_mode_) {
            case MusicPlaybackMode::kSequential:
                playback_mode_ = MusicPlaybackMode::kShuffle;
                break;
            case MusicPlaybackMode::kShuffle:
                playback_mode_ = MusicPlaybackMode::kRepeatOne;
                break;
            case MusicPlaybackMode::kRepeatOne:
            default:
                playback_mode_ = MusicPlaybackMode::kSequential;
                break;
        }
        mode = playback_mode_;
        xSemaphoreGive(mutex_);
    }
    SavePlaybackState();
    ESP_LOGI(TAG, "Playback mode changed: %s", ModeName(mode));
    return mode;
}

bool MusicPlayerService::PlayTrack(size_t index, uint64_t library_revision) {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool blocked = playback_blocked_;
    if (blocked) operation_error_ = "Playback is busy with another app";
    xSemaphoreGive(mutex_);
    if (blocked) return false;
    std::unique_lock<std::mutex> operation_lock(operation_mutex_, std::try_to_lock);
    if (!operation_lock.owns_lock()) {
        SetOperationError("Music is busy; retry shortly");
        return false;
    }

    return PlayTrackLocked(index, library_revision);
}

bool MusicPlayerService::PlayTrackLocked(size_t index, uint64_t library_revision) {
    MusicTrack track;
    int mode_value = 0;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (stopping_ || playback_blocked_ || playback_starting_ ||
        library_status_ != MusicLibraryStatus::kReady || index >= tracks_.size() ||
        (library_revision != 0 && library_revision != library_revision_)) {
        operation_error_ = stopping_ ? "Music is stopping" :
            playback_blocked_ ? "Playback is busy with another app" :
            playback_starting_ ? "Playback is starting" :
            library_status_ != MusicLibraryStatus::kReady ? library_message_ :
            "Music library changed; choose a song again";
        xSemaphoreGive(mutex_);
        return false;
    }
    operation_error_.clear();
    playback_starting_ = true;
    current_index_ = static_cast<int>(index);
    completion_handled_ = false;
    queue_paused_ = false;
    track = tracks_[index];
    switch (playback_mode_) {
        case MusicPlaybackMode::kShuffle:
            mode_value = 1;
            break;
        case MusicPlaybackMode::kRepeatOne:
            mode_value = 2;
            break;
        case MusicPlaybackMode::kSequential:
        default:
            mode_value = 0;
            break;
    }
    xSemaphoreGive(mutex_);

    Settings settings(kMusicNamespace, true);
    settings.SetInt(kModeKey, mode_value);
    settings.SetInt(kTrackIndexKey, static_cast<int>(index));
    settings.SetString(kTrackPathKey, track.path);

    const bool ok = audio_.PlayFile(track.path, track.title);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    playback_starting_ = false;
    xSemaphoreGive(mutex_);
    if (!ok) {
        const auto state = audio_.GetState();
        SetOperationError(state.message.empty() ? "Cannot play this file" : state.message);
        ESP_LOGW(TAG, "Cannot play track: %s", track.path.c_str());
    }
    return ok;
}

bool MusicPlayerService::PlayPrevious() {
    int index = -1;
    uint64_t revision = 0;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        revision = library_revision_;
        if (!tracks_.empty()) {
            index = current_index_ <= 0 ? static_cast<int>(tracks_.size() - 1) : current_index_ - 1;
        }
        xSemaphoreGive(mutex_);
    }
    return PlayTrack(index >= 0 ? static_cast<size_t>(index) : 0, revision);
}

bool MusicPlayerService::PlayNext() {
    int index = -1;
    uint64_t revision = 0;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        revision = library_revision_;
        index = NextIndexForManualNextLocked();
        xSemaphoreGive(mutex_);
    }
    return PlayTrack(index >= 0 ? static_cast<size_t>(index) : 0, revision);
}

bool MusicPlayerService::TogglePlayPause() {
    std::unique_lock<std::mutex> operation_lock(operation_mutex_, std::try_to_lock);
    if (!operation_lock.owns_lock() || mutex_ == nullptr) {
        SetOperationError("Music is busy; retry shortly");
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (stopping_ || playback_blocked_) {
        operation_error_ = stopping_ ? "Music is stopping" : "Playback is busy with another app";
        xSemaphoreGive(mutex_);
        return false;
    }
    operation_error_.clear();
    xSemaphoreGive(mutex_);
    const auto state = audio_.GetState();
    if (state.status == AudioPlaybackStatus::kPaused) {
        audio_.Resume();
        const auto resumed = audio_.GetState();
        const bool ok = resumed.status == AudioPlaybackStatus::kPlaying ||
                        resumed.status == AudioPlaybackStatus::kCompleted;
        xSemaphoreTake(mutex_, portMAX_DELAY);
        queue_paused_ = !ok;
        if (!ok) operation_error_ = resumed.message.empty() ? "Cannot resume playback" : resumed.message;
        xSemaphoreGive(mutex_);
        return ok;
    }
    if (state.status == AudioPlaybackStatus::kPlaying || state.status == AudioPlaybackStatus::kLoading) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        queue_paused_ = true;
        completion_handled_ = true;
        xSemaphoreGive(mutex_);
        audio_.Pause();
        return true;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    int index = state.status == AudioPlaybackStatus::kCompleted ? NextIndexForCompletedLocked() : -1;
    if (index < 0) index = current_index_ < 0 ? 0 : current_index_;
    const uint64_t revision = library_revision_;
    xSemaphoreGive(mutex_);
    return PlayTrackLocked(static_cast<size_t>(index), revision);
}

void MusicPlayerService::Pause() {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        queue_paused_ = true;
        completion_handled_ = true;
        xSemaphoreGive(mutex_);
    }
    audio_.Pause();
}

void MusicPlayerService::Resume() {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    bool blocked = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        blocked = playback_blocked_ || stopping_;
        if (!blocked) {
            queue_paused_ = false;
            operation_error_.clear();
        }
        xSemaphoreGive(mutex_);
    }
    if (blocked) {
        return;
    }
    audio_.Resume();
}

bool MusicPlayerService::SuspendPlaybackHardware() {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        queue_paused_ = true;
        completion_handled_ = true;
        xSemaphoreGive(mutex_);
    }
    return audio_.SuspendPlaybackHardware();
}

bool MusicPlayerService::SetPlaybackBlocked(bool blocked) {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    playback_blocked_ = blocked;
    xSemaphoreGive(mutex_);
    return true;
}

void MusicPlayerService::Stop() {
    std::lock_guard<std::mutex> operation_lock(operation_mutex_);
    audio_.Stop();
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        completion_handled_ = true;
        queue_paused_ = true;
        operation_error_.clear();
        xSemaphoreGive(mutex_);
    }
}

bool MusicPlayerService::ReleasePlaybackHardware() {
    return audio_.ReleasePlaybackHardware();
}

bool MusicPlayerService::Refresh() {
    std::unique_lock<std::mutex> operation_lock(operation_mutex_, std::try_to_lock);
    if (!operation_lock.owns_lock() || mutex_ == nullptr) {
        return false;
    }
    const auto state = audio_.GetState();
    if (!state.file_path.empty()) {
        SyncCurrentIndexFromPath(state.file_path);
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (state.status == AudioPlaybackStatus::kPlaying || state.status == AudioPlaybackStatus::kLoading) {
        completion_handled_ = false;
        xSemaphoreGive(mutex_);
        return false;
    }
    if (state.status != AudioPlaybackStatus::kCompleted || completion_handled_ ||
        queue_paused_ || stopping_ || playback_blocked_ ||
        library_status_ != MusicLibraryStatus::kReady ||
        FindTrackIndexByPath(tracks_, state.file_path) < 0) {
        xSemaphoreGive(mutex_);
        return false;
    }
    completion_handled_ = true;
    const int index = NextIndexForCompletedLocked();
    const uint64_t revision = library_revision_;
    xSemaphoreGive(mutex_);
    return index >= 0 && PlayTrackLocked(static_cast<size_t>(index), revision);
}

bool MusicPlayerService::SetVolume(int volume) {
    return audio_.SetVolume(volume);
}

int MusicPlayerService::volume() const {
    return audio_.volume();
}

void MusicPlayerService::MonitorTaskEntry(void* arg) {
    static_cast<MusicPlayerService*>(arg)->MonitorTask();
}

void MusicPlayerService::MonitorTask() {
    while (true) {
        bool stop_requested = false;
        bool scan_requested = false;
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            stop_requested = monitor_stop_requested_;
            scan_requested = scan_requested_;
            scan_requested_ = false;
            xSemaphoreGive(mutex_);
        }
        if (stop_requested) {
            break;
        }
        if (scan_requested) {
            ScanLibrary(false);
        }
        Refresh();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        monitor_task_active_ = false;
        monitor_task_ = nullptr;
        xSemaphoreGive(mutex_);
    }
    vTaskDelete(nullptr);
}

bool MusicPlayerService::ScanDirectory(const std::string& path, int depth, std::vector<MusicTrack>& tracks) {
    if (depth < 0) {
        return true;
    }
    if (file_service_ == nullptr) {
        return false;
    }

    std::vector<FileEntry> entries;
    if (!file_service_->ListDirectory(path, entries)) {
        return false;
    }

    for (const auto& entry : entries) {
        if (entry.is_directory) {
            const std::string next_path = path == "/" ? "/" + entry.name : path + "/" + entry.name;
            if (!ScanDirectory(next_path, depth - 1, tracks)) {
                return false;
            }
            continue;
        }
        if (!AudioService::IsSupportedAudioFile(entry.path)) {
            continue;
        }
        MusicTrack track;
        const size_t dot = entry.name.find_last_of('.');
        track.title = dot == std::string::npos ? entry.name : entry.name.substr(0, dot);
        track.path = entry.path;
        track.size = entry.size;
        tracks.push_back(track);
        ESP_LOGD(TAG, "Found audio track: %s (%zu bytes)", track.path.c_str(), track.size);
    }
    return true;
}

void MusicPlayerService::LoadPlaybackState(const std::vector<MusicTrack>& tracks, int& index,
                                           MusicPlaybackMode& mode) {
    Settings settings(kMusicNamespace, false);
    const int mode_value = settings.GetInt(kModeKey, 0);
    switch (mode_value) {
        case 1:
            mode = MusicPlaybackMode::kShuffle;
            break;
        case 2:
            mode = MusicPlaybackMode::kRepeatOne;
            break;
        case 0:
        default:
            mode = MusicPlaybackMode::kSequential;
            break;
    }

    const std::string path = settings.GetString(kTrackPathKey, "");
    index = FindTrackIndexByPath(tracks, path);
    if (index < 0) {
        const int saved_index = settings.GetInt(kTrackIndexKey, -1);
        if (saved_index >= 0 && saved_index < static_cast<int>(tracks.size())) {
            index = saved_index;
        }
    }

    ESP_LOGI(TAG, "Loaded playback state: mode=%d index=%d path=%s",
             mode_value, index, path.c_str());
}

void MusicPlayerService::SavePlaybackState() {
    MusicPlaybackMode mode = MusicPlaybackMode::kSequential;
    int current_index = -1;
    std::string current_path;

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        mode = playback_mode_;
        current_index = current_index_;
        if (current_index_ >= 0 && current_index_ < static_cast<int>(tracks_.size())) {
            current_path = tracks_[current_index_].path;
        }
        xSemaphoreGive(mutex_);
    }

    int mode_value = 0;
    switch (mode) {
        case MusicPlaybackMode::kShuffle:
            mode_value = 1;
            break;
        case MusicPlaybackMode::kRepeatOne:
            mode_value = 2;
            break;
        case MusicPlaybackMode::kSequential:
        default:
            mode_value = 0;
            break;
    }

    Settings settings(kMusicNamespace, true);
    settings.SetInt(kModeKey, mode_value);
    settings.SetInt(kTrackIndexKey, current_index);
    if (!current_path.empty()) {
        settings.SetString(kTrackPathKey, current_path);
    }
}

void MusicPlayerService::SyncCurrentIndexFromPath(const std::string& path) {
    if (path.empty() || mutex_ == nullptr) {
        return;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const int index = FindTrackIndexByPath(tracks_, path);
    if (index >= 0) {
        current_index_ = index;
    }
    xSemaphoreGive(mutex_);
}

int MusicPlayerService::FindTrackIndexByPath(const std::vector<MusicTrack>& tracks, const std::string& path) const {
    if (path.empty()) {
        return -1;
    }

    const std::string key = NormalizePathKey(path);
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (NormalizePathKey(tracks[i].path) == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int MusicPlayerService::PickRandomTrackIndexLocked() const {
    if (tracks_.empty()) {
        return -1;
    }
    if (tracks_.size() == 1) {
        return 0;
    }

    size_t index = static_cast<size_t>(esp_random() % tracks_.size());
    if (current_index_ >= 0 && index == static_cast<size_t>(current_index_)) {
        index = (index + 1) % tracks_.size();
    }
    return static_cast<int>(index);
}

int MusicPlayerService::NextIndexForCompletedLocked() const {
    if (tracks_.empty()) {
        return -1;
    }

    switch (playback_mode_) {
        case MusicPlaybackMode::kRepeatOne:
            return current_index_ < 0 ? 0 : current_index_;
        case MusicPlaybackMode::kShuffle:
            return PickRandomTrackIndexLocked();
        case MusicPlaybackMode::kSequential:
        default:
            if (current_index_ >= 0 && current_index_ < static_cast<int>(tracks_.size() - 1)) {
                return current_index_ + 1;
            }
            break;
    }
    return -1;
}

int MusicPlayerService::NextIndexForManualNextLocked() const {
    if (tracks_.empty()) {
        return -1;
    }
    if (playback_mode_ == MusicPlaybackMode::kShuffle) {
        return PickRandomTrackIndexLocked();
    }
    return current_index_ < 0 || current_index_ >= static_cast<int>(tracks_.size() - 1)
        ? 0 : current_index_ + 1;
}

}  // namespace rodakos
