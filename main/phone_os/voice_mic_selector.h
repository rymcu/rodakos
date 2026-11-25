#pragma once

#include <cstddef>
#include <cstdint>

namespace rodakos {

// Chooses MIC1 or MIC2 from the BigSmart four-slot TDM stream
// (slot0=MIC1, slot1=speaker loopback, slot2=MIC2, slot3=MIC4).
class VoiceMicSelector {
public:
    static constexpr size_t kChannels = 4;

    void Reset();
    void ResetSession();
    void Update(const int16_t* tdm, size_t frames);
    int selected_mic() const { return selected_mic_; }
    size_t selected_slot() const { return selected_mic_ == 1 ? 0 : 2; }
    bool playback_hold() const { return hold_samples_ > 0; }
    bool needs_adc_headroom() const { return needs_adc_headroom_; }

private:
    int selected_mic_ = 2;
    int switch_frames_ = 0;
    int64_t mic1_power_ = 0;
    int64_t mic2_power_ = 0;
    bool speech_lock_ = false;
    size_t hold_samples_ = 0;
    bool needs_adc_headroom_ = false;
};

}  // namespace rodakos
