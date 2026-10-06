#include "phone_os/recording_service.h"

#include "phone_os/audio_focus_service.h"
#include "rodakos_adapters/audio_codec_input.h"
#include "rodakos_adapters/file_service.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <inttypes.h>
#include <limits>
#include <fcntl.h>
#include <unistd.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "RecordingService";
constexpr const char* kAudioInputOwner = "recording-service";
constexpr int kAudioInputPriority = 20;
constexpr const char* kRecordingsDir = "/recordings";
constexpr int64_t kMinValidUnixTime = 1700000000;
// Keep collision recovery bounded; a full directory is a storage error, not a
// reason to hold audio focus and a worker for thousands of SD probes.
constexpr int kMaxNameSuffix = 256;
constexpr size_t kRecordBufferSize = 4096;
constexpr uint32_t kTaskStackWords = 6144;
constexpr size_t kMaxWavDataBytes = UINT32_MAX - 36U;

class OperationLock {
public:
    explicit OperationLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    ~OperationLock() { if (mutex_) xSemaphoreGive(mutex_); }
private:
    SemaphoreHandle_t mutex_;
};

bool IsValidRecordingConfig(const RecordingConfig& config) {
    if (config.sample_rate == 0 || config.sample_rate > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        (config.channels != 1 && config.channels != 2) || config.input_channels == 0 ||
        config.input_channels > 16 || config.bits_per_sample != 16 ||
        static_cast<uint64_t>(config.sample_rate) * config.channels * 2 > UINT32_MAX) return false;
    const uint32_t available = (1U << config.input_channels) - 1U;
    if ((config.input_channel_mask & ~available) != 0) return false;
    unsigned selected = config.input_channel_mask == 0 ? config.input_channels : 0;
    for (uint16_t mask = config.input_channel_mask; mask != 0; mask >>= 1) selected += mask & 1U;
    return selected == config.channels;
}

void WriteLe16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value & 0xff);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

void WriteLe32(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value & 0xff);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xff);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xff);
    out[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

bool WriteWavHeader(FILE* fp, const RecordingConfig& config, size_t data_bytes) {
    if (fp == nullptr || data_bytes > kMaxWavDataBytes || !IsValidRecordingConfig(config) ||
        data_bytes % (config.channels * sizeof(int16_t)) != 0) {
        return false;
    }

    const uint32_t encoded_data = static_cast<uint32_t>(data_bytes);
    const uint16_t block_align =
        static_cast<uint16_t>(config.channels * (config.bits_per_sample / 8));
    const uint32_t byte_rate = config.sample_rate * block_align;
    uint8_t header[44] = {};
    std::memcpy(header, "RIFF", 4);
    WriteLe32(header + 4, 36U + encoded_data);
    std::memcpy(header + 8, "WAVE", 4);
    std::memcpy(header + 12, "fmt ", 4);
    WriteLe32(header + 16, 16);
    WriteLe16(header + 20, 1);
    WriteLe16(header + 22, config.channels);
    WriteLe32(header + 24, config.sample_rate);
    WriteLe32(header + 28, byte_rate);
    WriteLe16(header + 32, block_align);
    WriteLe16(header + 34, config.bits_per_sample);
    std::memcpy(header + 36, "data", 4);
    WriteLe32(header + 40, encoded_data);

    return std::fwrite(header, 1, sizeof(header), fp) == sizeof(header) && !std::ferror(fp);
}

uint16_t ReadLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ReadLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

enum class WavReadResult { kValid, kInvalid, kIoError };

WavReadResult ReadWavDurationMs(const std::string& full_path, size_t file_size, uint32_t& duration) {
    FILE* fp = std::fopen(full_path.c_str(), "rb");
    if (fp == nullptr) {
        return WavReadResult::kIoError;
    }

    uint8_t header[44] = {};
    const bool ok = std::fread(header, 1, sizeof(header), fp) == sizeof(header) &&
                    std::memcmp(header, "RIFF", 4) == 0 &&
                    std::memcmp(header + 8, "WAVE", 4) == 0 &&
                    std::memcmp(header + 12, "fmt ", 4) == 0 &&
                    std::memcmp(header + 36, "data", 4) == 0;
    const bool read_ok = !std::ferror(fp);
    const bool closed = std::fclose(fp) == 0;
    if (!read_ok || !closed) return WavReadResult::kIoError;
    if (!ok) return WavReadResult::kInvalid;

    const uint16_t channels = ReadLe16(header + 22);
    const uint32_t sample_rate = ReadLe32(header + 24);
    const uint16_t bits_per_sample = ReadLe16(header + 34);
    const uint32_t data_bytes = ReadLe32(header + 40);
    const uint64_t bytes_per_second = static_cast<uint64_t>(sample_rate) * channels * (bits_per_sample / 8U);
    if (ReadLe16(header + 20) != 1 || (channels != 1 && channels != 2) || bits_per_sample != 16 ||
        bytes_per_second == 0 || bytes_per_second > UINT32_MAX || ReadLe32(header + 28) != bytes_per_second ||
        ReadLe16(header + 32) != channels * 2 || data_bytes == 0 || data_bytes % (channels * 2) != 0 ||
        static_cast<uint64_t>(data_bytes) + 44 != file_size || data_bytes > kMaxWavDataBytes ||
        ReadLe32(header + 4) != data_bytes + 36) {
        return WavReadResult::kInvalid;
    }
    duration = static_cast<uint32_t>(std::min<uint64_t>((data_bytes * 1000ULL) / bytes_per_second, UINT32_MAX));
    return WavReadResult::kValid;
}

std::string BasenameWithoutExtension(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

bool EndsWithCaseInsensitive(const std::string& text, const char* suffix) {
    const size_t suffix_len = std::strlen(suffix);
    if (text.size() < suffix_len) {
        return false;
    }
    const size_t offset = text.size() - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(text[offset + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

uint16_t PeakAbs16(const uint8_t* data, size_t bytes) {
    const auto* samples = reinterpret_cast<const int16_t*>(data);
    const size_t count = bytes / sizeof(int16_t);
    uint16_t peak = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t value = samples[i];
        const uint16_t abs_value = static_cast<uint16_t>(
            value == INT16_MIN ? INT16_MAX : (value < 0 ? -value : value));
        if (abs_value > peak) {
            peak = abs_value;
        }
    }
    return peak;
}
}  // namespace

RecordingService::RecordingService(AudioCodecInput& input,
                                   FileService* file_service,
                                   AudioFocusService* audio_focus)
    : input_(input), file_service_(file_service), audio_focus_(audio_focus) {
    mutex_ = xSemaphoreCreateMutex();
    operation_mutex_ = xSemaphoreCreateMutex();
}

RecordingService::~RecordingService() {
    RequestStop();
    // Stop has a UI-facing timeout; destruction cannot free a task's state or
    // codec owner while its file/driver operation is still in flight.
    while (HasTask()) vTaskDelay(pdMS_TO_TICKS(10));
    if (operation_mutex_ != nullptr) vSemaphoreDelete(operation_mutex_);
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool RecordingService::Start(const RecordingConfig& config) {
    OperationLock operation(operation_mutex_);
    if (mutex_ == nullptr || operation_mutex_ == nullptr || HasTask()) return false;
    if (!IsValidRecordingConfig(config)) {
        SetError("Unsupported recording format");
        return false;
    }
    const std::string path = BuildRecordingPath();
    if (path.empty()) {
        SetError("Failed to choose a recording path");
        return false;
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        stop_requested_ = false;
        active_config_ = config;
        task_active_ = true;
        task_ = nullptr;
        state_ = {};
        state_.status = RecordingStatus::kStarting;
        state_.path = path;
        state_.full_path = FullPath(path);
        state_.title = BasenameWithoutExtension(path);
        state_.message = "Starting";
        state_.sample_rate = config.sample_rate;
        state_.channels = config.channels;
        state_.bits_per_sample = config.bits_per_sample;
        state_.gain = config.gain;
        xSemaphoreGive(mutex_);
    }
    std::string error;
    if (!PrepareStorage(error) || (!ShouldStop() && !RequestAudioFocus())) {
        SetError(error.empty() ? "Audio focus unavailable" : error);
        ReleaseAudioFocus();
        ClearTask();
        return false;
    }
    if (ShouldStop()) {
        ReleaseAudioFocus();
        SetState(RecordingStatus::kIdle, "Cancelled");
        ClearTask();
        return false;
    }
    TaskHandle_t task_handle = nullptr;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    const BaseType_t task_ret = xTaskCreatePinnedToCore(
        RecordingTaskEntry, "recorder", kTaskStackWords, this, 5, &task_handle, 0);
#else
    const BaseType_t task_ret = xTaskCreate(
        RecordingTaskEntry, "recorder", kTaskStackWords, this, 5, &task_handle);
#endif
    if (task_ret != pdPASS) {
        ReleaseAudioFocus();
        SetError("No memory for recording task");
        ClearTask();
        ESP_LOGE(TAG, "Failed to create recording task");
        return false;
    }
    StoreTaskHandle(task_handle);
    return true;
}

void RecordingService::Stop() {
    RequestStop();
    // Do not hold operation_mutex_ while joining; the task owns the active
    // recording state until all file and focus cleanup has completed.
    JoinTask(2500);
}

void RecordingService::RequestStop() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        stop_requested_ = true;
        if (state_.status == RecordingStatus::kRecording ||
            state_.status == RecordingStatus::kStarting) {
            state_.status = RecordingStatus::kStopping;
            state_.message = "Stopping";
        }
        xSemaphoreGive(mutex_);
    }
}

RecordingState RecordingService::GetState() {
    RecordingState copy;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        copy = state_;
        xSemaphoreGive(mutex_);
    }
    return copy;
}

std::vector<RecordingEntry> RecordingService::GetRecordings() {
    std::vector<RecordingEntry> copy;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        copy = recordings_;
        xSemaphoreGive(mutex_);
    }
    return copy;
}

bool RecordingService::RefreshRecordings() {
    OperationLock operation(operation_mutex_);
    if (HasTask()) return false;
    return RefreshRecordingsLocked();
}

bool RecordingService::RefreshRecordingsLocked() {
    std::string error;
    if (!PrepareStorage(error)) {
        SetLibraryError(error);
        return false;
    }

    std::vector<FileEntry> entries;
    if (!file_service_->ListDirectory(kRecordingsDir, entries)) {
        SetLibraryError("Failed to list recordings");
        return false;
    }

    std::vector<RecordingEntry> recordings;
    for (const auto& entry : entries) {
        if (entry.is_directory || !IsRecordingFile(entry.name)) {
            continue;
        }
        RecordingEntry recording;
        recording.title = BasenameWithoutExtension(entry.name);
        recording.path = std::string(kRecordingsDir) + "/" + entry.name;
        recording.full_path = FullPath(recording.path);
        recording.size = entry.size;
        const auto read_result = ReadWavDurationMs(recording.full_path, entry.size, recording.duration_ms);
        if (read_result == WavReadResult::kIoError) {
            SetLibraryError("Cannot read recording file details");
            return false;
        }
        if (read_result == WavReadResult::kInvalid) continue;
        recording.modified_time = entry.modified_time;
        recordings.push_back(std::move(recording));
    }

    std::sort(recordings.begin(), recordings.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.modified_time != rhs.modified_time) {
            return lhs.modified_time > rhs.modified_time;
        }
        return lhs.path > rhs.path;
    });

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        recordings_ = std::move(recordings);
        state_.library_error.clear();
        xSemaphoreGive(mutex_);
    }
    return true;
}

bool RecordingService::DeleteRecording(const std::string& path) {
    OperationLock operation(operation_mutex_);
    if (HasTask()) {
        return false;
    }
    std::string error;
    if (!PrepareStorage(error)) {
        SetLibraryError(error);
        return false;
    }
    if (!file_service_->DeleteFile(path)) {
        SetError("Failed to delete recording");
        return false;
    }
    RefreshRecordingsLocked();
    return true;
}

bool RecordingService::IsRecordingFile(const std::string& name) {
    return EndsWithCaseInsensitive(name, ".wav");
}

void RecordingService::RecordingTaskEntry(void* arg) {
    static_cast<RecordingService*>(arg)->RecordingTask();
    vTaskDelete(nullptr);
}

void RecordingService::RecordingTask() {
    const RecordingConfig config = active_config_;
    const RecordingState start_state = GetState();
    const std::string base_path = start_state.path;
    bool completed = false;
    std::string error;
    bool cancelled = ShouldStop();
    for (int suffix = 0; !cancelled && suffix <= kMaxNameSuffix; ++suffix) {
        const std::string candidate = CandidateRecordingPath(base_path, suffix);
        bool collision = false;
        bool lease_result = file_service_ != nullptr && file_service_->WithWriteLease(candidate, [&]() {
            return RecordWithinLease(config, candidate, error, cancelled, collision);
        });
        if (lease_result) {
            completed = true;
            break;
        }
        if (collision) continue;
        if (!error.empty()) break;
        if (errno == EBUSY) {
            // Another writer owns this candidate. The lease is path scoped, so
            // try the next exclusive name instead of treating the whole
            // recordings directory as unavailable.
            continue;
        }
        error = "Recording path lease unavailable";
    }
    if (!completed && !cancelled && error.empty()) {
        error = "No unused recording filename available";
    }
    ReleaseAudioFocus();
    if (completed) {
        // The lease must be gone before directory enumeration. A library
        // refresh failure is retained separately from the successful file.
        RefreshRecordingsLocked();
    } else if (!error.empty()) {
        SetError(error);
    } else if (cancelled) {
        SetState(RecordingStatus::kIdle, "Cancelled");
    } else {
        SetError("Recording failed");
    }
    ClearTask();
}

bool RecordingService::RecordWithinLease(const RecordingConfig& config, const std::string& path,
                                         std::string& error, bool& cancelled, bool& collision) {
    std::string full_path;
    FILE* fp = CreateRecordingFile(path, full_path, error, collision);
    if (fp == nullptr) return false;
    uint8_t* buffer = nullptr;
    size_t bytes_written = 0;
    uint16_t peak = 0;
    bool failed = false;
    bool owned_file = true;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_.path = path;
    state_.full_path = full_path;
    state_.title = BasenameWithoutExtension(path);
    xSemaphoreGive(mutex_);
    if (!WriteWavHeader(fp, config, 0)) {
        failed = true;
        error = "Cannot write WAV header";
    } else if (ShouldStop()) {
        cancelled = true;
    } else if (!input_.OpenForOwner(kAudioInputOwner, kAudioInputPriority, config.sample_rate,
                                    config.input_channels, config.bits_per_sample, config.gain,
                                    config.input_channel_mask)) {
        failed = true;
        error = "Audio ADC unavailable";
    } else if (ShouldStop()) {
        cancelled = true;
    } else {
        buffer = static_cast<uint8_t*>(heap_caps_malloc(kRecordBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buffer == nullptr) buffer = static_cast<uint8_t*>(heap_caps_malloc(kRecordBufferSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (buffer == nullptr) {
            failed = true;
            error = "No recording buffer";
        } else {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            if (!stop_requested_) { state_.status = RecordingStatus::kRecording; state_.message = "Recording"; }
            xSemaphoreGive(mutex_);
            while (!ShouldStop()) {
                if (bytes_written > kMaxWavDataBytes - kRecordBufferSize) { failed = true; error = "WAV size limit reached"; break; }
                if (!input_.ReadForOwner(kAudioInputOwner, buffer, static_cast<int>(kRecordBufferSize))) { failed = true; error = "Audio read failed"; break; }
                peak = std::max(peak, PeakAbs16(buffer, kRecordBufferSize));
                const size_t written = std::fwrite(buffer, 1, kRecordBufferSize, fp);
                if (written != kRecordBufferSize || std::ferror(fp)) { failed = true; error = "SD write failed"; break; }
                bytes_written += written;
                UpdateProgress(bytes_written);
            }
            cancelled = !failed && bytes_written == 0;
        }
    }
    if (buffer != nullptr) heap_caps_free(buffer);
    input_.CloseForOwner(kAudioInputOwner);
    if (!failed && !cancelled) {
        if (std::fseek(fp, 0, SEEK_SET) != 0) { failed = true; error = "Cannot seek to finalize WAV header"; }
        else if (!WriteWavHeader(fp, config, bytes_written)) { failed = true; error = "Cannot finalize WAV header"; }
    }
    if (std::fflush(fp) != 0 || std::ferror(fp)) { if (!failed) error = "Cannot flush recording file"; failed = true; }
    if (std::fclose(fp) != 0) { if (!failed) error = "Cannot close recording file"; failed = true; }
    if ((failed || cancelled) && owned_file && std::remove(full_path.c_str()) != 0) {
        if (error.empty()) error = "Cannot remove cancelled recording";
        else error += "; incomplete recording could not be removed";
        failed = true;
    }
    if (failed) {
        SetError(error.empty() ? "Recording failed" : error);
        return false;
    }
    if (cancelled) {
        SetState(RecordingStatus::kIdle, "Cancelled");
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    state_.status = RecordingStatus::kCompleted;
    state_.message = "Saved";
    state_.bytes_written = bytes_written;
    state_.file_size = bytes_written + 44;
    state_.duration_ms = DurationForBytes(bytes_written);
    state_.last_error.clear();
    xSemaphoreGive(mutex_);
    ESP_LOGI(TAG, "Saved recording: %s (%u bytes), peak=%u/%u", path.c_str(),
             static_cast<unsigned>(bytes_written + 44), static_cast<unsigned>(peak), static_cast<unsigned>(INT16_MAX));
    return true;
}

bool RecordingService::PrepareStorage(std::string& error) {
    if (file_service_ == nullptr) {
        error = "File service unavailable";
        return false;
    }
    if (!file_service_->IsMounted() && !file_service_->Init()) {
        error = "SD card unavailable";
        return false;
    }
    if (!file_service_->Exists(kRecordingsDir) && !file_service_->CreateDirectory(kRecordingsDir)) {
        error = "Cannot create /recordings";
        return false;
    }
    return true;
}

std::string RecordingService::BuildRecordingPath() {
    char name[48] = {};
    const std::time_t now = std::time(nullptr);
    if (static_cast<int64_t>(now) > kMinValidUnixTime) {
        std::tm timeinfo = {};
        if (localtime_r(&now, &timeinfo) == nullptr ||
            std::strftime(name, sizeof(name), "REC_%Y%m%d_%H%M%S.wav", &timeinfo) == 0) name[0] = '\0';
    }
    if (name[0] == '\0') {
        std::snprintf(name, sizeof(name), "REC_BOOT_%" PRId64 ".wav", esp_timer_get_time() / 1000);
    }

    return std::string(kRecordingsDir) + "/" + name;
}

std::string RecordingService::CandidateRecordingPath(const std::string& base, int suffix) const {
    if (suffix == 0) return base;
    const size_t dot = base.rfind('.');
    const std::string stem = dot == std::string::npos ? base : base.substr(0, dot);
    const std::string extension = dot == std::string::npos ? ".wav" : base.substr(dot);
    char numbered[16]{};
    std::snprintf(numbered, sizeof(numbered), "_%04d%s", suffix, extension.c_str());
    return stem + numbered;
}

FILE* RecordingService::CreateRecordingFile(const std::string& path, std::string& full_path,
                                            std::string& error, bool& collision) {
    collision = false;
    full_path = FullPath(path);
    const int descriptor = ::open(full_path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
    if (descriptor < 0) {
        if (errno == EEXIST) {
            collision = true;
            return nullptr;
        }
        error = std::string("Cannot create recording file: ") + std::strerror(errno);
        return nullptr;
    }
    FILE* fp = ::fdopen(descriptor, "wb+");
    if (fp != nullptr) return fp;
    error = std::string("Cannot open recording stream: ") + std::strerror(errno);
    ::close(descriptor);
    if (std::remove(full_path.c_str()) != 0) error += "; incomplete recording could not be removed";
    return nullptr;
}

std::string RecordingService::FullPath(const std::string& path) const {
    const char* mount = file_service_ != nullptr ? file_service_->GetMountPoint() : "/sdcard";
    const std::string mount_point = mount != nullptr ? mount : "/sdcard";
    if (path.empty() || path == "/") {
        return mount_point;
    }
    if (path == mount_point ||
        path.compare(0, mount_point.size() + 1, mount_point + "/") == 0) {
        return path;
    }
    if (path[0] == '/') {
        return mount_point + path;
    }
    return mount_point + "/" + path;
}

bool RecordingService::RequestAudioFocus() {
    if (audio_focus_ == nullptr) {
        return true;
    }
    AudioFocusRequest request;
    request.owner = "recorder";
    request.gain = AudioFocusGain::kExclusive;
    request.resume_on_release = false;
    request.release_playback_hardware = true;
    uint32_t token = 0;
    if (!audio_focus_->RequestFocus(request, token)) {
        return false;
    }
    audio_focus_token_ = token;
    return true;
}

void RecordingService::ReleaseAudioFocus() {
    if (audio_focus_ != nullptr && audio_focus_token_ != 0) {
        audio_focus_->ReleaseFocus(audio_focus_token_);
    }
    audio_focus_token_ = 0;
}

bool RecordingService::ShouldStop() const {
    if (mutex_ == nullptr) {
        return true;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool stop = stop_requested_;
    xSemaphoreGive(mutex_);
    return stop;
}

bool RecordingService::HasTask() const {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool active = task_active_;
    xSemaphoreGive(mutex_);
    return active;
}

bool RecordingService::JoinTask(uint32_t timeout_ms) {
    const uint32_t delay_ms = 20;
    for (uint32_t waited = 0; HasTask() && waited < timeout_ms; waited += delay_ms) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    return !HasTask();
}

void RecordingService::StoreTaskHandle(TaskHandle_t task) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (task_active_) {
            task_ = task;
        }
        xSemaphoreGive(mutex_);
    }
}

void RecordingService::ClearTask() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        task_active_ = false;
        task_ = nullptr;
        stop_requested_ = false;
        xSemaphoreGive(mutex_);
    }
}

void RecordingService::SetState(RecordingStatus status, const char* message) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.status = status;
        if (message != nullptr) {
            state_.message = message;
        }
        xSemaphoreGive(mutex_);
    }
}

void RecordingService::SetError(const std::string& error) {
    ESP_LOGW(TAG, "%s", error.c_str());
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.status = RecordingStatus::kError;
        state_.message = error;
        state_.last_error = error;
        xSemaphoreGive(mutex_);
    }
}

void RecordingService::SetLibraryError(const std::string& error) {
    if (mutex_ == nullptr) return;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    recordings_.clear();
    state_.library_error = error;
    xSemaphoreGive(mutex_);
}

void RecordingService::UpdateProgress(size_t bytes_written) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.bytes_written = bytes_written;
        state_.file_size = bytes_written + 44;
        state_.duration_ms = DurationForBytes(bytes_written);
        xSemaphoreGive(mutex_);
    }
}

uint32_t RecordingService::DurationForBytes(size_t bytes) const {
    const uint64_t bytes_per_second = static_cast<uint64_t>(active_config_.sample_rate) *
        active_config_.channels * (active_config_.bits_per_sample / 8U);
    if (bytes_per_second == 0) {
        return 0;
    }
    return static_cast<uint32_t>(std::min<uint64_t>((bytes * 1000ULL) / bytes_per_second, UINT32_MAX));
}

}  // namespace rodakos
