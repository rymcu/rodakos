#include "phone_os/audio_service.h"

#include "phone_os/audio_output_service.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <limits>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <mp3dec.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "AudioService";
constexpr const char* kOutputOwner = "audio-playback";
constexpr size_t kPlaybackBufferSize = 4096;
constexpr int kMp3ReadBufferSize = 16 * 1024;
constexpr int kMp3RefillThreshold = 2 * MAINBUF_SIZE;
constexpr uint32_t kTaskStackWords = 6144;

class OperationLock {
public:
    explicit OperationLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    ~OperationLock() { if (mutex_) xSemaphoreGive(mutex_); }
private:
    SemaphoreHandle_t mutex_;
};

bool GetFileSize(FILE* fp, size_t& size);

struct WavInfo {
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    uint16_t audio_format = 0;
    uint32_t data_size = 0;
    long data_offset = 0;
};

uint16_t ReadLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ReadLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool ReadWavHeader(FILE* fp, WavInfo& info) {
    size_t file_size = 0;
    if (!GetFileSize(fp, file_size)) return false;
    uint8_t header[12] = {};
    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        return false;
    }
    if (std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) {
        return false;
    }
    const uint64_t riff_end = static_cast<uint64_t>(ReadLe32(header + 4)) + 8;
    if (riff_end < sizeof(header) || riff_end > file_size ||
        riff_end > static_cast<uint64_t>(std::numeric_limits<long>::max())) return false;

    bool found_fmt = false;
    bool found_data = false;
    uint16_t block_align = 0;
    uint32_t byte_rate = 0;

    while (!found_data) {
        const long chunk_pos = ftell(fp);
        if (chunk_pos < 0 || static_cast<uint64_t>(chunk_pos) + 8 > riff_end) return false;
        uint8_t chunk_header[8] = {};
        if (fread(chunk_header, 1, sizeof(chunk_header), fp) != sizeof(chunk_header)) {
            return false;
        }

        const uint32_t chunk_size = ReadLe32(chunk_header + 4);
        const long chunk_data_pos = ftell(fp);
        if (chunk_data_pos < 0) {
            return false;
        }
        const uint64_t next_pos = static_cast<uint64_t>(chunk_data_pos) + chunk_size + (chunk_size & 1U);
        if (next_pos > riff_end) return false;

        if (std::memcmp(chunk_header, "fmt ", 4) == 0) {
            if (found_fmt) return false;
            uint8_t fmt[16] = {};
            if (chunk_size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), fp) != sizeof(fmt)) {
                return false;
            }
            info.audio_format = ReadLe16(fmt);
            info.channels = ReadLe16(fmt + 2);
            info.sample_rate = ReadLe32(fmt + 4);
            byte_rate = ReadLe32(fmt + 8);
            block_align = ReadLe16(fmt + 12);
            info.bits_per_sample = ReadLe16(fmt + 14);
            found_fmt = true;
        } else if (std::memcmp(chunk_header, "data", 4) == 0) {
            if (!found_fmt) {
                return false;
            }
            info.data_size = chunk_size;
            info.data_offset = chunk_data_pos;
            found_data = true;
            break;
        }

        if (fseek(fp, static_cast<long>(next_pos), SEEK_SET) != 0) {
            return false;
        }
    }

    if (!found_fmt || !found_data) {
        return false;
    }
    if (info.audio_format != 1 || info.sample_rate == 0 ||
        info.sample_rate > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        (info.channels != 1 && info.channels != 2) || info.bits_per_sample != 16 ||
        block_align != info.channels * sizeof(int16_t) ||
        static_cast<uint64_t>(info.sample_rate) * block_align != byte_rate ||
        info.data_size == 0 || info.data_size % block_align != 0) {
        ESP_LOGW(TAG, "Unsupported WAV format: format=%u rate=%" PRIu32 " ch=%u bits=%u",
                 info.audio_format, info.sample_rate, info.channels, info.bits_per_sample);
        return false;
    }

    return fseek(fp, info.data_offset, SEEK_SET) == 0;
}

std::string ExtractTitle(const std::string& path) {
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
        const auto a = static_cast<unsigned char>(text[offset + i]);
        const auto b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

bool GetFileSize(FILE* fp, size_t& size) {
    const long current = ftell(fp);
    if (current < 0) {
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        return false;
    }
    const long end = ftell(fp);
    if (end < 0 || fseek(fp, current, SEEK_SET) != 0) {
        return false;
    }
    size = static_cast<size_t>(end);
    return true;
}

bool ReadAudioBytesAt(FILE* fp, size_t offset, uint8_t* data, size_t count) {
    return offset <= static_cast<size_t>(std::numeric_limits<long>::max()) &&
        fseek(fp, static_cast<long>(offset), SEEK_SET) == 0 &&
        fread(data, 1, count, fp) == count && !ferror(fp);
}

bool SkipId3v2(FILE* fp, size_t start, size_t end, size_t& next) {
    uint8_t header[10]{};
    if (end - start < sizeof(header) || !ReadAudioBytesAt(fp, start, header, sizeof(header)) ||
        std::memcmp(header, "ID3", 3) != 0 || header[3] < 2 || header[3] > 4 || header[4] == 0xff ||
        ((header[6] | header[7] | header[8] | header[9]) & 0x80) != 0) return false;
    const uint32_t size = (static_cast<uint32_t>(header[6]) << 21) |
        (static_cast<uint32_t>(header[7]) << 14) | (static_cast<uint32_t>(header[8]) << 7) | header[9];
    const bool footer = header[3] == 4 && (header[5] & 0x10) != 0;
    const uint64_t length = static_cast<uint64_t>(size) + 10 + (footer ? 10 : 0);
    if (length > end - start) return false;
    next = start + static_cast<size_t>(length);
    if (footer) {
        uint8_t trailer[10]{};
        if (!ReadAudioBytesAt(fp, next - 10, trailer, sizeof(trailer)) ||
            std::memcmp(trailer, "3DI", 3) != 0 || std::memcmp(trailer + 3, header + 3, 7) != 0) return false;
    }
    return true;
}

bool FindMp3AudioEnd(FILE* fp, size_t file_size, size_t& audio_end) {
    audio_end = file_size;
    // Remove bounded standard trailing tags before deciding whether EOF cut a
    // frame short. Their contents are metadata, and may contain sync-like bytes.
    while (audio_end != 0) {
        uint8_t tag[32]{};
        if (audio_end >= 128) {
            if (!ReadAudioBytesAt(fp, audio_end - 128, tag, 3)) return false;
            if (std::memcmp(tag, "TAG", 3) == 0) { audio_end -= 128; continue; }
        }
        if (audio_end >= 32) {
            if (!ReadAudioBytesAt(fp, audio_end - 32, tag, 32)) return false;
            if (std::memcmp(tag, "APETAGEX", 8) == 0) {
                const uint32_t version = ReadLe32(tag + 8), size = ReadLe32(tag + 12);
                const uint32_t flags = ReadLe32(tag + 20);
                const bool has_header = (flags & 0x80000000U) != 0;
                const uint64_t total = static_cast<uint64_t>(size) + (has_header ? 32 : 0);
                if ((version != 1000 && version != 2000) || size < 32 || total > audio_end ||
                    (flags & 0x20000000U) != 0) return false;
                if (has_header && (!ReadAudioBytesAt(fp, audio_end - total, tag, 8) ||
                    std::memcmp(tag, "APETAGEX", 8) != 0)) return false;
                audio_end -= static_cast<size_t>(total);
                continue;
            }
        }
        if (audio_end >= 10) {
            if (!ReadAudioBytesAt(fp, audio_end - 10, tag, 10)) return false;
            if (std::memcmp(tag, "3DI", 3) == 0) {
                if (((tag[6] | tag[7] | tag[8] | tag[9]) & 0x80) != 0) return false;
                const uint32_t length = (static_cast<uint32_t>(tag[6]) << 21) |
                    (static_cast<uint32_t>(tag[7]) << 14) | (static_cast<uint32_t>(tag[8]) << 7) | tag[9];
                if (static_cast<uint64_t>(length) + 20 > audio_end) return false;
                const size_t start = audio_end - length - 20;
                size_t next = 0;
                if (!SkipId3v2(fp, start, audio_end, next) || next != audio_end) return false;
                audio_end = start;
                continue;
            }
        }
        break;
    }
    return fseek(fp, 0, SEEK_SET) == 0;
}

uint8_t* AllocateAudioBuffer(size_t size) {
    auto* buffer = static_cast<uint8_t*>(
        heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        buffer = static_cast<uint8_t*>(
            heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    return buffer;
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

AudioService::AudioService(AudioOutputService& output)
    : output_(output) {
    mutex_ = xSemaphoreCreateMutex();
    operation_mutex_ = xSemaphoreCreateMutex();
}

AudioService::~AudioService() {
    Deinit();
    if (operation_mutex_ != nullptr) vSemaphoreDelete(operation_mutex_);
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool AudioService::Init() {
    OperationLock operation(operation_mutex_);
    return InitLocked();
}

bool AudioService::IsReady() const {
    if (mutex_ == nullptr) return false;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool ready = initialized_;
    xSemaphoreGive(mutex_);
    return ready;
}

bool AudioService::InitLocked() {
    if (mutex_ == nullptr || operation_mutex_ == nullptr) return false;
    if (IsReady()) {
        return true;
    }

    if (!output_.Init()) {
        SetState(AudioPlaybackStatus::kError, "Audio hardware unavailable");
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    initialized_ = true;
    xSemaphoreGive(mutex_);
    SetState(AudioPlaybackStatus::kIdle, "Ready");
    ESP_LOGI(TAG, "Audio service initialized");
    return true;
}

void AudioService::Deinit() {
    OperationLock operation(operation_mutex_);
    Stop();
    // A timed release may fail while SD/codec I/O is in flight. Destruction must
    // retain service state until that owner exits; never kill a task holding I/O.
    while (HasPlaybackTask()) vTaskDelay(pdMS_TO_TICKS(10));
    output_.CloseForOwner(kOutputOwner);
    if (IsReady()) {
        output_.Deinit();
        xSemaphoreTake(mutex_, portMAX_DELAY);
        initialized_ = false;
        xSemaphoreGive(mutex_);
        SetState(AudioPlaybackStatus::kIdle, "Stopped");
        ESP_LOGI(TAG, "Audio service deinitialized");
    }
}

bool AudioService::PlayFile(const std::string& path, const std::string& title) {
    OperationLock operation(operation_mutex_);
    if (!IsSupportedAudioFile(path)) {
        SetState(AudioPlaybackStatus::kError, "Unsupported audio file");
        return false;
    }
    if (!InitLocked()) {
        return false;
    }

    Stop();
    if (!JoinPlaybackTask(1500)) {
        SetState(AudioPlaybackStatus::kError, "Previous playback busy");
        return false;
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        stop_requested_ = false;
        pause_requested_ = false;
        playback_io_idle_ = false;
        playback_hardware_suspended_ = false;
        playback_abort_error_ = false;
        state_ = {};
        state_.status = AudioPlaybackStatus::kLoading;
        state_.file_path = path;
        state_.title = title.empty() ? ExtractTitle(path) : title;
        state_.message = "Loading";
        xSemaphoreGive(mutex_);
    }

    MarkPlaybackTaskStarting();
#if CONFIG_SOC_CPU_CORES_NUM > 1
    TaskHandle_t task_handle = nullptr;
    const BaseType_t task_ret = xTaskCreatePinnedToCore(
        PlaybackTaskEntry, "audio_play", kTaskStackWords, this, 5, &task_handle, 0);
#else
    TaskHandle_t task_handle = nullptr;
    const BaseType_t task_ret = xTaskCreate(
        PlaybackTaskEntry, "audio_play", kTaskStackWords, this, 5, &task_handle);
#endif
    if (task_ret != pdPASS) {
        ClearPlaybackTask();
        SetState(AudioPlaybackStatus::kError, "No memory for playback task");
        ESP_LOGE(TAG, "Failed to create playback task");
        return false;
    }
    StorePlaybackTaskHandle(task_handle);

    return true;
}

void AudioService::Stop() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        stop_requested_ = true;
        pause_requested_ = false;
        playback_io_idle_ = false;
        playback_hardware_suspended_ = false;
        xSemaphoreGive(mutex_);
    }
}

bool AudioService::ReleasePlaybackHardware() {
    OperationLock operation(operation_mutex_);
    Stop();
    if (!JoinPlaybackTask(1500)) {
        ESP_LOGW(TAG, "Timed out waiting to release playback hardware");
        return false;
    }

    output_.CloseForOwner(kOutputOwner);
    if (output_.IsOpenForOwner(kOutputOwner)) {
        ESP_LOGW(TAG, "Playback hardware remained open after release");
        return false;
    }
    return true;
}

bool AudioService::SuspendPlaybackHardware() {
    OperationLock operation(operation_mutex_);
    Pause();

    constexpr uint32_t kSuspendTimeoutMs = 1500;
    constexpr uint32_t kSuspendPollMs = 10;
    for (uint32_t waited = 0; waited < kSuspendTimeoutMs; waited += kSuspendPollMs) {
        bool active = false;
        bool io_idle = false;
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            active = playback_task_active_;
            io_idle = playback_io_idle_;
            xSemaphoreGive(mutex_);
        }
        if (!active || io_idle) {
            const bool was_open = output_.IsOpenForOwner(kOutputOwner);
            if (was_open) {
                output_.CloseForOwner(kOutputOwner);
            }
            if (mutex_ != nullptr) {
                xSemaphoreTake(mutex_, portMAX_DELAY);
                playback_hardware_suspended_ = was_open;
                xSemaphoreGive(mutex_);
            }
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(kSuspendPollMs));
    }

    ESP_LOGW(TAG, "Timed out waiting for playback to reach a pause boundary");
    return false;
}

void AudioService::Pause() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (state_.status == AudioPlaybackStatus::kPlaying ||
            state_.status == AudioPlaybackStatus::kLoading) {
            pause_requested_ = true;
            playback_io_idle_ = false;
            state_.status = AudioPlaybackStatus::kPaused;
            state_.message = "Paused";
        }
        xSemaphoreGive(mutex_);
    }
}

void AudioService::Resume() {
    OperationLock operation(operation_mutex_);
    if (mutex_ == nullptr) {
        return;
    }

    bool should_resume = false;
    bool reopen_output = false;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    should_resume = playback_task_active_ && !stop_requested_ && state_.status == AudioPlaybackStatus::kPaused;
    reopen_output = should_resume && playback_hardware_suspended_;
    sample_rate = state_.sample_rate;
    channels = state_.channels;
    bits_per_sample = state_.bits_per_sample;
    xSemaphoreGive(mutex_);

    if (!should_resume) {
        return;
    }
    if (reopen_output &&
        (sample_rate == 0 || channels == 0 || bits_per_sample == 0 ||
         !output_.OpenForOwner(kOutputOwner, sample_rate, channels, bits_per_sample))) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (stop_requested_ || !playback_task_active_) {
            xSemaphoreGive(mutex_);
            output_.CloseForOwner(kOutputOwner);
            return;
        }
        playback_abort_error_ = true;
        stop_requested_ = true;
        pause_requested_ = false;
        state_.status = AudioPlaybackStatus::kError;
        state_.message = "Cannot resume audio hardware";
        xSemaphoreGive(mutex_);
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (stop_requested_ || !playback_task_active_) {
        xSemaphoreGive(mutex_);
        if (reopen_output) output_.CloseForOwner(kOutputOwner);
        return;
    }
    playback_hardware_suspended_ = false;
    playback_io_idle_ = false;
    pause_requested_ = false;
    state_.status = AudioPlaybackStatus::kPlaying;
    state_.message = "Playing";
    xSemaphoreGive(mutex_);
}

void AudioService::TogglePause() {
    const auto state = GetState();
    if (state.status == AudioPlaybackStatus::kPaused) {
        Resume();
    } else if (state.status == AudioPlaybackStatus::kPlaying) {
        Pause();
    }
}

bool AudioService::SetVolume(int volume) {
    return output_.SetVolume(volume);
}

int AudioService::volume() const {
    return output_.volume();
}

AudioPlaybackState AudioService::GetState() {
    AudioPlaybackState copy;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        copy = state_;
        xSemaphoreGive(mutex_);
    }
    // MQTT、MCP、播放和 UI 共用同一配置源，播放状态不能保留另一份音量。
    copy.volume = output_.volume();
    return copy;
}

bool AudioService::IsBusy() {
    const auto status = GetState().status;
    return status == AudioPlaybackStatus::kLoading ||
           status == AudioPlaybackStatus::kPlaying ||
           status == AudioPlaybackStatus::kPaused;
}

bool AudioService::IsSupportedAudioFile(const std::string& name) {
    return EndsWithCaseInsensitive(name, ".wav") || EndsWithCaseInsensitive(name, ".mp3");
}

void AudioService::PlaybackTaskEntry(void* arg) {
    static_cast<AudioService*>(arg)->PlaybackTask();
    vTaskDelete(nullptr);
}

void AudioService::PlaybackTask() {
    const std::string path = GetState().file_path;
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        ESP_LOGE(TAG, "Failed to open %s", path.c_str());
        SetState(AudioPlaybackStatus::kError, "Cannot open file");
        FinishPlayback(false, false);
        ClearPlaybackTask();
        return;
    }

    bool stopped = false;
    bool ok = false;
    if (EndsWithCaseInsensitive(path, ".mp3")) {
        ok = PlayMp3File(fp, path, stopped);
    } else {
        ok = PlayWavFile(fp, path, stopped);
    }
    fclose(fp);

    FinishPlayback(ok, stopped);

    ESP_LOGI(TAG, "Playback ended: %s", path.c_str());
    ClearPlaybackTask();
}

bool AudioService::PlayWavFile(FILE* fp, const std::string& path, bool& stopped) {
    WavInfo wav = {};
    if (!ReadWavHeader(fp, wav)) {
        SetState(AudioPlaybackStatus::kError, ferror(fp) ? "Cannot read WAV" : "Invalid or unsupported WAV");
        return false;
    }

    if (!output_.IsReady()) {
        SetState(AudioPlaybackStatus::kError, "Audio DAC unavailable");
        return false;
    }

    if (!output_.OpenForOwner(kOutputOwner, wav.sample_rate, wav.channels, wav.bits_per_sample)) {
        SetState(AudioPlaybackStatus::kError, "Codec open failed");
        return false;
    }

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.sample_rate = wav.sample_rate;
        state_.channels = wav.channels;
        state_.bits_per_sample = wav.bits_per_sample;
        state_.data_bytes = wav.data_size;
        state_.status = pause_requested_ ? AudioPlaybackStatus::kPaused : AudioPlaybackStatus::kPlaying;
        state_.message = pause_requested_ ? "Paused" : "Playing";
        xSemaphoreGive(mutex_);
    }

    uint8_t* buffer = AllocateAudioBuffer(kPlaybackBufferSize);
    if (buffer == nullptr) {
        output_.CloseForOwner(kOutputOwner);
        SetState(AudioPlaybackStatus::kError, "No audio buffer");
        return false;
    }

    ESP_LOGI(TAG, "Playing %s: %" PRIu32 " Hz, %u ch, %u bits, %" PRIu32 " bytes",
             path.c_str(), wav.sample_rate, wav.channels, wav.bits_per_sample, wav.data_size);

    size_t played = 0;
    uint16_t peak = 0;
    bool failed = false;

    while (played < wav.data_size) {
        bool should_pause = false;
        if (ShouldStopOrPause(should_pause)) {
            stopped = true;
            break;
        }
        if (should_pause) {
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }

        const size_t bytes_left = wav.data_size - played;
        const size_t to_read = std::min(bytes_left, kPlaybackBufferSize);
        const size_t bytes_read = fread(buffer, 1, to_read, fp);
        if (bytes_read != to_read || ferror(fp)) {
            SetState(AudioPlaybackStatus::kError, ferror(fp) ? "Cannot read WAV" : "Truncated WAV data");
            failed = true;
            break;
        }

        if ((bytes_read % sizeof(int16_t)) == 0) {
            peak = std::max(peak, PeakAbs16(buffer, bytes_read));
        }

        if (!output_.WriteForOwner(kOutputOwner, buffer, static_cast<int>(bytes_read))) {
            SetState(AudioPlaybackStatus::kError, "Audio output failed");
            failed = true;
            break;
        }
        played += bytes_read;
        UpdateProgress(played, wav.data_size);
    }

    heap_caps_free(buffer);
    if (failed || stopped) {
        output_.CloseForOwner(kOutputOwner);
    }

    if (!failed && !stopped) {
        UpdateProgress(wav.data_size, wav.data_size);
    }
    ESP_LOGI(TAG, "WAV playback peak: %u/%u", static_cast<unsigned>(peak),
             static_cast<unsigned>(INT16_MAX));

    return !failed;
}

void AudioService::FinishPlayback(bool ok, bool stopped) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    // Publish the terminal result only after its output ownership is released.
    // Stop can be admitted by the final write, after the loop's last poll.
    if (!ok || stopped || stop_requested_ || playback_abort_error_) output_.CloseForOwner(kOutputOwner);
    if (playback_abort_error_) {
        state_.status = AudioPlaybackStatus::kError;
    } else if (stopped || stop_requested_) {
        state_.status = AudioPlaybackStatus::kStopped;
        state_.message = "Stopped";
    } else if (ok) {
        state_.status = AudioPlaybackStatus::kCompleted;
        state_.message = "Completed";
    } else if (state_.status != AudioPlaybackStatus::kError || state_.message.empty()) {
        state_.status = AudioPlaybackStatus::kError;
        state_.message = "Playback failed";
    }
    xSemaphoreGive(mutex_);
}

bool AudioService::PlayMp3File(FILE* fp, const std::string& path, bool& stopped) {
    size_t file_size = 0;
    size_t audio_end = 0;
    if (!GetFileSize(fp, file_size) || !FindMp3AudioEnd(fp, file_size, audio_end)) {
        SetState(AudioPlaybackStatus::kError, "Cannot read MP3");
        return false;
    }

    if (!output_.IsReady()) {
        SetState(AudioPlaybackStatus::kError, "Audio DAC unavailable");
        return false;
    }

    HMP3Decoder decoder = MP3InitDecoder();
    if (decoder == nullptr) {
        SetState(AudioPlaybackStatus::kError, "MP3 decoder unavailable");
        return false;
    }

    auto* read_buffer = static_cast<unsigned char*>(
        heap_caps_malloc(kMp3ReadBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (read_buffer == nullptr) {
        read_buffer = static_cast<unsigned char*>(
            heap_caps_malloc(kMp3ReadBufferSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    auto* pcm_buffer = static_cast<short*>(
        heap_caps_malloc(MAX_NCHAN * MAX_NGRAN * MAX_NSAMP * sizeof(short),
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (read_buffer == nullptr || pcm_buffer == nullptr) {
        if (read_buffer != nullptr) {
            heap_caps_free(read_buffer);
        }
        if (pcm_buffer != nullptr) {
            heap_caps_free(pcm_buffer);
        }
        MP3FreeDecoder(decoder);
        SetState(AudioPlaybackStatus::kError, "No MP3 buffer");
        return false;
    }

    int bytes_left = 0;
    bool eof_reached = false;
    bool failed = false;
    bool codec_ready = false;
    bool force_refill = false;
    size_t read_offset = 0;
    MP3FrameInfo output_format = {};
    unsigned char* read_ptr = read_buffer;
    MP3FrameInfo frame_info = {};

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.data_bytes = file_size;
        state_.status = pause_requested_ ? AudioPlaybackStatus::kPaused : AudioPlaybackStatus::kLoading;
        state_.message = pause_requested_ ? "Paused" : "Loading MP3";
        xSemaphoreGive(mutex_);
    }

    ESP_LOGI(TAG, "Playing MP3 %s: %zu bytes", path.c_str(), file_size);

    while (true) {
        bool should_pause = false;
        if (ShouldStopOrPause(should_pause)) {
            stopped = true;
            break;
        }
        if (should_pause) {
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }

        if ((bytes_left < kMp3RefillThreshold || force_refill) && !eof_reached) {
            std::memmove(read_buffer, read_ptr, static_cast<size_t>(bytes_left));
            const size_t request = std::min(audio_end - read_offset,
                static_cast<size_t>(kMp3ReadBufferSize - bytes_left));
            const size_t bytes_read = fread(read_buffer + bytes_left, 1,
                                            request, fp);
            if (bytes_read != request || ferror(fp)) {
                SetState(AudioPlaybackStatus::kError, ferror(fp) ? "Cannot read MP3" : "Truncated MP3 file");
                failed = true;
                break;
            }
            read_offset += bytes_read;
            if (bytes_read < static_cast<size_t>(kMp3ReadBufferSize - bytes_left)) {
                std::memset(read_buffer + bytes_left + bytes_read, 0,
                            kMp3ReadBufferSize - bytes_left - bytes_read);
            }
            bytes_left += static_cast<int>(bytes_read);
            read_ptr = read_buffer;
            eof_reached = read_offset == audio_end;
            force_refill = false;
        }

        if (bytes_left <= 0) {
            break;
        }

        if (bytes_left >= 3 && std::memcmp(read_ptr, "ID3", 3) == 0) {
            size_t next = 0;
            if (!SkipId3v2(fp, read_offset - static_cast<size_t>(bytes_left), audio_end, next) ||
                fseek(fp, static_cast<long>(next), SEEK_SET) != 0) {
                SetState(AudioPlaybackStatus::kError, "Invalid MP3 metadata");
                failed = true;
                break;
            }
            read_offset = next;
            read_ptr = read_buffer;
            bytes_left = 0;
            eof_reached = read_offset == audio_end;
            continue;
        }

        const int offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (offset < 0) {
            if (eof_reached) {
                if (!std::all_of(read_ptr, read_ptr + bytes_left, [](unsigned char value) { return value == 0; })) {
                    SetState(AudioPlaybackStatus::kError, "Invalid MP3 tail");
                    failed = true;
                }
                break;
            }
            if (bytes_left == 1 && read_ptr[0] == 0xff) {
                force_refill = true;
                continue;
            }
            if (codec_ready) {
                SetState(AudioPlaybackStatus::kError, "Invalid MP3 frame");
                failed = true;
                break;
            }
            // Preserve a possible sync prefix across a read-buffer boundary.
            read_ptr += bytes_left - 1;
            bytes_left = 1;
            continue;
        }
        if (codec_ready && offset != 0) {
            SetState(AudioPlaybackStatus::kError, "Invalid MP3 frame boundary");
            failed = true;
            break;
        }
        read_ptr += offset;
        bytes_left -= offset;

        auto* frame_start = read_ptr;
        const int frame_bytes = bytes_left;
        if (bytes_left < 4) {
            if (eof_reached) {
                SetState(AudioPlaybackStatus::kError, "Truncated MP3 frame");
                failed = true;
                break;
            }
            force_refill = true;
            continue;
        }
        const unsigned version = (read_ptr[1] >> 3) & 3;
        if (version == 1 || ((read_ptr[1] >> 1) & 3) != 1) {
            SetState(AudioPlaybackStatus::kError, "Unsupported MP3 frame header");
            failed = true;
            break;
        }
        const bool mono = (read_ptr[3] >> 6) == 3;
        const int side_info_bytes = version == 3 ? (mono ? 17 : 32) : (mono ? 9 : 17);
        const int header_bytes = (read_ptr[1] & 1) != 0 ? 4 : 6;
        // Helix reads CRC and side-info before testing bytesLeft for underflow.
        // Only call it after those bytes actually exist in this buffer.
        if (bytes_left < header_bytes + side_info_bytes) {
            if (eof_reached) {
                SetState(AudioPlaybackStatus::kError, "Truncated MP3 frame");
                failed = true;
                break;
            }
            force_refill = true;
            continue;
        }
        const int decode_ret = MP3Decode(decoder, &read_ptr, &bytes_left, pcm_buffer, 0);
        if (decode_ret != ERR_MP3_NONE) {
            if (decode_ret == ERR_MP3_INDATA_UNDERFLOW) {
                if (eof_reached || frame_bytes == kMp3ReadBufferSize) {
                    SetState(AudioPlaybackStatus::kError, "Truncated MP3 frame");
                    failed = true;
                    break;
                }
                // Helix has already consumed the header and side-info on this
                // error. Refill the original complete frame, not its payload.
                read_ptr = frame_start;
                bytes_left = frame_bytes;
                force_refill = true;
                continue;
            }
            if (decode_ret == ERR_MP3_MAINDATA_UNDERFLOW && bytes_left >= 0 && bytes_left < frame_bytes) {
                continue;
            }
            ESP_LOGW(TAG, "MP3 decode failed: %d", decode_ret);
            SetState(AudioPlaybackStatus::kError, "MP3 decode failed");
            failed = true;
            break;
        }

        MP3GetLastFrameInfo(decoder, &frame_info);
        if (bytes_left < 0 || bytes_left >= frame_bytes || frame_info.outputSamps <= 0 ||
            frame_info.outputSamps > MAX_NCHAN * MAX_NGRAN * MAX_NSAMP || frame_info.samprate <= 0 ||
            (frame_info.nChans != 1 && frame_info.nChans != 2) || frame_info.bitsPerSample != 16 ||
            frame_info.outputSamps % frame_info.nChans != 0 ||
            (codec_ready && (frame_info.samprate != output_format.samprate || frame_info.nChans != output_format.nChans))) {
            SetState(AudioPlaybackStatus::kError, "Invalid MP3 output format");
            failed = true;
            break;
        }

        if (!codec_ready) {
            if (!output_.OpenForOwner(kOutputOwner,
                                      static_cast<uint32_t>(frame_info.samprate),
                                      static_cast<uint16_t>(frame_info.nChans),
                                      static_cast<uint16_t>(frame_info.bitsPerSample))) {
                SetState(AudioPlaybackStatus::kError, "Codec open failed");
                failed = true;
                break;
            }
            codec_ready = true;
            output_format = frame_info;

            if (mutex_ != nullptr) {
                xSemaphoreTake(mutex_, portMAX_DELAY);
                state_.sample_rate = static_cast<uint32_t>(frame_info.samprate);
                state_.channels = static_cast<uint16_t>(frame_info.nChans);
                state_.bits_per_sample = static_cast<uint16_t>(frame_info.bitsPerSample);
                state_.data_bytes = file_size;
                state_.status = pause_requested_ ? AudioPlaybackStatus::kPaused : AudioPlaybackStatus::kPlaying;
                state_.message = pause_requested_ ? "Paused" : "Playing";
                xSemaphoreGive(mutex_);
            }
            ESP_LOGI(TAG, "MP3 stream: %d Hz, %d ch, %d bits",
                     frame_info.samprate, frame_info.nChans, frame_info.bitsPerSample);
        }

        const int output_bytes = frame_info.outputSamps * (frame_info.bitsPerSample / 8);
        if (!output_.WriteForOwner(kOutputOwner, pcm_buffer, output_bytes)) {
            SetState(AudioPlaybackStatus::kError, "Audio output failed");
            failed = true;
            break;
        }

        const long offset_now = ftell(fp);
        if (offset_now >= 0) {
            const size_t file_offset = static_cast<size_t>(offset_now);
            const size_t buffered = bytes_left > 0 ? static_cast<size_t>(bytes_left) : 0;
            const size_t consumed = file_offset > buffered ? file_offset - buffered : 0;
            UpdateProgress(std::min(consumed, file_size), file_size);
        }
    }

    if (codec_ready && (failed || stopped)) {
        output_.CloseForOwner(kOutputOwner);
    }
    heap_caps_free(pcm_buffer);
    heap_caps_free(read_buffer);
    MP3FreeDecoder(decoder);

    if (!failed && !stopped && !codec_ready) {
        SetState(AudioPlaybackStatus::kError, "No MP3 frames");
        failed = true;
    }

    if (!failed && !stopped) {
        UpdateProgress(file_size, file_size);
    }
    return !failed;
}

bool AudioService::ShouldStopOrPause(bool& should_pause) {
    should_pause = false;
    bool should_stop = false;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        should_stop = stop_requested_;
        should_pause = pause_requested_;
        playback_io_idle_ = should_pause;
        xSemaphoreGive(mutex_);
    }
    return should_stop;
}

void AudioService::SetState(AudioPlaybackStatus status, const char* message) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.status = status;
        if (message != nullptr) {
            state_.message = message;
        }
        xSemaphoreGive(mutex_);
    }
}

void AudioService::UpdateProgress(size_t bytes_played, size_t data_bytes) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.bytes_played = bytes_played;
        state_.data_bytes = data_bytes;
        state_.progress_percent = data_bytes == 0 ? 0 :
            static_cast<int>((bytes_played * 100ULL) / data_bytes);
        xSemaphoreGive(mutex_);
    }
}

void AudioService::MarkPlaybackTaskStarting() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        playback_task_active_ = true;
        playback_task_ = nullptr;
        xSemaphoreGive(mutex_);
    }
}

void AudioService::StorePlaybackTaskHandle(TaskHandle_t task) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (playback_task_active_) {
            playback_task_ = task;
        }
        xSemaphoreGive(mutex_);
    }
}

void AudioService::ClearPlaybackTask() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        playback_task_active_ = false;
        playback_task_ = nullptr;
        xSemaphoreGive(mutex_);
    }
}

bool AudioService::HasPlaybackTask() {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool has_task = playback_task_active_;
    xSemaphoreGive(mutex_);
    return has_task;
}

bool AudioService::JoinPlaybackTask(uint32_t timeout_ms) {
    const int delay_ms = 20;
    uint32_t waited = 0;
    while (HasPlaybackTask() && waited < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        waited += delay_ms;
    }
    return !HasPlaybackTask();
}

}  // namespace rodakos
