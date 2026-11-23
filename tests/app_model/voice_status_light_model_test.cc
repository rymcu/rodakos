#include "test_framework.h"

#include "phone_os/voice_status_light_model.h"

namespace {

using rodakos::VoiceAssistantPhase;
using rodakos::VoiceStatusLightFrame;
using rodakos::VoiceStatusLightModel;
using rodakos::VoiceStatusLightSnapshot;

VoiceStatusLightSnapshot Phase(VoiceAssistantPhase phase, uint32_t starts = 0) {
    VoiceStatusLightSnapshot snapshot;
    snapshot.phase = phase;
    snapshot.interaction_starts = starts;
    return snapshot;
}

bool Is(const VoiceStatusLightFrame& frame, uint8_t red, uint8_t green, uint8_t blue) {
    return frame.active && frame.red == red && frame.green == green && frame.blue == blue;
}

}  // namespace

RODAK_TEST("status light stays off while idle and ignores counters set before it started") {
    VoiceStatusLightModel model;
    auto snapshot = Phase(VoiceAssistantPhase::kIdle, 7);
    snapshot.barge_ins = 3;
    RODAK_CHECK_FALSE(model.Update(snapshot, 1000).active);
    RODAK_CHECK_FALSE(model.Update(snapshot, 2000).active);
}

RODAK_TEST("status light flashes white on wake, then blinks blue while connecting") {
    VoiceStatusLightModel model;
    model.Update(Phase(VoiceAssistantPhase::kIdle, 0), 0);
    const auto start = Phase(VoiceAssistantPhase::kConnecting, 1);
    RODAK_CHECK(Is(model.Update(start, 1000), 255, 255, 255));
    RODAK_CHECK(Is(model.Update(start, 1000 + VoiceStatusLightModel::kWakeFlashMs - 1), 255, 255, 255));

    // 2 Hz blink: on for the first half of each 500 ms period, level 0 for the second half.
    const auto on = model.Update(start, 2000);
    RODAK_CHECK(Is(on, 0, 90, 255));
    RODAK_CHECK_EQ(on.level, 100);
    const auto off = model.Update(start, 2300);
    RODAK_CHECK(off.active);
    RODAK_CHECK_EQ(off.level, 0);
}

RODAK_TEST("status light shows amber while reconnecting") {
    VoiceStatusLightModel model;
    auto snapshot = Phase(VoiceAssistantPhase::kConnecting);
    snapshot.reconnecting = true;
    model.Update(snapshot, 0);
    RODAK_CHECK(Is(model.Update(snapshot, 1000), 255, 140, 0));
}

RODAK_TEST("status light listening is dim cyan and brighter while the user speaks") {
    VoiceStatusLightModel model;
    auto snapshot = Phase(VoiceAssistantPhase::kListening);
    const auto quiet = model.Update(snapshot, 0);
    RODAK_CHECK(Is(quiet, 0, 200, 200));
    snapshot.user_speech = true;
    const auto speech = model.Update(snapshot, 100);
    RODAK_CHECK(speech.level > quiet.level);
    RODAK_CHECK_EQ(speech.level, 100);
}

RODAK_TEST("status light fades the last five seconds of the follow-up window") {
    VoiceStatusLightModel model;
    auto snapshot = Phase(VoiceAssistantPhase::kListening);
    snapshot.follow_up_remaining_ms = 20000;
    const uint8_t full = model.Update(snapshot, 0).level;
    snapshot.follow_up_remaining_ms = 2500;
    RODAK_CHECK_EQ(model.Update(snapshot, 100).level, full / 2);
    snapshot.follow_up_remaining_ms = 0;
    RODAK_CHECK_EQ(model.Update(snapshot, 200).level, 0);
}

RODAK_TEST("status light breathes purple while waiting for a reply") {
    VoiceStatusLightModel model;
    auto snapshot = Phase(VoiceAssistantPhase::kListening);
    snapshot.awaiting_reply = true;
    const auto low = model.Update(snapshot, 0);
    const auto high = model.Update(snapshot, VoiceStatusLightModel::kBreathPeriodMs / 2);
    RODAK_CHECK(Is(low, 160, 0, 255));
    RODAK_CHECK(high.level > low.level);
    RODAK_CHECK_EQ(high.level, 100);
}

RODAK_TEST("status light speaking follows the playback level") {
    VoiceStatusLightModel model;
    auto snapshot = Phase(VoiceAssistantPhase::kSpeaking);
    snapshot.playback_level = 0;
    const auto quiet = model.Update(snapshot, 0);
    snapshot.playback_level = 255;
    const auto loud = model.Update(snapshot, 33);
    RODAK_CHECK(Is(quiet, 0, 220, 60));
    RODAK_CHECK(quiet.level > 0);
    RODAK_CHECK_EQ(loud.level, 100);
}

RODAK_TEST("status light flashes white on barge-in") {
    VoiceStatusLightModel model;
    auto snapshot = Phase(VoiceAssistantPhase::kSpeaking);
    model.Update(snapshot, 0);
    snapshot.phase = VoiceAssistantPhase::kListening;
    snapshot.barge_ins = 1;
    RODAK_CHECK(Is(model.Update(snapshot, 100), 255, 255, 255));
    RODAK_CHECK(Is(model.Update(snapshot, 100 + VoiceStatusLightModel::kBargeInFlashMs), 0, 200, 200));
}

RODAK_TEST("status light blinks red three times on error and then turns off") {
    VoiceStatusLightModel model;
    model.Update(Phase(VoiceAssistantPhase::kListening), 0);
    const auto error = Phase(VoiceAssistantPhase::kError);
    const auto first = model.Update(error, 1000);
    RODAK_CHECK(Is(first, 255, 0, 0));
    RODAK_CHECK_EQ(first.level, 100);
    RODAK_CHECK_EQ(model.Update(error, 1000 + VoiceStatusLightModel::kErrorBlinkMs).level, 0);
    const int64_t end = 1000 + VoiceStatusLightModel::kErrorBlinkMs * 2 * VoiceStatusLightModel::kErrorBlinks;
    RODAK_CHECK(model.Update(error, end - 1).active);
    RODAK_CHECK_FALSE(model.Update(error, end).active);
}

RODAK_TEST("status light fades out after a session ends and then gives the light back") {
    VoiceStatusLightModel model;
    auto speaking = Phase(VoiceAssistantPhase::kSpeaking);
    speaking.playback_level = 255;
    model.Update(speaking, 0);
    const auto idle = Phase(VoiceAssistantPhase::kIdle);
    const auto start = model.Update(idle, 1000);
    RODAK_CHECK(Is(start, 0, 220, 60));
    RODAK_CHECK_EQ(start.level, 100);
    const auto half = model.Update(idle, 1000 + VoiceStatusLightModel::kEndFadeMs / 2);
    RODAK_CHECK_EQ(half.level, 50);
    RODAK_CHECK_FALSE(model.Update(idle, 1000 + VoiceStatusLightModel::kEndFadeMs).active);
}

RODAK_TEST("status light wake flash cancels a running end fade") {
    VoiceStatusLightModel model;
    model.Update(Phase(VoiceAssistantPhase::kListening, 1), 0);
    model.Update(Phase(VoiceAssistantPhase::kIdle, 1), 1000);
    RODAK_CHECK(Is(model.Update(Phase(VoiceAssistantPhase::kConnecting, 2), 1100), 255, 255, 255));
}

RODAK_TEST("status light maps reply RMS to a bounded level") {
    RODAK_CHECK_EQ(VoiceStatusLightModel::LevelFromRmsDbfs(-96.0f), 0);
    RODAK_CHECK_EQ(VoiceStatusLightModel::LevelFromRmsDbfs(-50.0f), 0);
    RODAK_CHECK_EQ(VoiceStatusLightModel::LevelFromRmsDbfs(-30.0f), 127);
    RODAK_CHECK_EQ(VoiceStatusLightModel::LevelFromRmsDbfs(-10.0f), 255);
    RODAK_CHECK_EQ(VoiceStatusLightModel::LevelFromRmsDbfs(0.0f), 255);
}
