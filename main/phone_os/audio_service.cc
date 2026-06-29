#include "phone_os/audio_service.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include <dev_audio_codec.h>
#include <esp_board_manager.h>
#include <esp_codec_dev.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "AudioService";
constexpr const char* kAudioDacDeviceName = "audio_dac";
constexpr size_t kPlaybackBufferSize = 4096;
constexpr uint32_t kTaskStackWords = 6144;

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
    uint8_t header[12] = {};
    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        return false;
    }
    if (std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool found_fmt = false;
    bool found_data = false;

    while (!found_data) {
        uint8_t chunk_header[8] = {};
        if (fread(chunk_header, 1, sizeof(chunk_header), fp) != sizeof(chunk_header)) {
            return false;
        }

        const uint32_t chunk_size = ReadLe32(chunk_header + 4);
        const long chunk_data_pos = ftell(fp);
        if (chunk_data_pos < 0) {
            return false;
        }

        if (std::memcmp(chunk_header, "fmt ", 4) == 0) {
            uint8_t fmt[16] = {};
            if (chunk_size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), fp) != sizeof(fmt)) {
                return false;
            }
            info.audio_format = ReadLe16(fmt);
            info.channels = ReadLe16(fmt + 2);
            info.sample_rate = ReadLe32(fmt + 4);
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

        long next_pos = chunk_data_pos + static_cast<long>(chunk_size);
        if ((chunk_size & 1U) != 0) {
            next_pos++;
        }
        if (fseek(fp, next_pos, SEEK_SET) != 0) {
            return false;
        }
    }

    if (!found_fmt || !found_data) {
        return false;
    }
    if (info.audio_format != 1 || info.sample_rate == 0 ||
        (info.channels != 1 && info.channels != 2) || info.bits_per_sample != 16) {
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

}  // namespace

AudioService::AudioService() {
    mutex_ = xSemaphoreCreateMutex();
    state_.volume = volume_;
}

AudioService::~AudioService() {
    Deinit();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool AudioService::Init() {
    if (initialized_) {
        return true;
    }

    esp_err_t ret = esp_board_manager_init_device_by_name(kAudioDacDeviceName);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio DAC (%s)", esp_err_to_name(ret));
        SetState(AudioPlaybackStatus::kError, "Audio hardware unavailable");
        return false;
    }

    ret = esp_board_manager_get_device_handle(kAudioDacDeviceName, &dac_handle_);
    if (ret != ESP_OK || dac_handle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to get audio DAC handle (%s)", esp_err_to_name(ret));
        esp_board_manager_deinit_device_by_name(kAudioDacDeviceName);
        dac_handle_ = nullptr;
        SetState(AudioPlaybackStatus::kError, "Audio handle unavailable");
        return false;
    }

    initialized_ = true;
    SetState(AudioPlaybackStatus::kIdle, "Ready");
    ESP_LOGI(TAG, "Audio service initialized");
    return true;
}

void AudioService::Deinit() {
    Stop();
    const bool playback_stopped = JoinPlaybackTask(1500);

    if (playback_stopped && IsCodecOpen() && dac_handle_ != nullptr) {
        auto* handle = static_cast<dev_audio_codec_handles_t*>(dac_handle_);
        if (handle->codec_dev != nullptr) {
            esp_codec_dev_close(handle->codec_dev);
        }
        SetCodecOpen(false);
    }

    if (initialized_) {
        esp_board_manager_deinit_device_by_name(kAudioDacDeviceName);
        initialized_ = false;
        dac_handle_ = nullptr;
        SetState(AudioPlaybackStatus::kIdle, "Stopped");
        ESP_LOGI(TAG, "Audio service deinitialized");
    }
}

bool AudioService::PlayFile(const std::string& path, const std::string& title) {
    if (!IsSupportedAudioFile(path)) {
        SetState(AudioPlaybackStatus::kError, "Only WAV is supported");
        return false;
    }
    if (!Init()) {
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
        state_ = {};
        state_.status = AudioPlaybackStatus::kLoading;
        state_.file_path = path;
        state_.title = title.empty() ? ExtractTitle(path) : title;
        state_.message = "Loading";
        state_.volume = volume_;
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
        xSemaphoreGive(mutex_);
    }
}

void AudioService::Pause() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (state_.status == AudioPlaybackStatus::kPlaying) {
            pause_requested_ = true;
            state_.status = AudioPlaybackStatus::kPaused;
            state_.message = "Paused";
        }
        xSemaphoreGive(mutex_);
    }
}

void AudioService::Resume() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (state_.status == AudioPlaybackStatus::kPaused) {
            pause_requested_ = false;
            state_.status = AudioPlaybackStatus::kPlaying;
            state_.message = "Playing";
        }
        xSemaphoreGive(mutex_);
    }
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
    const int clamped = std::clamp(volume, 0, 100);
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        volume_ = clamped;
        state_.volume = volume_;
        xSemaphoreGive(mutex_);
    } else {
        volume_ = clamped;
    }

    if (initialized_ && dac_handle_ != nullptr) {
        auto* handle = static_cast<dev_audio_codec_handles_t*>(dac_handle_);
        if (handle->codec_dev != nullptr) {
            const int ret = esp_codec_dev_set_out_vol(handle->codec_dev, volume_);
            if (ret != ESP_CODEC_DEV_OK) {
                ESP_LOGW(TAG, "Failed to set volume to %d", volume_);
                return false;
            }
        }
    }
    return true;
}

int AudioService::volume() const {
    if (mutex_ == nullptr) {
        return volume_;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const int volume = volume_;
    xSemaphoreGive(mutex_);
    return volume;
}

AudioPlaybackState AudioService::GetState() {
    AudioPlaybackState copy;
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        copy = state_;
        xSemaphoreGive(mutex_);
    }
    return copy;
}

bool AudioService::IsBusy() {
    const auto status = GetState().status;
    return status == AudioPlaybackStatus::kLoading ||
           status == AudioPlaybackStatus::kPlaying ||
           status == AudioPlaybackStatus::kPaused;
}

bool AudioService::IsSupportedAudioFile(const std::string& name) {
    return EndsWithCaseInsensitive(name, ".wav");
}

void AudioService::PlaybackTaskEntry(void* arg) {
    static_cast<AudioService*>(arg)->PlaybackTask();
}

void AudioService::PlaybackTask() {
    const std::string path = GetState().file_path;
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        ESP_LOGE(TAG, "Failed to open %s", path.c_str());
        SetState(AudioPlaybackStatus::kError, "Cannot open file");
        ClearPlaybackTask();
        vTaskDelete(nullptr);
        return;
    }

    WavInfo wav = {};
    if (!ReadWavHeader(fp, wav)) {
        fclose(fp);
        SetState(AudioPlaybackStatus::kError, "Unsupported WAV");
        ClearPlaybackTask();
        vTaskDelete(nullptr);
        return;
    }

    auto* handle = static_cast<dev_audio_codec_handles_t*>(dac_handle_);
    if (handle == nullptr || handle->codec_dev == nullptr) {
        fclose(fp);
        SetState(AudioPlaybackStatus::kError, "Audio DAC unavailable");
        ClearPlaybackTask();
        vTaskDelete(nullptr);
        return;
    }

    esp_codec_dev_sample_info_t sample_info = {};
    sample_info.bits_per_sample = static_cast<uint8_t>(wav.bits_per_sample);
    sample_info.channel = static_cast<uint8_t>(wav.channels);
    sample_info.channel_mask = 0;
    sample_info.sample_rate = wav.sample_rate;
    sample_info.mclk_multiple = (wav.sample_rate % 11025U) == 0 ? 384 : 256;
    int ret = esp_codec_dev_open(handle->codec_dev, &sample_info);
    if (ret != ESP_CODEC_DEV_OK) {
        fclose(fp);
        ESP_LOGE(TAG, "Failed to open codec: %d", ret);
        SetState(AudioPlaybackStatus::kError, "Codec open failed");
        ClearPlaybackTask();
        vTaskDelete(nullptr);
        return;
    }
    SetCodecOpen(true);
    SetVolume(volume_);

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.sample_rate = wav.sample_rate;
        state_.channels = wav.channels;
        state_.bits_per_sample = wav.bits_per_sample;
        state_.data_bytes = wav.data_size;
        state_.status = AudioPlaybackStatus::kPlaying;
        state_.message = "Playing";
        xSemaphoreGive(mutex_);
    }

    uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(kPlaybackBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        buffer = static_cast<uint8_t*>(heap_caps_malloc(kPlaybackBufferSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (buffer == nullptr) {
        fclose(fp);
        esp_codec_dev_close(handle->codec_dev);
        SetCodecOpen(false);
        SetState(AudioPlaybackStatus::kError, "No audio buffer");
        ClearPlaybackTask();
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "Playing %s: %" PRIu32 " Hz, %u ch, %u bits, %" PRIu32 " bytes",
             path.c_str(), wav.sample_rate, wav.channels, wav.bits_per_sample, wav.data_size);

    size_t played = 0;
    bool stopped = false;
    bool failed = false;

    while (played < wav.data_size) {
        bool should_stop = false;
        bool should_pause = false;
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            should_stop = stop_requested_;
            should_pause = pause_requested_;
            xSemaphoreGive(mutex_);
        }
        if (should_stop) {
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
        if (bytes_read == 0) {
            if (feof(fp)) {
                break;
            }
            failed = true;
            break;
        }

        ret = esp_codec_dev_write(handle->codec_dev, buffer, static_cast<int>(bytes_read));
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Codec write failed: %d", ret);
            failed = true;
            break;
        }
        played += bytes_read;
        UpdateProgress(played, wav.data_size);
    }

    heap_caps_free(buffer);
    fclose(fp);
    esp_codec_dev_close(handle->codec_dev);
    SetCodecOpen(false);

    if (failed) {
        SetState(AudioPlaybackStatus::kError, "Playback failed");
    } else if (stopped) {
        SetState(AudioPlaybackStatus::kStopped, "Stopped");
    } else {
        UpdateProgress(wav.data_size, wav.data_size);
        SetState(AudioPlaybackStatus::kCompleted, "Completed");
    }

    ESP_LOGI(TAG, "Playback ended: %s", path.c_str());
    ClearPlaybackTask();
    vTaskDelete(nullptr);
}

void AudioService::SetState(AudioPlaybackStatus status, const char* message) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.status = status;
        if (message != nullptr) {
            state_.message = message;
        }
        state_.volume = volume_;
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

void AudioService::SetCodecOpen(bool open) {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        codec_open_ = open;
        xSemaphoreGive(mutex_);
    }
}

bool AudioService::IsCodecOpen() {
    if (mutex_ == nullptr) {
        return false;
    }
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool open = codec_open_;
    xSemaphoreGive(mutex_);
    return open;
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
