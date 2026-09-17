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
