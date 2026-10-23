#include "test_framework.h"
#include "phone_os/voice_playback_queue.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace {
using rodakos::VoicePlaybackQueue;

VoicePlaybackQueue::Frame MakeFrame(int16_t marker, int duration_ms = 60) {
    VoicePlaybackQueue::Frame frame;
    frame.sample_rate = 16000;
    frame.frame_duration_ms = duration_ms;
    frame.pcm.assign(960, marker);
    return frame;
}
}  // namespace

RODAK_TEST("playback queue plays frames in arrival order and tracks queued duration") {
    VoicePlaybackQueue queue(4);
    RODAK_CHECK(queue.Push(MakeFrame(1)));
    RODAK_CHECK(queue.Push(MakeFrame(2, 20)));
    RODAK_CHECK_EQ(queue.size(), size_t{2});
    RODAK_CHECK_EQ(queue.queued_ms(), int64_t{80});

    VoicePlaybackQueue::Frame frame;
    RODAK_CHECK(queue.Pop(frame));
    RODAK_CHECK_EQ(frame.pcm.front(), int16_t{1});
    RODAK_CHECK_EQ(queue.queued_ms(), int64_t{20});
    RODAK_CHECK(queue.Pop(frame));
    RODAK_CHECK_EQ(frame.pcm.front(), int16_t{2});
    RODAK_CHECK(queue.empty());
    RODAK_CHECK_EQ(queue.queued_ms(), int64_t{0});
    RODAK_CHECK_FALSE(queue.Pop(frame));
}

RODAK_TEST("playback queue keeps queued audio and rejects new frames when full") {
    VoicePlaybackQueue queue(2);
    RODAK_CHECK(queue.Push(MakeFrame(1)));
    RODAK_CHECK(queue.Push(MakeFrame(2)));
    RODAK_CHECK_FALSE(queue.Push(MakeFrame(3)));
    RODAK_CHECK_EQ(queue.size(), size_t{2});
    VoicePlaybackQueue::Frame frame;
    RODAK_CHECK(queue.Pop(frame));
    RODAK_CHECK_EQ(frame.pcm.front(), int16_t{1});
}

RODAK_TEST("playback queue ignores empty frames and clears interrupted audio") {
    VoicePlaybackQueue queue(4);
    VoicePlaybackQueue::Frame empty;
    empty.frame_duration_ms = 60;
    RODAK_CHECK(queue.Push(std::move(empty)));
    RODAK_CHECK(queue.empty());
    RODAK_CHECK(queue.Push(MakeFrame(1)));
    RODAK_CHECK(queue.Push(MakeFrame(2)));
    queue.Clear();
    RODAK_CHECK(queue.empty());
    RODAK_CHECK_EQ(queue.queued_ms(), int64_t{0});
}
