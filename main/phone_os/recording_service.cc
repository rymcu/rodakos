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

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "RecordingService";
constexpr const char* kRecordingsDir = "/recordings";
constexpr int64_t kMinValidUnixTime = 1700000000;
constexpr int kMaxNameSuffix = 9999;
constexpr size_t kRecordBufferSize = 4096;
constexpr uint32_t kTaskStackWords = 6144;

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
    if (fp == nullptr) {
        return false;
    }

    const uint32_t clamped_data = data_bytes > UINT32_MAX
                                      ? UINT32_MAX
                                      : static_cast<uint32_t>(data_bytes);
    const uint16_t block_align =
        static_cast<uint16_t>(config.channels * (config.bits_per_sample / 8));
    const uint32_t byte_rate = config.sample_rate * block_align;
    uint8_t header[44] = {};
    std::memcpy(header, "RIFF", 4);
    WriteLe32(header + 4, 36U + clamped_data);
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
    WriteLe32(header + 40, clamped_data);

    return std::fwrite(header, 1, sizeof(header), fp) == sizeof(header);
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

uint32_t ReadWavDurationMs(const std::string& full_path, size_t fallback_size) {
    FILE* fp = std::fopen(full_path.c_str(), "rb");
    if (fp == nullptr) {
        return 0;
    }

    uint8_t header[44] = {};
    const bool ok = std::fread(header, 1, sizeof(header), fp) == sizeof(header) &&
                    std::memcmp(header, "RIFF", 4) == 0 &&
                    std::memcmp(header + 8, "WAVE", 4) == 0 &&
                    std::memcmp(header + 12, "fmt ", 4) == 0 &&
                    std::memcmp(header + 36, "data", 4) == 0;
    std::fclose(fp);
    if (!ok) {
        return 0;
    }

    const uint16_t channels = ReadLe16(header + 22);
    const uint32_t sample_rate = ReadLe32(header + 24);
    const uint16_t bits_per_sample = ReadLe16(header + 34);
    const uint32_t data_bytes = ReadLe32(header + 40);
    const uint32_t bytes_per_second =
        sample_rate * channels * (bits_per_sample / 8U);
    if (bytes_per_second == 0) {
        return 0;
    }
    const size_t usable_data = data_bytes > 0 ? data_bytes : fallback_size > 44 ? fallback_size - 44 : 0;
    return static_cast<uint32_t>((usable_data * 1000ULL) / bytes_per_second);
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
}

RecordingService::~RecordingService() {
    Stop();
    input_.Deinit();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool RecordingService::Start(const RecordingConfig& config) {
    if (HasTask()) {
        SetError("Recording is already running");
        return false;
    }
    if (!PrepareStorage()) {
        return false;
    }
    if (config.sample_rate == 0 || config.channels == 0 || config.input_channels == 0 ||
        config.bits_per_sample != 16) {
        SetError("Unsupported recording format");
        return false;
    }

    const std::string path = BuildRecordingPath();
    if (path.empty()) {
        SetError("Failed to choose a recording path");
        return false;
    }

    if (!RequestAudioFocus()) {
        SetError("Audio focus unavailable");
        return false;
    }

    active_config_ = config;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        stop_requested_ = false;
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

    MarkTaskStarting();
    TaskHandle_t task_handle = nullptr;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    const BaseType_t task_ret = xTaskCreatePinnedToCore(
        RecordingTaskEntry, "recorder", kTaskStackWords, this, 5, &task_handle, 0);
#else
    const BaseType_t task_ret = xTaskCreate(
        RecordingTaskEntry, "recorder", kTaskStackWords, this, 5, &task_handle);
#endif
    if (task_ret != pdPASS) {
        ClearTask();
        ReleaseAudioFocus();
        SetError("No memory for recording task");
        ESP_LOGE(TAG, "Failed to create recording task");
        return false;
    }
    StoreTaskHandle(task_handle);
    return true;
}

void RecordingService::Stop() {
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
    JoinTask(2500);
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
    if (!PrepareStorage()) {
        return false;
    }

    std::vector<FileEntry> entries;
    if (!file_service_->ListDirectory(kRecordingsDir, entries)) {
        SetError("Failed to list recordings");
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
        recording.duration_ms = ReadWavDurationMs(recording.full_path, entry.size);
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
        xSemaphoreGive(mutex_);
    }
    return true;
}

bool RecordingService::DeleteRecording(const std::string& path) {
    if (HasTask()) {
        SetError("Stop recording before deleting files");
        return false;
    }
    if (!PrepareStorage()) {
        return false;
    }
    if (!file_service_->DeleteFile(path)) {
        SetError("Failed to delete recording");
        return false;
    }
    RefreshRecordings();
    return true;
}

bool RecordingService::IsRecordingFile(const std::string& name) {
    return EndsWithCaseInsensitive(name, ".wav");
}

void RecordingService::RecordingTaskEntry(void* arg) {
    static_cast<RecordingService*>(arg)->RecordingTask();
}

void RecordingService::RecordingTask() {
    const RecordingConfig config = active_config_;
    const RecordingState start_state = GetState();
    const std::string path = start_state.path;
    const std::string full_path = start_state.full_path;
    FILE* fp = nullptr;
    uint8_t* buffer = nullptr;
    size_t bytes_written = 0;
    uint16_t peak = 0;
    bool failed = false;
    std::string error;

    fp = std::fopen(full_path.c_str(), "wb+");
    if (fp == nullptr) {
        failed = true;
        error = std::string("Cannot open recording file: ") + std::strerror(errno);
        goto cleanup;
    }
    if (!WriteWavHeader(fp, config, 0)) {
        failed = true;
        error = "Cannot write WAV header";
        goto cleanup;
    }

    if (!input_.Open(config.sample_rate,
                     config.input_channels,
                     config.bits_per_sample,
                     config.gain,
                     config.input_channel_mask)) {
        failed = true;
        error = "Audio ADC unavailable";
        goto cleanup;
    }

    buffer = static_cast<uint8_t*>(
        heap_caps_malloc(kRecordBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        buffer = static_cast<uint8_t*>(
            heap_caps_malloc(kRecordBufferSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (buffer == nullptr) {
        failed = true;
        error = "No recording buffer";
        goto cleanup;
    }

    SetState(RecordingStatus::kRecording, "Recording");
    ESP_LOGI(TAG, "Recording to %s: %" PRIu32 " Hz, %u ch, %u bits",
             full_path.c_str(), config.sample_rate,
             static_cast<unsigned>(config.channels),
             static_cast<unsigned>(config.bits_per_sample));

    while (!ShouldStop()) {
        if (!input_.Read(buffer, static_cast<int>(kRecordBufferSize))) {
            failed = true;
            error = "Audio read failed";
            break;
        }
        peak = std::max(peak, PeakAbs16(buffer, kRecordBufferSize));
        const size_t written = std::fwrite(buffer, 1, kRecordBufferSize, fp);
        if (written != kRecordBufferSize) {
            failed = true;
            error = "SD write failed";
            break;
        }
        bytes_written += written;
        UpdateProgress(bytes_written);
    }

cleanup:
    if (buffer != nullptr) {
        heap_caps_free(buffer);
    }
    input_.Close();
    input_.Deinit();

    if (fp != nullptr) {
        if (std::fseek(fp, 0, SEEK_SET) == 0) {
            WriteWavHeader(fp, config, bytes_written);
        }
        std::fclose(fp);
    }

    ReleaseAudioFocus();

    if (failed) {
        if (bytes_written == 0 && !full_path.empty()) {
            std::remove(full_path.c_str());
        }
        SetError(error.empty() ? "Recording failed" : error);
        ESP_LOGW(TAG, "Recording failed: %s", error.c_str());
    } else {
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            state_.status = RecordingStatus::kCompleted;
            state_.message = "Saved";
            state_.bytes_written = bytes_written;
            state_.file_size = bytes_written + 44;
            state_.duration_ms = DurationForBytes(bytes_written);
            state_.last_error.clear();
            xSemaphoreGive(mutex_);
        }
        ESP_LOGI(TAG, "Saved recording: %s (%u bytes), peak=%u/%u",
                 path.c_str(), static_cast<unsigned>(bytes_written + 44),
                 static_cast<unsigned>(peak), static_cast<unsigned>(INT16_MAX));
        RefreshRecordings();
    }

    ClearTask();
    vTaskDelete(nullptr);
}

bool RecordingService::PrepareStorage() {
    if (file_service_ == nullptr) {
        SetError("File service unavailable");
        return false;
    }
    if (!file_service_->IsMounted() && !file_service_->Init()) {
        SetError("SD card unavailable");
        return false;
    }
    if (!file_service_->Exists(kRecordingsDir) && !file_service_->CreateDirectory(kRecordingsDir)) {
        SetError("Cannot create /recordings");
        return false;
    }
    return true;
}

std::string RecordingService::BuildRecordingPath() {
    char name[48] = {};
    const std::time_t now = std::time(nullptr);
    if (static_cast<int64_t>(now) > kMinValidUnixTime) {
        std::tm timeinfo = {};
        localtime_r(&now, &timeinfo);
        std::strftime(name, sizeof(name), "REC_%Y%m%d_%H%M%S.wav", &timeinfo);
    } else {
        std::snprintf(name, sizeof(name), "REC_BOOT_%" PRId64 ".wav", esp_timer_get_time() / 1000);
    }

    std::string path = std::string(kRecordingsDir) + "/" + name;
    const char* dot = std::strrchr(name, '.');
    const std::string stem = dot != nullptr ? std::string(name, static_cast<size_t>(dot - name)) : name;
    const std::string extension = dot != nullptr ? dot : ".wav";
    for (int suffix = 1; suffix <= kMaxNameSuffix && file_service_ != nullptr &&
                         file_service_->Exists(path); ++suffix) {
        char numbered[80] = {};
        std::snprintf(numbered, sizeof(numbered), "%s_%04d%s",
                      stem.c_str(), suffix, extension.c_str());
        path = std::string(kRecordingsDir) + "/" + numbered;
    }
    if (file_service_ != nullptr && file_service_->Exists(path)) {
        return {};
    }
    return path;
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

void RecordingService::MarkTaskStarting() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        task_active_ = true;
        task_ = nullptr;
        xSemaphoreGive(mutex_);
    }
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
    const uint32_t bytes_per_second =
        active_config_.sample_rate * active_config_.channels * (active_config_.bits_per_sample / 8U);
    if (bytes_per_second == 0) {
        return 0;
    }
    return static_cast<uint32_t>((bytes * 1000ULL) / bytes_per_second);
}

}  // namespace rodakos
