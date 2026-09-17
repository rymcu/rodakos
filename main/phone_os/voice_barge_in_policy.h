#pragma once

#include <cstdint>
#include <cstddef>

namespace rodakos {

class VoiceVadEndPolicy {
public:
    void Reset();
    void Start(uint32_t timestamp_ms);
    bool Observe(uint32_t frame_end_ms, uint32_t duration_ms,
                 bool vad_valid, bool vad_speech);
    bool pending() const { return pending_; }

private:
    bool pending_ = false;
    bool have_frame_ = false;
    uint32_t started_ms_ = 0;
    uint32_t last_frame_ms_ = 0;
    uint32_t silence_ms_ = 0;
};

class VoicePlaybackEpochPolicy {
public:
    void Reset();
    bool AcceptStart(uint32_t epoch);
    bool AcceptAudioOrStop(uint32_t epoch) const;
    void Interrupt();
    uint32_t current_epoch() const { return current_epoch_; }

private:
    bool IsStale(uint32_t epoch) const;
    uint32_t current_epoch_ = 0;
    uint32_t interrupted_epoch_ = 0;
};

class VoiceBargeInPolicy {
public:
    static uint32_t CalculatePcmPower(const int16_t* samples, size_t count);
    void Reset();
    void StartPlayback(uint32_t timestamp_ms);
    void StopPlayback();
    bool Observe(uint32_t frame_end_ms, uint32_t duration_ms,
                 bool vad_valid, bool vad_speech, uint32_t pcm_power, uint32_t& onset_ms);

private:
    void ClearEvidence();
    bool playing_ = false;
    bool fired_ = false;
    bool armed_ = false;
    bool have_frame_ = false;
    uint32_t playback_start_ms_ = 0;
    uint32_t last_frame_ms_ = 0;
    uint32_t silence_ms_ = 0;
    uint32_t speech_ms_ = 0;
    uint32_t onset_ms_ = 0;
};

}  // namespace rodakos
