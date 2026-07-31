#include "phone_os/audio_service.h"

#include "phone_os/audio_output_service.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <inttypes.h>

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

    if (!output_.Init()) {
        SetState(AudioPlaybackStatus::kError, "Audio hardware unavailable");
        return false;
    }

    initialized_ = true;
    SetState(AudioPlaybackStatus::kIdle, "Ready");
    ESP_LOGI(TAG, "Audio service initialized");
    return true;
}

void AudioService::Deinit() {
    ReleasePlaybackHardware();

    if (initialized_) {
        output_.Deinit();
        initialized_ = false;
        SetState(AudioPlaybackStatus::kIdle, "Stopped");
        ESP_LOGI(TAG, "Audio service deinitialized");
    }
}

bool AudioService::PlayFile(const std::string& path, const std::string& title) {
    if (!IsSupportedAudioFile(path)) {
        SetState(AudioPlaybackStatus::kError, "Unsupported audio file");
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
        playback_io_idle_ = false;
        playback_hardware_suspended_ = false;
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
        playback_io_idle_ = false;
        playback_hardware_suspended_ = false;
        xSemaphoreGive(mutex_);
    }
}

bool AudioService::ReleasePlaybackHardware() {
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
    if (mutex_ == nullptr) {
        return;
    }

    bool should_resume = false;
    bool reopen_output = false;
    uint32_t sample_rate = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    should_resume = state_.status == AudioPlaybackStatus::kPaused;
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
        SetState(AudioPlaybackStatus::kError, "Cannot resume audio hardware");
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
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
    const int clamped = std::clamp(volume, 0, 100);
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        volume_ = clamped;
        state_.volume = volume_;
        xSemaphoreGive(mutex_);
    } else {
        volume_ = clamped;
    }

    return output_.SetVolume(volume_);
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

    if (ok) {
        if (stopped) {
            SetState(AudioPlaybackStatus::kStopped, "Stopped");
        } else {
            SetState(AudioPlaybackStatus::kCompleted, "Completed");
        }
    } else if (stopped) {
        SetState(AudioPlaybackStatus::kStopped, "Stopped");
    } else {
        SetGenericPlaybackErrorIfNeeded();
    }

    ESP_LOGI(TAG, "Playback ended: %s", path.c_str());
    ClearPlaybackTask();
}

bool AudioService::PlayWavFile(FILE* fp, const std::string& path, bool& stopped) {
    WavInfo wav = {};
    if (!ReadWavHeader(fp, wav)) {
        SetState(AudioPlaybackStatus::kError, "Unsupported WAV");
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
    SetVolume(volume_);

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
        if (bytes_read == 0) {
            if (feof(fp)) {
                break;
            }
            failed = true;
            break;
        }

        if ((bytes_read % sizeof(int16_t)) == 0) {
            peak = std::max(peak, PeakAbs16(buffer, bytes_read));
        }

        if (!output_.WriteForOwner(kOutputOwner, buffer, static_cast<int>(bytes_read))) {
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

bool AudioService::PlayMp3File(FILE* fp, const std::string& path, bool& stopped) {
    size_t file_size = 0;
    if (!GetFileSize(fp, file_size) || fseek(fp, 0, SEEK_SET) != 0) {
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
    unsigned char* read_ptr = read_buffer;
    MP3FrameInfo frame_info = {};

    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.data_bytes = file_size;
        state_.status = AudioPlaybackStatus::kLoading;
        state_.message = "Loading MP3";
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

        if (bytes_left < kMp3RefillThreshold && !eof_reached) {
            std::memmove(read_buffer, read_ptr, static_cast<size_t>(bytes_left));
            const size_t bytes_read = fread(read_buffer + bytes_left, 1,
                                            kMp3ReadBufferSize - bytes_left, fp);
            if (bytes_read < static_cast<size_t>(kMp3ReadBufferSize - bytes_left)) {
                std::memset(read_buffer + bytes_left + bytes_read, 0,
                            kMp3ReadBufferSize - bytes_left - bytes_read);
            }
            bytes_left += static_cast<int>(bytes_read);
            read_ptr = read_buffer;
            if (bytes_read == 0) {
                eof_reached = true;
            }
        }

        if (bytes_left <= 0) {
            break;
        }

        const int offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (offset < 0) {
            if (eof_reached) {
                break;
            }
            bytes_left = 0;
            read_ptr = read_buffer;
            continue;
        }
        read_ptr += offset;
        bytes_left -= offset;

        const int decode_ret = MP3Decode(decoder, &read_ptr, &bytes_left, pcm_buffer, 0);
        if (decode_ret != ERR_MP3_NONE) {
            if (decode_ret == ERR_MP3_INDATA_UNDERFLOW) {
                if (eof_reached) {
                    break;
                }
                continue;
            }
            if (decode_ret == ERR_MP3_MAINDATA_UNDERFLOW) {
                continue;
            }
            ESP_LOGW(TAG, "MP3 decode failed: %d", decode_ret);
            failed = true;
            break;
        }

        MP3GetLastFrameInfo(decoder, &frame_info);
        if (frame_info.outputSamps <= 0 || frame_info.samprate <= 0 ||
            frame_info.nChans <= 0 || frame_info.bitsPerSample != 16) {
            continue;
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
            SetVolume(volume_);

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
        state_.volume = volume_;
        xSemaphoreGive(mutex_);
    }
}

void AudioService::SetGenericPlaybackErrorIfNeeded() {
    if (mutex_ != nullptr) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        if (state_.status != AudioPlaybackStatus::kError || state_.message.empty()) {
            state_.status = AudioPlaybackStatus::kError;
            state_.message = "Playback failed";
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
