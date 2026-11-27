#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace rodakos {

// Matches output.caption cues (realtime voice contract section 6.2) to the RAV1 frame the speaker
// is playing now. Cues arrive with the reply audio, up to about three seconds before that audio is
// heard, so the text follows the frames the playback task actually writes, not network arrival.
// Not thread-safe: the assistant guards it with its service mutex.
class VoiceCaptionTimeline {
public:
    // Cues held ahead of the speaker. A burst beyond this drops the oldest cue, never audio.
    static constexpr size_t kMaxPendingCues = 12;

    // An output.start: forget the previous reply and follow this epoch. fallback_text is the
    // output.start text of a session without output.caption; it shows from the first written
    // frame until the reply ends. A cue replaces it.
    void Begin(uint32_t playback_epoch, std::string fallback_text = {});
    // Interrupt, stop, error or end of the reply: nothing is shown until the next Begin.
    void Reset();
    // Queue a cue of the current epoch. Cues of another epoch, and cues whose frames have already
    // been written, are dropped. Returns true when the cue was kept.
    bool AddCue(uint32_t playback_epoch, uint32_t first_audio_seq, uint32_t last_audio_seq,
                std::string text);
    // The playback task wrote this frame to the speaker. Returns true when the shown text changed.
    // A cue stays on screen after its last frame until the next cue starts, so short gaps between
    // sentences do not blink.
    bool Advance(uint32_t playback_epoch, uint32_t audio_seq);

    const std::string& current_text() const { return current_text_; }
    bool active() const { return active_; }
    uint32_t epoch() const { return epoch_; }
    size_t pending_cues() const { return cues_.size(); }

private:
    struct Cue {
        uint32_t first_audio_seq = 0;
        uint32_t last_audio_seq = 0;
        std::string text;
    };

    bool active_ = false;
    uint32_t epoch_ = 0;
    uint32_t written_seq_ = 0;
    bool fallback_pending_ = false;
    std::string fallback_text_;
    std::string current_text_;
    std::deque<Cue> cues_;
};

}  // namespace rodakos
