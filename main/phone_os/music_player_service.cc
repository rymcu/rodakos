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
    if (initialized_) {
        return ScanLibrary(false);
    }
    if (!audio_.Init()) {
        ESP_LOGW(TAG, "Audio service unavailable");
    }
    ScanLibrary(true);
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        monitor_stop_requested_ = false;
        monitor_task_active_ = true;
        xSemaphoreGive(mutex_);
    }
    const BaseType_t task_ret = xTaskCreate(
        MonitorTaskEntry, "music_player", kMonitorTaskStackWords, this, 4, &monitor_task_);
    if (task_ret != pdPASS) {
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            monitor_task_active_ = false;
            monitor_task_ = nullptr;
            xSemaphoreGive(mutex_);
        }
        ESP_LOGW(TAG, "Failed to create monitor task");
    }
    initialized_ = true;
    ESP_LOGI(TAG, "Music player initialized with %zu tracks", track_count());
    return true;
}

void MusicPlayerService::Deinit() {
    Stop();
    bool monitor_active = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        monitor_active = monitor_task_active_;
        monitor_stop_requested_ = true;
        xSemaphoreGive(mutex_);
    }
    if (monitor_active) {
        for (int waited = 0; waited < 1500; waited += 50) {
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
    initialized_ = false;
}

bool MusicPlayerService::ScanLibrary() {
    return ScanLibrary(false);
}

bool MusicPlayerService::ScanLibrary(bool load_saved_state) {
    std::vector<MusicTrack> tracks;
    if (file_service_ == nullptr) {
        ESP_LOGW(TAG, "No file service available");
        return false;
    }
    if (!file_service_->IsMounted() && !file_service_->Init()) {
        ESP_LOGW(TAG, "SD card is not mounted");
        return false;
    }
    ESP_LOGI(TAG, "SD card mounted at %s", file_service_->GetMountPoint());

    std::vector<FileEntry> root_entries;
    if (file_service_->ListDirectory("/", root_entries)) {
        ESP_LOGI(TAG, "Music scan root has %zu entries", root_entries.size());
        for (const auto& entry : root_entries) {
            ESP_LOGD(TAG, "Root entry: %s dir=%d size=%zu path=%s",
                     entry.name.c_str(), entry.is_directory ? 1 : 0, entry.size, entry.path.c_str());
            if (entry.is_directory && NormalizePathKey(entry.name) == "music") {
                const std::string scan_path = "/" + entry.name;
                ESP_LOGI(TAG, "Scanning music directory: %s", scan_path.c_str());
                ScanDirectory(scan_path, 3, tracks);
            }
        }
    } else {
        ESP_LOGW(TAG, "Failed to list SD card root");
    }

    if (tracks.empty()) {
        ESP_LOGI(TAG, "No tracks in /music; scanning SD root fallback");
        ScanDirectory("/", 3, tracks);
    }

    std::sort(tracks.begin(), tracks.end(), TrackPathLess);
    const auto unique_end = std::unique(tracks.begin(), tracks.end(), SameTrackPath);
    tracks.erase(unique_end, tracks.end());

    std::sort(tracks.begin(), tracks.end(), [](const MusicTrack& a, const MusicTrack& b) {
        return a.title < b.title;
    });

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
        tracks_ = std::move(tracks);
        playback_mode_ = loaded_mode;
        current_index_ = loaded_index;
        xSemaphoreGive(mutex_);
    }

    ESP_LOGI(TAG, "Music scan found %zu tracks", track_count());
    return true;
}

std::vector<MusicTrack> MusicPlayerService::GetTracks() {
    std::vector<MusicTrack> copy;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        copy = tracks_;
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
    Refresh();
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
        completion_handled_ = false;
        queue_paused_ = false;
        mode = playback_mode_;
        xSemaphoreGive(mutex_);
    }
    SavePlaybackState();
    ESP_LOGI(TAG, "Playback mode changed: %s", ModeName(mode));
    return mode;
}

bool MusicPlayerService::PlayTrack(size_t index) {
    if (mutex_ == nullptr) {
        return false;
    }

    MusicTrack track;
    int mode_value = 0;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (playback_starting_ || index >= tracks_.size()) {
        xSemaphoreGive(mutex_);
        return false;
    }
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
        ESP_LOGW(TAG, "Cannot play track: %s", track.path.c_str());
    }
    return ok;
}

bool MusicPlayerService::PlayPrevious() {
    int index = -1;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (!tracks_.empty()) {
            index = current_index_ <= 0 ? static_cast<int>(tracks_.size() - 1) : current_index_ - 1;
        }
        xSemaphoreGive(mutex_);
    }
    return index >= 0 && PlayTrack(static_cast<size_t>(index));
}

bool MusicPlayerService::PlayNext() {
    int index = -1;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        index = NextIndexForManualNextLocked();
        xSemaphoreGive(mutex_);
    }
    return index >= 0 && PlayTrack(static_cast<size_t>(index));
}

bool MusicPlayerService::TogglePlayPause() {
    const auto state = audio_.GetState();
    if (state.status == AudioPlaybackStatus::kPaused) {
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            queue_paused_ = false;
            xSemaphoreGive(mutex_);
        }
        audio_.Resume();
        return true;
    }

    if (state.status == AudioPlaybackStatus::kPlaying || state.status == AudioPlaybackStatus::kLoading) {
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            queue_paused_ = true;
            completion_handled_ = true;
            xSemaphoreGive(mutex_);
        }
        audio_.Pause();
        return true;
    }

    if (state.status == AudioPlaybackStatus::kCompleted) {
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            queue_paused_ = false;
            completion_handled_ = false;
            xSemaphoreGive(mutex_);
        }
        if (PlayFromCompletedState()) {
            return true;
        }
    }

    int index = -1;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (!tracks_.empty()) {
            index = current_index_ < 0 ? 0 : current_index_;
            queue_paused_ = false;
        }
        xSemaphoreGive(mutex_);
    }
    return index >= 0 && PlayTrack(static_cast<size_t>(index));
}

void MusicPlayerService::Pause() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        queue_paused_ = true;
        completion_handled_ = true;
        xSemaphoreGive(mutex_);
    }
    audio_.Pause();
}

void MusicPlayerService::Resume() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        queue_paused_ = false;
        xSemaphoreGive(mutex_);
    }
    audio_.Resume();
}

void MusicPlayerService::Stop() {
    audio_.Stop();
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        completion_handled_ = true;
        queue_paused_ = true;
        xSemaphoreGive(mutex_);
    }
}

void MusicPlayerService::ReleasePlaybackHardware() {
    audio_.ReleasePlaybackHardware();
}

bool MusicPlayerService::Refresh() {
    const auto state = audio_.GetState();
    if (!state.file_path.empty()) {
        SyncCurrentIndexFromPath(state.file_path);
    }

    if (state.status == AudioPlaybackStatus::kPlaying || state.status == AudioPlaybackStatus::kLoading) {
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            completion_handled_ = false;
            xSemaphoreGive(mutex_);
        }
        return false;
    }

    if (state.status != AudioPlaybackStatus::kCompleted) {
        return false;
    }

    bool should_continue = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (!completion_handled_ && !tracks_.empty()) {
            completion_handled_ = true;
            should_continue = !queue_paused_;
        }
        xSemaphoreGive(mutex_);
    }
    return should_continue && PlayFromCompletedState();
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
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            stop_requested = monitor_stop_requested_;
            xSemaphoreGive(mutex_);
        }
        if (stop_requested) {
            break;
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

void MusicPlayerService::ScanDirectory(const std::string& path, int depth, std::vector<MusicTrack>& tracks) {
    if (depth < 0 || file_service_ == nullptr) {
        return;
    }

    std::vector<FileEntry> entries;
    if (!file_service_->ListDirectory(path, entries)) {
        return;
    }

    for (const auto& entry : entries) {
        if (entry.is_directory) {
            const std::string next_path = path == "/" ? "/" + entry.name : path + "/" + entry.name;
            ScanDirectory(next_path, depth - 1, tracks);
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

bool MusicPlayerService::PlayFromCompletedState() {
    int index = -1;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        index = NextIndexForCompletedLocked();
        xSemaphoreGive(mutex_);
    }
    return index >= 0 && PlayTrack(static_cast<size_t>(index));
}

}  // namespace rodakos
