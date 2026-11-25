#include "test_framework.h"
#include "phone_os/voice_barge_in_policy.h"
#include <vector>

namespace {
using rodakos::VoiceBargeInPolicy;
using rodakos::VoicePlaybackEpochPolicy;
using rodakos::VoiceVadEndPolicy;
// 60 ms frames: 400 ms grace plus 180 ms of silence arm the policy after ten quiet frames,
// and 360 ms of speech (six frames) confirms an interruption.
constexpr int kArmFrames = 10;
constexpr int kConfirmFrames = 6;
constexpr uint32_t kLoud = 65536;

bool Frames(VoiceBargeInPolicy& policy, uint32_t& time, bool speech,
            int count, uint32_t& onset, bool valid = true, uint32_t input_power = 0) {
    bool triggered = false;
    for (int i = 0; i < count; ++i) {
        time += 60;
        triggered = policy.Observe(time, 60, valid, speech, speech ? kLoud : 0,
                                   input_power, onset) || triggered;
    }
    return triggered;
}

void Arm(VoiceBargeInPolicy& policy, uint32_t& time, uint32_t& onset) {
    Frames(policy, time, false, kArmFrames, onset);
}
}

RODAK_TEST("barge in rejects measured residual echo even when VAD labels speech") {
    VoiceBargeInPolicy policy;
    uint32_t time = 1000, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    for (uint32_t rms : {0U, 0U, 0U, 77U, 104U, 75U, 21U, 20U, 107U, 153U, 153U, 153U, 16U}) {
        time += 60;
        RODAK_CHECK_FALSE(policy.Observe(time, 60, true, true, rms * rms, 0, onset));
    }
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK(Frames(policy, time, true, kConfirmFrames, onset));
}

RODAK_TEST("barge in ignores speech in the playback start grace window") {
    VoiceBargeInPolicy policy;
    uint32_t time = 1000, onset = 0;
    policy.StartPlayback(time);
    // Echo that starts with the reply: AEC has not adapted yet, so it is loud and labelled speech.
    RODAK_CHECK_FALSE(Frames(policy, time, true, 6, onset));
    // Silence inside the grace window does not count towards arming.
    time = 1000;
    policy.StartPlayback(time);
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK_FALSE(Frames(policy, time, true, kConfirmFrames, onset));
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK(Frames(policy, time, true, kConfirmFrames, onset));
}

RODAK_TEST("barge in needs sustained speech longer than an echo burst") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    // 300 ms of loud, speech-labelled residual (the old 180 ms confirmation fired here).
    RODAK_CHECK_FALSE(Frames(policy, time, true, 5, onset));
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK(Frames(policy, time, true, kConfirmFrames, onset));
}

RODAK_TEST("barge in rejects output far below the microphone input as echo residual") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    // Loud speaker echo at the microphone, 18 dB removed by AEC: output is 1/64 of input.
    RODAK_CHECK_FALSE(Frames(policy, time, true, 10, onset, true, kLoud * 64));
    RODAK_CHECK_EQ(policy.stats().echo_rejected_ms, uint32_t{600});
    RODAK_CHECK_EQ(policy.stats().vad_speech_ms, uint32_t{600});
    // Near-end speech over the same echo keeps most of its level through AEC.
    RODAK_CHECK(Frames(policy, time, true, kConfirmFrames, onset, true, kLoud * 4));
    RODAK_CHECK_EQ(policy.stats().echo_rejected_ms, uint32_t{600});
}

RODAK_TEST("barge in needs 180 ms of energy evidence inside the recent VAD run") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    const uint32_t expected_onset = time;
    // Echo residual labelled speech: one passing frame every three, as measured on hardware.
    for (int i = 0; i < 4; ++i) {
        RODAK_CHECK_FALSE(Frames(policy, time, true, 2, onset, true, kLoud * 64));
        RODAK_CHECK_FALSE(Frames(policy, time, true, 1, onset, true, kLoud * 2));
    }
    // Near-end speech adds a passing frame; three now fall inside the last 360 ms.
    RODAK_CHECK_FALSE(Frames(policy, time, true, 1, onset, true, kLoud * 64));
    RODAK_CHECK(Frames(policy, time, true, 1, onset, true, kLoud * 2));
    (void)expected_onset;
    RODAK_CHECK_EQ(onset, time - 360);
}

RODAK_TEST("barge in evidence from an earlier VAD run does not carry over") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    RODAK_CHECK_FALSE(Frames(policy, time, true, 2, onset));
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK_FALSE(Frames(policy, time, true, 5, onset, true, kLoud * 64));
    RODAK_CHECK_FALSE(Frames(policy, time, true, 1, onset));
}

RODAK_TEST("barge in quiet syllable gaps inside speech do not reset confirmation") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    const uint32_t expected_onset = time;
    RODAK_CHECK_FALSE(Frames(policy, time, true, 2, onset));
    for (int i = 0; i < 3; ++i) {
        time += 60;
        RODAK_CHECK_FALSE(policy.Observe(time, 60, true, true, 100 * 100, 0, onset));
    }
    RODAK_CHECK(Frames(policy, time, true, 1, onset));
    RODAK_CHECK_EQ(onset, expected_onset);
}

RODAK_TEST("barge in stats count quiet rejections and reset with playback") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    time += 60;
    RODAK_CHECK_FALSE(policy.Observe(time, 60, true, true, 100 * 100, 0, onset));
    RODAK_CHECK_EQ(policy.stats().vad_speech_ms, uint32_t{60});
    RODAK_CHECK_EQ(policy.stats().quiet_rejected_ms, uint32_t{60});
    RODAK_CHECK_EQ(policy.stats().echo_rejected_ms, uint32_t{0});
    policy.StopPlayback();
    RODAK_CHECK_EQ(policy.stats().vad_speech_ms, uint32_t{0});
    RODAK_CHECK_EQ(policy.stats().quiet_rejected_ms, uint32_t{0});
}

RODAK_TEST("barge in energy rejects speech-labelled frames just below the floor") {
    VoiceBargeInPolicy policy;
    uint32_t time = 1000, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    for (int i = 0; i < 12; ++i) {
        time += 60;
        RODAK_CHECK_FALSE(policy.Observe(time, 60, true, true, 255 * 255, 0, onset));
    }
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK(Frames(policy, time, true, kConfirmFrames, onset));
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
    uint32_t time = 1000;
    for (int i = 0; i < 9; ++i) {
        time += 60;
        RODAK_CHECK_FALSE(vad_end.Observe(time, 60, true, false));
    }
    RODAK_CHECK(vad_end.Observe(time + 60, 60, true, false));
    RODAK_CHECK_FALSE(vad_end.pending());
    RODAK_CHECK(playback.AcceptStart(3));
    RODAK_CHECK(playback.AcceptAudioOrStop(3));
}

RODAK_TEST("barge in requires fresh silence then sustained speech and fires once") {
    VoiceBargeInPolicy policy;
    uint32_t time = 1000, onset = 0;
    policy.StartPlayback(time);
    RODAK_CHECK_FALSE(Frames(policy, time, true, 12, onset));
    RODAK_CHECK_FALSE(Frames(policy, time, false, 3, onset));
    const uint32_t expected_onset = time;
    RODAK_CHECK_FALSE(Frames(policy, time, true, kConfirmFrames - 1, onset));
    RODAK_CHECK(Frames(policy, time, true, 1, onset));
    RODAK_CHECK_EQ(onset, expected_onset);
    RODAK_CHECK_FALSE(Frames(policy, time, false, 6, onset));
    RODAK_CHECK_FALSE(Frames(policy, time, true, 8, onset));
}

RODAK_TEST("barge in ignores short noise and requires silence rearm") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    RODAK_CHECK_FALSE(Frames(policy, time, true, 5, onset));
    Frames(policy, time, false, 1, onset);
    RODAK_CHECK_FALSE(Frames(policy, time, true, 7, onset));
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK(Frames(policy, time, true, kConfirmFrames, onset));
}

RODAK_TEST("barge in invalid vad or missing capture breaks confirmation") {
    for (bool gap : {false, true}) {
        VoiceBargeInPolicy policy;
        uint32_t time = 0, onset = 0;
        policy.StartPlayback(time);
        Arm(policy, time, onset);
        Frames(policy, time, true, 5, onset);
        if (gap) time += 120;
        RODAK_CHECK_FALSE(Frames(policy, time, true, 1, onset, gap));
        RODAK_CHECK_FALSE(Frames(policy, time, true, 5, onset));
        Frames(policy, time, false, 3, onset);
        RODAK_CHECK(Frames(policy, time, true, kConfirmFrames, onset));
    }
}

RODAK_TEST("barge in rejects queued frames before playback and resets for next playback") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(1000);
    Frames(policy, time, false, 3, onset);
    time = 1000;
    RODAK_CHECK_FALSE(Frames(policy, time, true, kConfirmFrames, onset));
    Arm(policy, time, onset);
    RODAK_CHECK(Frames(policy, time, true, kConfirmFrames, onset));
    policy.StopPlayback();
    Arm(policy, time, onset);
    RODAK_CHECK_FALSE(Frames(policy, time, true, kConfirmFrames, onset));
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    RODAK_CHECK(Frames(policy, time, true, kConfirmFrames, onset));
}

RODAK_TEST("barge in timestamp rollover preserves onset") {
    VoiceBargeInPolicy policy;
    uint32_t time = UINT32_MAX - 800, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    const uint32_t expected = time;
    RODAK_CHECK(Frames(policy, time, true, kConfirmFrames, onset));
    RODAK_CHECK_EQ(onset, expected);
}

RODAK_TEST("vad end waits for fresh sustained silence then fires once") {
    VoiceVadEndPolicy policy;
    policy.Start(1000);
    RODAK_CHECK_FALSE(policy.Observe(900, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(1060, 60, true, true));
    uint32_t time = 1060;
    for (int i = 0; i < 9; ++i) {
        time += 60;
        RODAK_CHECK_FALSE(policy.Observe(time, 60, true, false));
    }
    time += 60;
    RODAK_CHECK(policy.Observe(time, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(time + 60, 60, true, false));
    policy.Start(time + 60);
    RODAK_CHECK_FALSE(policy.Observe(time + 120, 60, true, false));
    policy.Reset();
    RODAK_CHECK_FALSE(policy.Observe(time + 180, 60, true, false));
}

RODAK_TEST("vad end survives a short pause inside the utterance") {
    VoiceVadEndPolicy policy;
    policy.Start(0);
    uint32_t time = 0;
    // A 300 ms pause between words (the old 180 ms rule ended the utterance here).
    for (int i = 0; i < 5; ++i) RODAK_CHECK_FALSE(policy.Observe(time += 60, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(time += 60, 60, true, true));
    for (int i = 0; i < 9; ++i) RODAK_CHECK_FALSE(policy.Observe(time += 60, 60, true, false));
    RODAK_CHECK(policy.Observe(time += 60, 60, true, false));
}

RODAK_TEST("vad end invalid frame and capture gap reset silence evidence") {
    VoiceVadEndPolicy policy;
    policy.Start(0);
    uint32_t time = 0;
    for (int i = 0; i < 9; ++i) RODAK_CHECK_FALSE(policy.Observe(time += 60, 60, true, false));
    RODAK_CHECK_FALSE(policy.Observe(time += 60, 60, false, false));
    for (int i = 0; i < 9; ++i) RODAK_CHECK_FALSE(policy.Observe(time += 60, 60, true, false));
    time += 180;
    for (int i = 0; i < 9; ++i) RODAK_CHECK_FALSE(policy.Observe(time += 60, 60, true, false));
    RODAK_CHECK(policy.Observe(time += 60, 60, true, false));
}

RODAK_TEST("barge in duplicate timestamps cannot accumulate evidence") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    Frames(policy, time, true, 5, onset);
    RODAK_CHECK_FALSE(policy.Observe(time, 60, true, true, kLoud, 0, onset));
    RODAK_CHECK_FALSE(Frames(policy, time, true, 5, onset));
    Frames(policy, time, false, 3, onset);
    RODAK_CHECK(Frames(policy, time, true, kConfirmFrames, onset));
}

RODAK_TEST("barge in reanchors onset past pure echo prefix when real speech follows") {
    VoiceBargeInPolicy policy;
    uint32_t time = 1000, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    // 600 ms of pure echo mislabelled as VAD speech.
    RODAK_CHECK_FALSE(Frames(policy, time, true, 10, onset, true, kLoud * 64));
    const uint32_t speech_start = time;
    RODAK_CHECK(Frames(policy, time, true, 3, onset, true, kLoud * 4));
    RODAK_CHECK_EQ(onset, speech_start);
}

RODAK_TEST("barge in confirms strong near-end speech over loud speaker echo") {
    VoiceBargeInPolicy policy;
    uint32_t time = 0, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    constexpr uint32_t kLoudSpeakerMic = 20000000;
    // Weak residual echo (150,000 < 262,144) is still rejected under loud playback.
    for (int i = 0; i < 6; ++i) {
        time += 60;
        RODAK_CHECK_FALSE(policy.Observe(time, 60, true, true, 150000, kLoudSpeakerMic, onset));
    }
    Frames(policy, time, false, 3, onset);
    // Strong post-AEC near-end speech (300,000 >= 262,144) confirms despite 20e6 echo on mic.
    for (int i = 0; i < 5; ++i) {
        time += 60;
        RODAK_CHECK_FALSE(policy.Observe(time, 60, true, true, 300000, kLoudSpeakerMic, onset));
    }
    time += 60;
    RODAK_CHECK(policy.Observe(time, 60, true, true, 300000, kLoudSpeakerMic, onset));
}

RODAK_TEST("playback epoch AcceptStop accepts epochless stop only before interruption") {
    VoicePlaybackEpochPolicy policy;
    RODAK_CHECK(policy.AcceptStart(3));
    RODAK_CHECK(policy.AcceptStop(0));
    RODAK_CHECK(policy.AcceptStop(3));
    RODAK_CHECK_FALSE(policy.AcceptStop(2));
    policy.Interrupt();
    RODAK_CHECK_FALSE(policy.AcceptStop(0));
    RODAK_CHECK_FALSE(policy.AcceptStop(3));
    RODAK_CHECK(policy.AcceptStart(4));
    RODAK_CHECK(policy.AcceptStop(0));
}

RODAK_TEST("barge in and vad end survive assembler clock pullback without losing evidence") {
    VoiceBargeInPolicy barge;
    uint32_t time = 1000, onset = 0;
    barge.StartPlayback(time);
    // 8 quiet frames reach 480 ms, then a 45 ms assembler pullback shrinks frame 9 delta to 15 ms.
    Frames(barge, time, false, 8, onset);
    time += 15;
    RODAK_CHECK_FALSE(barge.Observe(time, 60, true, false, 0, 0, onset));
    Frames(barge, time, false, 1, onset);
    RODAK_CHECK_FALSE(Frames(barge, time, true, 3, onset));
    time += 15;
    RODAK_CHECK_FALSE(barge.Observe(time, 60, true, true, kLoud, 0, onset));
    RODAK_CHECK(Frames(barge, time, true, 2, onset));

    VoiceVadEndPolicy vad_end;
    vad_end.Start(1000);
    uint32_t end_ts = 1000;
    for (int i = 0; i < 8; ++i) RODAK_CHECK_FALSE(vad_end.Observe(end_ts += 60, 60, true, false));
    RODAK_CHECK_FALSE(vad_end.Observe(end_ts += 15, 60, true, false));
    RODAK_CHECK(vad_end.Observe(end_ts += 60, 60, true, false));
}

RODAK_TEST("barge in onset advances to earliest passing frame inside confirmation window") {
    VoiceBargeInPolicy policy;
    uint32_t time = 1000, onset = 0;
    policy.StartPlayback(time);
    Arm(policy, time, onset);
    // Frame 1 passes, frames 2..5 fail, frames 6, 8, 10 pass -> confirms at frame 10 with onset at frame 6 start.
    RODAK_CHECK_FALSE(Frames(policy, time, true, 1, onset, true, kLoud * 2));
    RODAK_CHECK_FALSE(Frames(policy, time, true, 4, onset, true, kLoud * 64));
    const uint32_t frame6_start = time;
    RODAK_CHECK_FALSE(Frames(policy, time, true, 1, onset, true, kLoud * 2));
    RODAK_CHECK_FALSE(Frames(policy, time, true, 1, onset, true, kLoud * 64));
    RODAK_CHECK_FALSE(Frames(policy, time, true, 1, onset, true, kLoud * 2));
    RODAK_CHECK_FALSE(Frames(policy, time, true, 1, onset, true, kLoud * 64));
    RODAK_CHECK(Frames(policy, time, true, 1, onset, true, kLoud * 2));
    RODAK_CHECK_EQ(onset, frame6_start);
}
