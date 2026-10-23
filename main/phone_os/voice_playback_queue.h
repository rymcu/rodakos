#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace rodakos {

// Decoded TTS frames waiting for the assistant playback worker. Not thread-safe:
// the assistant guards it with its service mutex and clears it whenever a reply is
// interrupted, so it only ever holds audio that is still meant to be heard.
class VoicePlaybackQueue {
public:
    struct Frame {
        uint32_t sample_rate = 0;
        int frame_duration_ms = 0;
        std::vector<int16_t> pcm;
    };

    explicit VoicePlaybackQueue(size_t max_frames) : max_frames_(max_frames) {}

    // Returns false when the queue is full. The queued audio is kept because
    // dropping it would skip words the listener is about to hear.
    bool Push(Frame&& frame);
    bool Pop(Frame& frame);
    void Clear();
    size_t size() const { return frames_.size(); }
    bool empty() const { return frames_.empty(); }
    // Milliseconds of audio still queued.
    int64_t queued_ms() const { return queued_ms_; }

private:
    size_t max_frames_;
    int64_t queued_ms_ = 0;
    std::deque<Frame> frames_;
};

}  // namespace rodakos
