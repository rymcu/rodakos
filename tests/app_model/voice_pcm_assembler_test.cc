#include "test_framework.h"
#include "phone_os/voice_pcm_assembler.h"

#include <numeric>

namespace {
using rodakos::VoicePcmAssembler;
using rodakos::VoicePcmFrame;
using rodakos::VoiceRecorderConfig;

VoiceRecorderConfig Config() {
    VoiceRecorderConfig config;
    config.frame_duration_ms = 60;
    return config;
}
}

RODAK_TEST("pcm assembly retains every sample across repeated fetch timeouts") {
    VoicePcmAssembler assembler;
    std::deque<VoicePcmFrame> frames;
    std::vector<int16_t> input(1920);
    std::iota(input.begin(), input.end(), int16_t{1});
    assembler.Append(input.data(), 512, Config(), 732000, true, true, frames, 80);
    RODAK_CHECK(frames.empty());
    assembler.InvalidateContinuity();
    assembler.InvalidateContinuity();
    assembler.Append(input.data() + 512, 512, Config(), 1000000, true, true, frames, 80);
    RODAK_CHECK_EQ(frames.size(), size_t{1});
    RODAK_CHECK_EQ(frames[0].timestamp_ms, uint32_t{996});
    RODAK_CHECK_FALSE(frames[0].vad_valid);
    RODAK_CHECK_FALSE(frames[0].vad_speech);
    assembler.Append(input.data() + 1024, 896, Config(), 1056000, true, true, frames, 80);
    RODAK_CHECK_EQ(frames.size(), size_t{2});
    RODAK_CHECK_EQ(frames[1].timestamp_ms, uint32_t{1056});
    RODAK_CHECK(frames[1].vad_valid);
    RODAK_CHECK(frames[1].vad_speech);
    std::vector<int16_t> actual = frames[0].samples;
    actual.insert(actual.end(), frames[1].samples.begin(), frames[1].samples.end());
    RODAK_CHECK(actual == input);
}

RODAK_TEST("pcm assembly marks only the frame crossing a fetch gap invalid") {
    VoicePcmAssembler assembler;
    std::deque<VoicePcmFrame> frames;
    const std::vector<int16_t> before(512, 17);
    const std::vector<int16_t> after(2368, 23);
    assembler.Append(before.data(), before.size(), Config(), 500000, true, true, frames, 80);
    assembler.InvalidateContinuity();
    assembler.Append(after.data(), after.size(), Config(), 1000000, true, false, frames, 80);
    RODAK_CHECK_EQ(frames.size(), size_t{3});
    RODAK_CHECK_FALSE(frames[0].vad_valid);
    RODAK_CHECK(frames[1].vad_valid);
    RODAK_CHECK(frames[2].vad_valid);
    RODAK_CHECK_FALSE(frames[1].vad_speech);
    RODAK_CHECK_EQ(frames[0].timestamp_ms, uint32_t{880});
    RODAK_CHECK_EQ(frames[1].timestamp_ms, uint32_t{940});
    RODAK_CHECK_EQ(frames[2].timestamp_ms, uint32_t{1000});
}

RODAK_TEST("pcm assembly explicit reset discards old session partial audio") {
    VoicePcmAssembler assembler;
    std::deque<VoicePcmFrame> frames;
    const std::vector<int16_t> before(512, 17);
    const std::vector<int16_t> after(960, 23);
    assembler.Append(before.data(), before.size(), Config(), 500000, true, true, frames, 80);
    assembler.InvalidateContinuity();
    assembler.Reset();
    assembler.Append(after.data(), after.size(), Config(), 1000000, true, false, frames, 80);
    RODAK_CHECK_EQ(frames.size(), size_t{1});
    RODAK_CHECK(frames[0].samples == after);
    RODAK_CHECK(frames[0].vad_valid);
    RODAK_CHECK_EQ(frames[0].timestamp_ms, uint32_t{1000});
}

RODAK_TEST("pcm assembly carries the loudest input power of each frame and resets it") {
    VoicePcmAssembler assembler;
    std::deque<VoicePcmFrame> frames;
    const std::vector<int16_t> input(1920, 42);
    assembler.Append(input.data(), 512, Config(), 1032000, true, true, frames, 80, 500);
    assembler.Append(input.data() + 512, 512, Config(), 1064000, true, true, frames, 80, 9000);
    assembler.Append(input.data() + 1024, 896, Config(), 1120000, true, true, frames, 80, 70);
    RODAK_CHECK_EQ(frames.size(), size_t{2});
    // Frame 1 spans the first two fetches; frame 2 takes the tail of fetch 2 and all of fetch 3.
    RODAK_CHECK_EQ(frames[0].input_power, uint32_t{9000});
    RODAK_CHECK_EQ(frames[1].input_power, uint32_t{9000});
    assembler.Append(input.data(), 960, Config(), 1180000, true, true, frames, 80, 70);
    RODAK_CHECK_EQ(frames[2].input_power, uint32_t{70});
    assembler.Append(input.data(), 512, Config(), 1212000, true, true, frames, 80, 9000);
    assembler.Reset();
    assembler.Append(input.data(), 960, Config(), 1300000, true, true, frames, 80);
    RODAK_CHECK_EQ(frames[3].input_power, uint32_t{0});
}

RODAK_TEST("pcm assembly pulls a late first anchor back so timestamps never pass fetch time") {
    VoicePcmAssembler assembler;
    std::deque<VoicePcmFrame> frames;
    const std::vector<int16_t> input(960, 42);
    // The first fetch returns 50 ms late; later fetches arrive on time.
    assembler.Append(input.data(), 960, Config(), 1050000, true, false, frames, 80);
    RODAK_CHECK_EQ(frames[0].timestamp_ms, uint32_t{1050});
    assembler.Append(input.data(), 960, Config(), 1060000, true, false, frames, 80);
    RODAK_CHECK_EQ(frames[1].timestamp_ms, uint32_t{1060});
    assembler.Append(input.data(), 960, Config(), 1120000, true, false, frames, 80);
    RODAK_CHECK_EQ(frames[2].timestamp_ms, uint32_t{1120});
    // A late fetch does not move timestamps forward.
    assembler.Append(input.data(), 960, Config(), 1230000, true, false, frames, 80);
    RODAK_CHECK_EQ(frames[3].timestamp_ms, uint32_t{1180});
}

RODAK_TEST("pcm assembly preserves normal sample weighted VAD across fetch boundaries") {
    VoicePcmAssembler assembler;
    std::deque<VoicePcmFrame> frames;
    const std::vector<int16_t> input(1920, 42);
    assembler.Append(input.data(), 512, Config(), 1032000, true, true, frames, 80);
    assembler.Append(input.data() + 512, 1408, Config(), 1120000, true, false, frames, 80);
    RODAK_CHECK_EQ(frames.size(), size_t{2});
    RODAK_CHECK(frames[0].vad_valid);
    RODAK_CHECK(frames[0].vad_speech);
    RODAK_CHECK(frames[1].vad_valid);
    RODAK_CHECK_FALSE(frames[1].vad_speech);
    RODAK_CHECK_EQ(frames[0].timestamp_ms, uint32_t{1060});
    RODAK_CHECK_EQ(frames[1].timestamp_ms, uint32_t{1120});
}

RODAK_TEST("pcm assembly reanchors after cumulative sub-stall RX sample loss") {
    VoicePcmAssembler assembler;
    std::deque<VoicePcmFrame> frames;
    const std::vector<int16_t> input(960, 42);
    assembler.Append(input.data(), 960, Config(), 1060000, true, true, frames, 80);
    RODAK_CHECK_EQ(frames.size(), size_t{1});
    RODAK_CHECK_EQ(frames[0].timestamp_ms, uint32_t{1060});
    // 320 ms of silent DMA sample loss (> 250 ms threshold): marks crossing frame invalid
    // and re-anchors subsequent frames to wall clock so VAD freshness recovers.
    assembler.Append(input.data(), 960, Config(), 1440000, true, true, frames, 80);
    RODAK_CHECK_EQ(frames.size(), size_t{2});
    RODAK_CHECK_FALSE(frames[1].vad_valid);
    RODAK_CHECK_EQ(frames[1].timestamp_ms, uint32_t{1440});
    assembler.Append(input.data(), 960, Config(), 1500000, true, true, frames, 80);
    RODAK_CHECK_EQ(frames.size(), size_t{3});
    RODAK_CHECK(frames[2].vad_valid);
    RODAK_CHECK(frames[2].vad_speech);
    RODAK_CHECK_EQ(frames[2].timestamp_ms, uint32_t{1500});
}

RODAK_TEST("pcm assembly keeps timestamps strictly monotonic across backlogged same-ms fetches") {
    VoicePcmAssembler assembler;
    std::deque<VoicePcmFrame> frames;
    const std::vector<int16_t> chunk(512, 19);
    // First 15 fetches of 512 samples = 7680 samples = exactly 8 frames of 960 samples;
    // late first fetch puts clock_start_us_ 60 ms ahead of real capture.
    int64_t t_us = 1092000;
    for (int i = 0; i < 15; ++i) {
        assembler.Append(chunk.data(), chunk.size(), Config(), t_us, true, true, frames, 80);
        t_us += 32000;
    }
    RODAK_CHECK_EQ(frames.size(), size_t{8});
    const uint32_t eighth_ts = frames.back().timestamp_ms;
    // Two backlogged fetches arrive in the same millisecond before eighth_ts: frame 9 must
    // still advance monotonically rather than pulling back and resetting VAD evidence.
    const int64_t burst_us = static_cast<int64_t>(eighth_ts - 4) * 1000;
    assembler.Append(chunk.data(), chunk.size(), Config(), burst_us, true, true, frames, 80);
    assembler.Append(chunk.data(), chunk.size(), Config(), burst_us, true, true, frames, 80);
    RODAK_CHECK_EQ(frames.size(), size_t{9});
    RODAK_CHECK(frames[8].vad_valid);
    RODAK_CHECK_EQ(frames[8].timestamp_ms, eighth_ts + 1);
}
