#pragma once

#include <cstdint>

namespace rodakos {

// Maps a user volume (0-100) to the esp_codec_dev volume that produces this project's output curve.
int AudioCodecVolumeForUserVolume(int volume);

class AudioCodecOutput {
public:
    AudioCodecOutput() = default;
    ~AudioCodecOutput();
    AudioCodecOutput(const AudioCodecOutput&) = delete;
    AudioCodecOutput& operator=(const AudioCodecOutput&) = delete;

    bool Init();
    void Deinit();

    bool Open(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, int volume);
    void Close();
    bool Write(const void* data, int bytes);
    bool SetVolume(int volume);

    bool IsReady() const { return initialized_; }
    bool IsOpen() const { return codec_open_; }

private:
    bool MatchesOpenFormat(uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample) const;
    void ClearOpenFormat();

    bool initialized_ = false;
    bool codec_open_ = false;
    uint32_t sample_rate_ = 0;
    uint16_t channels_ = 0;
    uint16_t bits_per_sample_ = 0;
    void* dac_handle_ = nullptr;
    // PSRAM staging for the codec's four-slot frame; kept until Deinit.
    int16_t* frame_buffer_ = nullptr;
};

}  // namespace rodakos
