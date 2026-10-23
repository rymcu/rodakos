#include "phone_os/voice_playback_queue.h"

#include <utility>

namespace rodakos {

bool VoicePlaybackQueue::Push(Frame&& frame) {
    if (frame.pcm.empty()) return true;
    if (frames_.size() >= max_frames_) return false;
    queued_ms_ += frame.frame_duration_ms > 0 ? frame.frame_duration_ms : 0;
    frames_.push_back(std::move(frame));
    return true;
}

bool VoicePlaybackQueue::Pop(Frame& frame) {
    if (frames_.empty()) return false;
    frame = std::move(frames_.front());
    frames_.pop_front();
    queued_ms_ -= frame.frame_duration_ms > 0 ? frame.frame_duration_ms : 0;
    if (queued_ms_ < 0) queued_ms_ = 0;
    return true;
}

void VoicePlaybackQueue::Clear() {
    frames_.clear();
    queued_ms_ = 0;
}

}  // namespace rodakos
