#include "test_framework.h"
#include "wake_frontend_runtime.h"
#include "phone_os/voice_wake_frontend.h"

#include <cstdint>
#include <vector>

namespace {
using rodakos::VoiceWakeFrontend;
namespace runtime = rodakos_test::wake_frontend;

// Interleaved TDM read: slot 2 microphone, slot 1 loopback, as on BigSmart.
std::vector<int16_t> Tdm(size_t frames, int16_t microphone, int16_t reference) {
    std::vector<int16_t> tdm(frames * 4, 0);
    for (size_t i = 0; i < frames; ++i) {
        tdm[i * 4 + 1] = reference;
        tdm[i * 4 + 2] = microphone;
    }
    return tdm;
}

std::vector<int16_t> Run(VoiceWakeFrontend& frontend, size_t frames, int16_t microphone,
                         int16_t reference) {
    const auto tdm = Tdm(frames, microphone, reference);
    std::vector<int16_t> output;
    frontend.Process(tdm.data() + 2, 4, tdm.data() + 1, 4, frames, output);
    return output;
}
}  // namespace

RODAK_TEST("Wake frontend cancels against the loopback slot, then applies 10 ms AGC") {
    runtime::Reset();
    VoiceWakeFrontend frontend;
    RODAK_CHECK(frontend.Start(true, true));
    RODAK_CHECK_EQ(runtime::AecFormat(), "MR");
    RODAK_CHECK(runtime::AgcGainDb() > 0);

    // 320 frames: one 256-sample AEC frame -> 256 samples -> one 160-sample AGC block. The
    // 300-LSB loopback is above the playback gate, so AEC runs.
    auto output = Run(frontend, 320, 1000, 300);
    RODAK_CHECK_EQ(output.size(), 160U);
    for (const auto sample : output) RODAK_CHECK_EQ(sample, (1000 - 300) * 2);
    RODAK_CHECK_EQ(runtime::AecFrames(), 1U);
    RODAK_CHECK_EQ(runtime::AgcBlocks(), 1U);

    // Carried-over samples are not lost: after 1024 more frames all 1344 frames have
    // passed AEC (5 frames = 1280) and every complete AGC block is emitted.
    output = Run(frontend, 1024, 1000, 300);
    RODAK_CHECK_EQ(runtime::AecFrames(), 5U);
    RODAK_CHECK_EQ(160U + output.size(), 1280U / 160U * 160U);
}

RODAK_TEST("Wake frontend without AEC passes the microphone, not the loopback, to AGC") {
    runtime::Reset();
    VoiceWakeFrontend frontend;
    RODAK_CHECK(frontend.Start(false, true));
    RODAK_CHECK_FALSE(frontend.aec_active());
    const auto output = Run(frontend, 320, 1000, 300);
    RODAK_CHECK_EQ(output.size(), 320U);
    for (const auto sample : output) RODAK_CHECK_EQ(sample, 2000);
}

RODAK_TEST("Wake frontend AEC-only output keeps the microphone timeline") {
    runtime::Reset();
    VoiceWakeFrontend frontend;
    RODAK_CHECK(frontend.Start(true, false));
    size_t total = 0;
    for (int i = 0; i < 10; ++i) total += Run(frontend, 320, 1000, 300).size();
    // 3200 input frames -> 12 full AEC frames (3072); the remainder waits for the next read.
    RODAK_CHECK_EQ(total, 3072U);
}

RODAK_TEST("Wake frontend runs AEC only while the loopback carries playback, plus a tail") {
    runtime::Reset();
    VoiceWakeFrontend frontend;
    RODAK_CHECK(frontend.Start(true, false));
    // Silent loopback: frames bypass AEC and keep the raw microphone.
    auto output = Run(frontend, 512, 1000, 0);
    RODAK_CHECK_EQ(runtime::AecFrames(), 0U);
    RODAK_CHECK_EQ(frontend.stats().bypassed_frames, 2U);
    for (const auto sample : output) RODAK_CHECK_EQ(sample, 1000);
    // Playback starts: AEC runs.
    output = Run(frontend, 512, 1000, 300);
    RODAK_CHECK_EQ(runtime::AecFrames(), 2U);
    for (const auto sample : output) RODAK_CHECK_EQ(sample, 700);
    // Playback stops: AEC keeps running for the 1 s echo tail, then bypasses again.
    Run(frontend, 16000 + 512, 1000, 0);
    const size_t tail_frames = runtime::AecFrames();
    // 1 s of tail is 62.5 frames of 256 samples; the gate counts whole frames.
    RODAK_CHECK(tail_frames >= 2U + 62U && tail_frames <= 2U + 63U);
    output = Run(frontend, 2048, 1000, 0);
    RODAK_CHECK_EQ(runtime::AecFrames(), tail_frames);
    for (const auto sample : output) RODAK_CHECK_EQ(sample, 1000);
}

RODAK_TEST("Wake frontend failed AEC frames pass the raw microphone through") {
    runtime::Reset();
    VoiceWakeFrontend frontend;
    RODAK_CHECK(frontend.Start(true, false));
    runtime::SetAecFails(true);
    const auto output = Run(frontend, 256, 1000, 300);
    RODAK_CHECK_EQ(output.size(), 256U);
    for (const auto sample : output) RODAK_CHECK_EQ(sample, 1000);
    RODAK_CHECK_EQ(frontend.stats().failures, 1U);
}

RODAK_TEST("Wake frontend stages that cannot be created are skipped and released") {
    runtime::Reset();
    {
        VoiceWakeFrontend frontend;
        runtime::SetCreatable(false, true);
        RODAK_CHECK(frontend.Start(true, true));
        RODAK_CHECK_FALSE(frontend.aec_active());
        RODAK_CHECK(frontend.agc_active());
        runtime::SetCreatable(false, false);
        RODAK_CHECK_FALSE(frontend.Start(true, true));
        RODAK_CHECK_FALSE(frontend.IsActive());
        RODAK_CHECK_EQ(runtime::LiveAgc(), 0U);
        runtime::SetCreatable(true, true);
        RODAK_CHECK(frontend.Start(true, true));
        RODAK_CHECK_EQ(runtime::LiveAec(), 1U);
        RODAK_CHECK_EQ(runtime::LiveAgc(), 1U);
    }
    RODAK_CHECK_EQ(runtime::LiveAec(), 0U);
    RODAK_CHECK_EQ(runtime::LiveAgc(), 0U);
}
