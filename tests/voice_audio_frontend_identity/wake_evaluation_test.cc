#include "test_framework.h"
#include "host_runtime.h"

#define private public
#include "phone_os/voice_audio_frontend.h"
#undef private

#include <cstdint>
#include <string>
#include <vector>

namespace {
using rodakos::AudioCodecInput;
using rodakos::VoiceAudioFrontend;
using namespace rodakos_test::voice_frontend;

constexpr int kChunk = 320;

struct Fixture {
    AudioCodecInput input;
    VoiceAudioFrontend frontend{input};
    bool woke = false;
    Fixture() {
        Reset();
        SetModelChunkSamples(kChunk);
    }
    bool Listen() {
        return frontend.StartListening([this](const std::string&) { woke = true; });
    }
    bool Load(size_t samples, int16_t value) {
        if (!frontend.LoadDiagnosticAudio("audio_begin " + std::to_string(samples))) return false;
        const uint16_t raw = static_cast<uint16_t>(value);
        char word[5];
        std::snprintf(word, sizeof(word), "%02x%02x", raw & 0xff, raw >> 8);
        for (size_t offset = 0; offset < samples; offset += 64) {
            const size_t count = std::min<size_t>(64, samples - offset);
            std::string hex;
            for (size_t i = 0; i < count; ++i) hex += word;
            if (!frontend.LoadDiagnosticAudio("audio_chunk " + std::to_string(offset) + " " + hex)) {
                return false;
            }
        }
        return true;
    }
    void Feed(size_t chunks, int16_t value = 1000) {
        for (size_t i = 0; i < chunks; ++i) {
            std::vector<int16_t> samples(kChunk, value);
            frontend.ProcessWakeSamples(samples, frontend.wake_generation_);
        }
    }
};

// kChunk samples at 16 kHz.
constexpr size_t kChunkMs = kChunk / 16;
}  // namespace

RODAK_TEST("Wake window restarts during silence so a phrase never straddles the timeout") {
    Fixture fixture;
    RODAK_CHECK(fixture.Listen());
    const size_t armed = CleanCount();
    fixture.Feed(1000 / kChunkMs - 1, 0);
    RODAK_CHECK_EQ(CleanCount(), armed);
    fixture.Feed(1, 0);
    RODAK_CHECK_EQ(CleanCount(), armed + 1);
    RODAK_CHECK_EQ(fixture.frontend.wake_realignments_, 1U);
}

RODAK_TEST("Wake window is not restarted inside a phrase or its short pauses") {
    Fixture fixture;
    RODAK_CHECK(fixture.Listen());
    fixture.Feed(200 / kChunkMs, 0);
    const size_t before = CleanCount();
    // 1.2 s of speech with a 200 ms pause in the middle, as between 你好 and 达克.
    fixture.Feed(600 / kChunkMs, 3000);
    fixture.Feed(200 / kChunkMs, 0);
    fixture.Feed(600 / kChunkMs, 3000);
    RODAK_CHECK_EQ(CleanCount(), before);
    fixture.Feed(400 / kChunkMs - 1, 0);
    RODAK_CHECK_EQ(CleanCount(), before);
    fixture.Feed(1, 0);
    RODAK_CHECK_EQ(CleanCount(), before + 1);
}

RODAK_TEST("Wake noise floor rises to quiet steady noise and keeps realigning") {
    Fixture fixture;
    RODAK_CHECK(fixture.Listen());
    // A near-silent start pins the floor at its minimum.
    fixture.Feed(2, 0);
    // About -62 dBFS of steady noise: below the integer step that used to truncate to zero.
    fixture.Feed(12000 / kChunkMs, 25);
    const uint32_t before = fixture.frontend.wake_realignments_;
    fixture.Feed(3000 / kChunkMs, 25);
    RODAK_CHECK(fixture.frontend.wake_realignments_ > before);
}

RODAK_TEST("Wake window timeout still restarts the recognizer") {
    Fixture fixture;
    RODAK_CHECK(fixture.Listen());
    const size_t before = CleanCount();
    SetDetectionState(ESP_MN_STATE_TIMEOUT);
    fixture.Feed(1, 3000);
    RODAK_CHECK_EQ(CleanCount(), before + 1);
    RODAK_CHECK_EQ(fixture.frontend.wake_window_samples_, 0U);
}

RODAK_TEST("Wake evaluation replaces microphone input after the lead and never opens a session") {
    Fixture fixture;
    RODAK_CHECK(fixture.Listen());
    RODAK_CHECK(fixture.Load(kChunk, 1234));
    RODAK_CHECK(fixture.frontend.StartWakeEvaluation("250 20"));
    RODAK_CHECK(fixture.frontend.wake_evaluation_active_);
    RODAK_CHECK(DetectionThreshold() > 0.249F && DetectionThreshold() < 0.251F);

    fixture.Feed(1);
    auto fed = LastDetectSamples();
    RODAK_CHECK_EQ(fed.size(), static_cast<size_t>(kChunk));
    for (const auto sample : fed) RODAK_CHECK(sample >= -8 && sample <= 7);

    fixture.Feed(1);
    fed = LastDetectSamples();
    RODAK_CHECK_EQ(fed.front(), 1234);
    RODAK_CHECK_EQ(fed.back(), 1234);

    SetDetection(true);
    SetDetectionProbability(0.42F);
    fixture.Feed(1);
    RODAK_CHECK_EQ(fixture.frontend.wake_evaluation_detections_, 1U);
    RODAK_CHECK_FALSE(fixture.woke);
    RODAK_CHECK_EQ(fixture.frontend.mode_, VoiceAudioFrontend::Mode::kWakeOnly);
    RODAK_CHECK_FALSE(fixture.frontend.wake_notification_pending_);
    RODAK_CHECK(static_cast<bool>(fixture.frontend.on_wake_word_));
}

RODAK_TEST("Wake evaluation restores threshold and stops after the tail") {
    Fixture fixture;
    RODAK_CHECK(fixture.Listen());
    RODAK_CHECK(fixture.Load(kChunk, 500));
    RODAK_CHECK(fixture.frontend.StartWakeEvaluation("300 0"));
    SetDetectionState(ESP_MN_STATE_TIMEOUT);
    const size_t cleans = CleanCount();
    // One chunk of audio, then 1500 ms of tail at 16 kHz.
    fixture.Feed(1 + (1500 * 16 + kChunk - 1) / kChunk);
    RODAK_CHECK_FALSE(fixture.frontend.wake_evaluation_active_);
    RODAK_CHECK_FALSE(fixture.frontend.diagnostic_audio_active_);
    RODAK_CHECK(fixture.frontend.wake_evaluation_timeouts_ > 0U);
    RODAK_CHECK(CleanCount() > cleans);
    RODAK_CHECK(DetectionThreshold() > 0.139F && DetectionThreshold() < 0.141F);
    RODAK_CHECK_FALSE(fixture.woke);

    // Normal detection resumes: the wake leaves listening mode and is queued for dispatch.
    SetDetectionState(ESP_MN_STATE_DETECTING);
    SetDetection(true);
    fixture.Feed(1);
    RODAK_CHECK_EQ(fixture.frontend.mode_, VoiceAudioFrontend::Mode::kIdle);
}

RODAK_TEST("Wake evaluation command override is restored when listening stops") {
    Fixture fixture;
    RODAK_CHECK(fixture.Listen());
    RODAK_CHECK_EQ(RegisteredCommand(), "ni hao da ke");
    RODAK_CHECK(fixture.Load(kChunk, 1));
    RODAK_CHECK(fixture.frontend.StartWakeEvaluation("140 0 hai da ke"));
    RODAK_CHECK_EQ(RegisteredCommand(), "hai da ke");
    fixture.frontend.StopListening();
    RODAK_CHECK_FALSE(fixture.frontend.wake_evaluation_active_);
    RODAK_CHECK_EQ(RegisteredCommand(), "ni hao da ke");
    RODAK_CHECK(fixture.frontend.wake_model_ready_);
}

RODAK_TEST("A chunk built before an evaluation started is not fed to its window") {
    Fixture fixture;
    RODAK_CHECK(fixture.Listen());
    RODAK_CHECK(fixture.Load(kChunk, 1234));
    const uint32_t stale_epoch = fixture.frontend.wake_evaluation_epoch_;
    RODAK_CHECK(fixture.frontend.StartWakeEvaluation("140 0"));
    std::vector<int16_t> stale(kChunk, 999);
    fixture.frontend.ProcessWakeSamples(stale, fixture.frontend.wake_generation_, stale_epoch, false);
    RODAK_CHECK_EQ(fixture.frontend.wake_evaluation_fed_samples_, 0U);
    fixture.Feed(1);
    RODAK_CHECK_EQ(LastDetectSamples().front(), 1234);
}

RODAK_TEST("Wake evaluation rejects bad arguments and incomplete or busy state") {
    Fixture fixture;
    RODAK_CHECK(fixture.Listen());
    RODAK_CHECK_FALSE(fixture.frontend.StartWakeEvaluation("140 0"));
    RODAK_CHECK(fixture.frontend.LoadDiagnosticAudio("audio_begin 10"));
    RODAK_CHECK_FALSE(fixture.frontend.StartWakeEvaluation("140 0"));
    RODAK_CHECK(fixture.Load(kChunk, 1));
    for (const char* bad : {"", "140", "0 0", "1000 0", "140 3001", "x 0", "140 -1",
                            "~25~140 0", "~31~140 0", "~-31~140 0", "~6140 0", "+140 0"}) {
        RODAK_CHECK_FALSE(fixture.frontend.StartWakeEvaluation(bad));
    }
    RODAK_CHECK_FALSE(fixture.frontend.StartWakeEvaluation("140 0 " + std::string(129, 'a')));
    RODAK_CHECK(fixture.frontend.StartWakeEvaluation("140 0"));
    RODAK_CHECK_FALSE(fixture.frontend.StartWakeEvaluation("140 0"));
    RODAK_CHECK_FALSE(fixture.frontend.ArmDiagnosticAudio());
    RODAK_CHECK_FALSE(fixture.frontend.LoadDiagnosticAudio("audio_clear"));
}

RODAK_TEST("Failed command restore fences the model until the next listen rebuilds it") {
    Fixture fixture;
    RODAK_CHECK(fixture.Listen());
    RODAK_CHECK(fixture.Load(kChunk, 1));
    RODAK_CHECK(fixture.frontend.StartWakeEvaluation("140 0 hai da ke"));
    SetCommandUpdateResult(false);
    fixture.frontend.ClearDiagnosticAudio();
    RODAK_CHECK_FALSE(fixture.frontend.wake_evaluation_active_);
    RODAK_CHECK_FALSE(fixture.frontend.wake_model_ready_);
    RODAK_CHECK_EQ(fixture.frontend.multinet_data_, nullptr);
    RODAK_CHECK_EQ(fixture.frontend.mode_, VoiceAudioFrontend::Mode::kIdle);
    SetCommandUpdateResult(true);
    RODAK_CHECK(fixture.Listen());
    RODAK_CHECK_EQ(RegisteredCommand(), "ni hao da ke");
}

RODAK_TEST("Re-arming during an evaluation whose restore fails rebuilds the model first") {
    Fixture fixture;
    RODAK_CHECK(fixture.Listen());
    RODAK_CHECK(fixture.Load(kChunk, 1));
    RODAK_CHECK(fixture.frontend.StartWakeEvaluation("140 0 hai da ke"));
    SetCommandUpdateResult(false);
    // StartListening finishes the evaluation; the failed restore releases the model.
    const bool listening = fixture.Listen();
    SetCommandUpdateResult(true);
    if (listening) {
        RODAK_CHECK(fixture.frontend.wake_model_ready_);
        RODAK_CHECK_NE(fixture.frontend.multinet_data_, nullptr);
        RODAK_CHECK(fixture.frontend.wake_chunk_samples_ > 0U);
    } else {
        RODAK_CHECK_FALSE(fixture.frontend.IsListening());
    }
    RODAK_CHECK(fixture.Listen());
    RODAK_CHECK(fixture.frontend.wake_model_ready_);
    RODAK_CHECK_EQ(RegisteredCommand(), "ni hao da ke");
}
