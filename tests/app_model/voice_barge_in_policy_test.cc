#include "test_framework.h"
#include "phone_os/voice_barge_in_policy.h"
#include <vector>

namespace {
using rodakos::VoiceBargeInPolicy;
using rodakos::VoicePlaybackEpochPolicy;
using rodakos::VoiceVadEndPolicy;
bool Frames(VoiceBargeInPolicy& policy, uint32_t& time, bool speech,
            int count, uint32_t& onset, bool valid = true) {
    bool triggered = false;
    for (int i = 0; i < count; ++i) {
        time += 60;
        triggered = policy.Observe(time, 60, valid, speech, speech ? 65536 : 0, onset) || triggered;
    }
    return triggered;
}
}

RODAK_TEST("barge in rejects measured residual echo even when VAD labels speech") {
    VoiceBargeInPolicy policy;
    policy.StartPlayback(1000);
    uint32_t time = 1000, onset = 0;
    for (uint32_t rms : {0U, 0U, 0U, 77U, 104U, 75U, 21U, 20U, 107U, 153U, 153U, 153U, 16U}) {
        time += 60;
        RODAK_CHECK_FALSE(policy.Observe(time, 60, true, true, rms * rms, onset));
    }
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK(Frames(policy, time, true, 3, onset));
}

RODAK_TEST("barge in energy rejects a short transient and resets speech confirmation") {
    VoiceBargeInPolicy policy;
    uint32_t time = 1000, onset = 0;
    policy.StartPlayback(time);
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK_FALSE(Frames(policy, time, true, 2, onset));
    time += 60;
    RODAK_CHECK_FALSE(policy.Observe(time, 60, true, true, 255 * 255, onset));
    RODAK_CHECK_FALSE(Frames(policy, time, true, 3, onset));
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK(Frames(policy, time, true, 3, onset));
}

RODAK_TEST("barge in PCM energy excludes DC and handles full scale without overflow") {
    std::vector<int16_t> samples(960, 3000);
    RODAK_CHECK_EQ(VoiceBargeInPolicy::CalculatePcmPower(samples.data(), samples.size()), uint32_t{0});
    for (size_t i = 0; i < samples.size(); ++i) samples[i] += i % 2 ? 128 : -128;
    RODAK_CHECK_EQ(VoiceBargeInPolicy::CalculatePcmPower(samples.data(), samples.size()), uint32_t{16384});
    for (size_t i = 0; i < samples.size(); ++i) samples[i] = i % 2 ? 32767 : -32768;
    RODAK_CHECK(VoiceBargeInPolicy::CalculatePcmPower(samples.data(), samples.size()) > 1000000000U);
    RODAK_CHECK_EQ(VoiceBargeInPolicy::CalculatePcmPower(nullptr, 960), uint32_t{0});
}

RODAK_TEST("playback epoch rejects interrupted start audio stop and accepts fresh response") {
    VoicePlaybackEpochPolicy policy;
    RODAK_CHECK(policy.AcceptStart(1));
    RODAK_CHECK(policy.AcceptAudioOrStop(1));
    policy.Interrupt();
    RODAK_CHECK_FALSE(policy.AcceptStart(1));
    RODAK_CHECK_FALSE(policy.AcceptAudioOrStop(1));
    RODAK_CHECK_FALSE(policy.AcceptStart(0));
    RODAK_CHECK_FALSE(policy.AcceptAudioOrStop(0));
    RODAK_CHECK_FALSE(policy.AcceptAudioOrStop(2));
    RODAK_CHECK(policy.AcceptStart(2));
    RODAK_CHECK(policy.AcceptAudioOrStop(2));
    RODAK_CHECK_FALSE(policy.AcceptStart(1));
    RODAK_CHECK_FALSE(policy.AcceptAudioOrStop(1));
}

RODAK_TEST("playback epoch rejects stop tagged with next cancellation epoch") {
    VoicePlaybackEpochPolicy policy;
    RODAK_CHECK(policy.AcceptStart(4));
    RODAK_CHECK_FALSE(policy.AcceptAudioOrStop(5));
    RODAK_CHECK_EQ(policy.current_epoch(), uint32_t{4});
    RODAK_CHECK(policy.AcceptAudioOrStop(4));
    policy.Interrupt();
    RODAK_CHECK(policy.AcceptStart(6));
    RODAK_CHECK_FALSE(policy.AcceptAudioOrStop(5));
    RODAK_CHECK(policy.AcceptAudioOrStop(6));
}

RODAK_TEST("playback epoch permits normal same epoch turns and legacy manual fallback") {
    VoicePlaybackEpochPolicy policy;
    RODAK_CHECK(policy.AcceptStart(0));
    policy.Interrupt();
    RODAK_CHECK(policy.AcceptStart(0));
    RODAK_CHECK(policy.AcceptAudioOrStop(0));
    RODAK_CHECK(policy.AcceptStart(7));
    RODAK_CHECK(policy.AcceptAudioOrStop(7));
    RODAK_CHECK(policy.AcceptStart(7));
    RODAK_CHECK_FALSE(policy.AcceptStart(0));
    policy.Reset();
    RODAK_CHECK_EQ(policy.current_epoch(), uint32_t{0});
    RODAK_CHECK(policy.AcceptStart(1));
}

RODAK_TEST("explicit interruption preserves previous vad end and blocks newly interrupted epoch") {
    VoicePlaybackEpochPolicy playback;
    VoiceVadEndPolicy vad_end;
    RODAK_CHECK(playback.AcceptStart(1));
    playback.Interrupt();
    vad_end.Start(1000);
    RODAK_CHECK(playback.AcceptStart(2));
    RODAK_CHECK(vad_end.pending());
    playback.Interrupt();
    RODAK_CHECK_FALSE(playback.AcceptStart(2));
    RODAK_CHECK_FALSE(playback.AcceptAudioOrStop(2));
    RODAK_CHECK(vad_end.pending());
    RODAK_CHECK_FALSE(vad_end.Observe(1060, 60, true, false));
    RODAK_CHECK_FALSE(vad_end.Observe(1120, 60, true, false));
    RODAK_CHECK(vad_end.Observe(1180, 60, true, false));
    RODAK_CHECK_FALSE(vad_end.pending());
    RODAK_CHECK(playback.AcceptStart(3));
    RODAK_CHECK(playback.AcceptAudioOrStop(3));
}

RODAK_TEST("barge in requires fresh silence then sustained speech and fires once") {
    VoiceBargeInPolicy policy;
    uint32_t time = 1000, onset = 0;
    policy.StartPlayback(time);
    RODAK_CHECK_FALSE(Frames(policy, time, true, 10, onset));
    RODAK_CHECK_FALSE(Frames(policy, time, false, 3, onset));
    const uint32_t expected_onset = time;
    RODAK_CHECK_FALSE(Frames(policy, time, true, 2, onset));
    RODAK_CHECK(Frames(policy, time, true, 1, onset));
    RODAK_CHECK_EQ(onset, expected_onset);
    RODAK_CHECK_FALSE(Frames(policy, time, false, 6, onset));
    RODAK_CHECK_FALSE(Frames(policy, time, true, 6, onset));
}

RODAK_TEST("barge in ignores short noise and requires silence rearm") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(time);
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK_FALSE(Frames(policy, time, true, 2, onset));
    Frames(policy, time, false, 1, onset);
    RODAK_CHECK_FALSE(Frames(policy, time, true, 4, onset));
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK(Frames(policy, time, true, 3, onset));
}

RODAK_TEST("barge in invalid vad or missing capture breaks confirmation") {
    for (bool gap : {false, true}) {
        VoiceBargeInPolicy policy;
        uint32_t time = 0, onset = 0;
        policy.StartPlayback(time);
        Frames(policy, time, false, 3, onset);
        Frames(policy, time, true, 2, onset);
        if (gap) time += 120;
        RODAK_CHECK_FALSE(Frames(policy, time, true, 1, onset, gap));
        RODAK_CHECK_FALSE(Frames(policy, time, true, 3, onset));
        Frames(policy, time, false, 3, onset);
        RODAK_CHECK(Frames(policy, time, true, 3, onset));
    }
}

RODAK_TEST("barge in rejects queued frames before playback and resets for next playback") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(1000);
    Frames(policy, time, false, 3, onset);
    time = 1000;
    RODAK_CHECK_FALSE(Frames(policy, time, true, 3, onset));
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK(Frames(policy, time, true, 3, onset));
    policy.StopPlayback();
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK_FALSE(Frames(policy, time, true, 3, onset));
    policy.StartPlayback(time);
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK(Frames(policy, time, true, 3, onset));
}

RODAK_TEST("barge in timestamp rollover preserves onset") {
    VoiceBargeInPolicy policy;
    uint32_t time = UINT32_MAX - 200, onset = 0;
    policy.StartPlayback(time);
    Frames(policy, time, false, 3, onset);
    const uint32_t expected = time;
    RODAK_CHECK(Frames(policy, time, true, 3, onset));
    RODAK_CHECK_EQ(onset, expected);
}

RODAK_TEST("vad end waits for fresh sustained silence then fires once") {
    VoiceVadEndPolicy policy;
    policy.Start(1000);
    RODAK_CHECK_FALSE(policy.Observe(900, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(1060, 60, true, true));
    RODAK_CHECK_FALSE(policy.Observe(1120, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(1180, 60, true, false));
    RODAK_CHECK(policy.Observe(1240, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(1300, 60, true, false));
    policy.Start(1300);
    RODAK_CHECK_FALSE(policy.Observe(1360, 60, true, false));
    policy.Reset();
    RODAK_CHECK_FALSE(policy.Observe(1420, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(1480, 60, true, false));
}

RODAK_TEST("vad end invalid frame and capture gap reset silence evidence") {
    VoiceVadEndPolicy policy;
    policy.Start(0);
    RODAK_CHECK_FALSE(policy.Observe(60, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(120, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(180, 60, false, false));
    RODAK_CHECK_FALSE(policy.Observe(240, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(300, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(480, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(540, 60, true, false));
    RODAK_CHECK(policy.Observe(600, 60, true, false));
}

RODAK_TEST("barge in duplicate timestamps cannot accumulate evidence") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(time);
    Frames(policy, time, false, 3, onset);
    Frames(policy, time, true, 2, onset);
    RODAK_CHECK_FALSE(policy.Observe(time, 60, true, true, 65536, onset));
    RODAK_CHECK_FALSE(Frames(policy, time, true, 3, onset));
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK(Frames(policy, time, true, 3, onset));
}
