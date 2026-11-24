#pragma once

#include <array>
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
    // Frames the VAD labelled speech during one playback, split by the gate that rejected them.
    // Field logs use these to tune the gates against real echo.
    struct Stats {
        uint32_t vad_speech_ms = 0;
        uint32_t quiet_rejected_ms = 0;
        uint32_t echo_rejected_ms = 0;
    };

    static uint32_t CalculatePcmPower(const int16_t* samples, size_t count);
    void Reset();
    void StartPlayback(uint32_t timestamp_ms);
    void StopPlayback();
    // input_power is the microphone power before echo cancellation; 0 means unknown.
    bool Observe(uint32_t frame_end_ms, uint32_t duration_ms, bool vad_valid, bool vad_speech,
                 uint32_t pcm_power, uint32_t input_power, uint32_t& onset_ms);
    const Stats& stats() const { return stats_; }

private:
    struct SpeechEvidence {
        uint32_t frame_end_ms = 0;
        uint32_t evidence_ms = 0;
    };

    void ClearEvidence();
    void ClearSpeechEvidence();
    uint32_t RecentSpeechEvidenceMs(uint32_t frame_end_ms) const;
    bool playing_ = false;
    bool fired_ = false;
    bool armed_ = false;
    bool have_frame_ = false;
    uint32_t playback_start_ms_ = 0;
    uint32_t last_frame_ms_ = 0;
    uint32_t silence_ms_ = 0;
    uint32_t speech_ms_ = 0;
    uint32_t onset_ms_ = 0;
    // Covers the confirmation window for frames of 20 ms or longer; shorter frames can only
    // undercount evidence.
    std::array<SpeechEvidence, 18> evidence_{};
    size_t evidence_index_ = 0;
    Stats stats_;
};

}  // namespace rodakos
