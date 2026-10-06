#include "test_framework.h"
#include "host_runtime.h"

#define private public
#include "phone_os/voice_audio_frontend.h"
#undef private

#include <cstdint>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {
using rodakos::AudioCodecInput;
using rodakos::VoiceAudioFrontend;

rodakos::VoiceIdentityConfig Identity(const char* command) {
    auto config = rodakos::DefaultVoiceIdentityConfig();
    config.name = "测试身份";
    config.wake_word = "你好测试";
    config.wake_command = command;
    config.revision = 2;
    return config;
}

struct Fixture {
    AudioCodecInput input;
    VoiceAudioFrontend frontend{input};
    Fixture() { rodakos_test::voice_frontend::Reset(); }
    ~Fixture() = default;
};
}

RODAK_TEST("Configure failure fences the old graph from Start and ProcessWake") {
    Fixture fixture;
    RODAK_CHECK(fixture.frontend.Init());
    rodakos_test::voice_frontend::SetCommandUpdateResult(false);
    RODAK_CHECK_FALSE(fixture.frontend.ConfigureWakeWord(Identity("new command")));
    RODAK_CHECK_FALSE(fixture.frontend.wake_model_ready_);
    RODAK_CHECK_EQ(fixture.frontend.multinet_data_, nullptr);

    rodakos_test::voice_frontend::SetDetection(true);
    fixture.frontend.mode_ = VoiceAudioFrontend::Mode::kWakeOnly;
    std::vector<int16_t> samples(320, 0);
    fixture.frontend.ProcessWakeSamples(samples, fixture.frontend.wake_generation_);
    RODAK_CHECK_FALSE(rodakos_test::voice_frontend::LastDetectionRan());
    RODAK_CHECK_FALSE(fixture.frontend.StartListening([](const std::string&) {}));
}

RODAK_TEST("Every MultiNet command update failure point fences the old graph") {
    for (const auto failure : {0, 1, 2}) {
        Fixture fixture;
        RODAK_CHECK(fixture.frontend.Init());
        if (failure == 0) rodakos_test::voice_frontend::SetCommandClearResult(false);
        if (failure == 1) rodakos_test::voice_frontend::SetCommandAddResult(false);
        if (failure == 2) rodakos_test::voice_frontend::SetCommandUpdateResult(false);
        RODAK_CHECK_FALSE(fixture.frontend.ConfigureWakeWord(Identity("failure point")));
        RODAK_CHECK_FALSE(fixture.frontend.wake_model_ready_);
        RODAK_CHECK_EQ(fixture.frontend.multinet_data_, nullptr);
    }
}

RODAK_TEST("Successful reconfigure rebuilds the fenced graph before listening") {
    Fixture fixture;
    RODAK_CHECK(fixture.frontend.Init());
    const size_t initial_create = rodakos_test::voice_frontend::ModelCreateCount();
    rodakos_test::voice_frontend::SetCommandUpdateResult(false);
    RODAK_CHECK_FALSE(fixture.frontend.ConfigureWakeWord(Identity("broken command")));
    rodakos_test::voice_frontend::SetCommandUpdateResult(true);
    RODAK_CHECK(fixture.frontend.ConfigureWakeWord(Identity("restored command")));
    RODAK_CHECK_FALSE(fixture.frontend.wake_model_ready_);
    RODAK_CHECK(fixture.frontend.StartListening([](const std::string&) {}));
    RODAK_CHECK(fixture.frontend.wake_model_ready_);
    RODAK_CHECK(rodakos_test::voice_frontend::ModelCreateCount() > initial_create);
    RODAK_CHECK_EQ(rodakos_test::voice_frontend::RegisteredCommand(), "restored command");

    rodakos_test::voice_frontend::SetDetection(true);
    std::vector<int16_t> samples(320, 0);
    fixture.frontend.ProcessWakeSamples(samples, fixture.frontend.wake_generation_);
    RODAK_CHECK(rodakos_test::voice_frontend::LastDetectionRan());
}

RODAK_TEST("Model initialization failure keeps Start and detection fenced") {
    Fixture fixture;
    rodakos_test::voice_frontend::SetModelCreateResult(false);
    RODAK_CHECK_FALSE(fixture.frontend.Init());
    RODAK_CHECK_FALSE(fixture.frontend.wake_model_ready_);
    rodakos_test::voice_frontend::SetDetection(true);
    std::vector<int16_t> samples(320, 0);
    fixture.frontend.mode_ = VoiceAudioFrontend::Mode::kWakeOnly;
    fixture.frontend.ProcessWakeSamples(samples, fixture.frontend.wake_generation_);
    RODAK_CHECK_FALSE(rodakos_test::voice_frontend::LastDetectionRan());
    RODAK_CHECK_FALSE(fixture.frontend.StartListening([](const std::string&) {}));
}

RODAK_TEST("Wake reconfigure failure does not release the conversation model catalog or AFE") {
    Fixture fixture;
    rodakos::VoiceRecorderConfig config;
    RODAK_CHECK(fixture.frontend.Start(config));
    const auto* models = fixture.frontend.models_;
    const auto* afe = fixture.frontend.afe_data_;
    RODAK_CHECK_NE(models, nullptr);
    RODAK_CHECK_NE(afe, nullptr);
    RODAK_CHECK(fixture.frontend.IsRunning());

    rodakos_test::voice_frontend::SetCommandUpdateResult(false);
    RODAK_CHECK_FALSE(fixture.frontend.ConfigureWakeWord(Identity("conversation failure")));
    RODAK_CHECK_EQ(fixture.frontend.mode_, VoiceAudioFrontend::Mode::kConversation);
    RODAK_CHECK_EQ(fixture.frontend.models_, models);
    RODAK_CHECK_EQ(fixture.frontend.afe_data_, afe);
    RODAK_CHECK(fixture.frontend.IsRunning());
    RODAK_CHECK_FALSE(fixture.frontend.wake_model_ready_);
}

RODAK_TEST("LastErrorSnapshot copies frontend errors while Configure updates them") {
    Fixture fixture;
    RODAK_CHECK(fixture.frontend.Init());
    auto invalid_a = Identity("snapshot failure");
    invalid_a.name.clear();
    auto invalid_b = Identity("snapshot failure");
    invalid_b.revision = 0;
    RODAK_CHECK_FALSE(fixture.frontend.ConfigureWakeWord(invalid_a));
    const std::string error_a = "voice identity field length is invalid";
    const std::string error_b = "voice identity revision must be positive";
    std::atomic<bool> running{true};
    std::atomic<size_t> reads{0};
    std::atomic<bool> invalid_snapshot{false};
    std::thread reader([&] {
        while (running.load()) {
            const auto snapshot = fixture.frontend.LastErrorSnapshot();
            if (snapshot != error_a && snapshot != error_b) invalid_snapshot = true;
            ++reads;
        }
    });
    for (int i = 0; i < 100; ++i) {
        RODAK_CHECK_FALSE(fixture.frontend.ConfigureWakeWord(i % 2 == 0 ? invalid_b : invalid_a));
    }
    running = false;
    reader.join();
    RODAK_CHECK(reads.load() > 0);
    RODAK_CHECK_FALSE(invalid_snapshot.load());
    RODAK_CHECK(
        fixture.frontend.LastErrorSnapshot() == error_a ||
        fixture.frontend.LastErrorSnapshot() == error_b);
}
