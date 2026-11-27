#include "phone_os/voice_caption_timeline.h"

#include <utility>

namespace rodakos {

void VoiceCaptionTimeline::Begin(uint32_t playback_epoch, std::string fallback_text) {
    Reset();
    active_ = true;
    epoch_ = playback_epoch;
    fallback_text_ = std::move(fallback_text);
    fallback_pending_ = !fallback_text_.empty();
}

void VoiceCaptionTimeline::Reset() {
    active_ = false;
    epoch_ = 0;
    written_seq_ = 0;
    fallback_pending_ = false;
    fallback_text_.clear();
    current_text_.clear();
    cues_.clear();
}

bool VoiceCaptionTimeline::AddCue(uint32_t playback_epoch, uint32_t first_audio_seq,
                                  uint32_t last_audio_seq, std::string text) {
    if (!active_ || playback_epoch != epoch_ || first_audio_seq == 0 ||
        first_audio_seq > last_audio_seq || text.empty() || last_audio_seq <= written_seq_) {
        return false;
    }
    // Cues are the server's sentences, in order. One that starts at or before a queued cue is a
    // replay and is dropped.
    if (!cues_.empty() && first_audio_seq <= cues_.back().last_audio_seq) {
        return false;
    }
    // Any cue, even a later one, means this reply is captioned: the whole-reply fallback would
    // only repeat it.
    fallback_pending_ = false;
    fallback_text_.clear();
    if (cues_.size() >= kMaxPendingCues) {
        cues_.pop_front();
    }
    cues_.push_back(Cue{first_audio_seq, last_audio_seq, std::move(text)});
    return true;
}

bool VoiceCaptionTimeline::Advance(uint32_t playback_epoch, uint32_t audio_seq) {
    if (!active_ || playback_epoch != epoch_ || audio_seq == 0) {
        return false;
    }
    if (audio_seq > written_seq_) {
        written_seq_ = audio_seq;
    }
    if (fallback_pending_) {
        fallback_pending_ = false;
        current_text_ = std::move(fallback_text_);
        fallback_text_.clear();
        return true;
    }
    // Cues whose frames all played before this one were missed, for example because the first
    // frames of a sentence were lost; skip them.
    while (!cues_.empty() && cues_.front().last_audio_seq < audio_seq) {
        cues_.pop_front();
    }
    if (cues_.empty() || cues_.front().first_audio_seq > audio_seq) {
        return false;
    }
    current_text_ = std::move(cues_.front().text);
    cues_.pop_front();
    return true;
}

}  // namespace rodakos
