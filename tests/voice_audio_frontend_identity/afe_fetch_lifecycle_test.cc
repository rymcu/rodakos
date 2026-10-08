#include "test_framework.h"
#include "host_runtime.h"
#include "afe_fetch_control.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#define private public
#include "phone_os/voice_audio_frontend.h"
#undef private

namespace {
using namespace std::chrono_literals;
namespace control = rodakos_test::afe_fetch;
namespace frontend_host = rodakos_test::voice_frontend;

struct Fixture {
    rodakos::AudioCodecInput input;
    rodakos::VoiceAudioFrontend frontend{input};
    Fixture() {
        frontend_host::Reset();
        control::BeginScript();
    }
    ~Fixture() {
        control::ForceReleaseAll();
        frontend.Deinit();
    }
};

template <typename T>
bool WaitUntil(T predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

uint32_t Generation(rodakos::VoiceAudioFrontend& frontend) {
    xSemaphoreTake(frontend.mutex_, portMAX_DELAY);
    const auto generation = frontend.conversation_generation_;
    xSemaphoreGive(frontend.mutex_);
    return generation;
}

struct Stopper {
    std::atomic<bool> returned{false};
    std::thread thread;
    explicit Stopper(rodakos::VoiceAudioFrontend& frontend)
        : thread([&frontend, this] { frontend.Stop(); returned = true; }) {}
    ~Stopper() {
        control::ForceReleaseAll();
        if (thread.joinable()) thread.join();
    }
    void Join() {
        control::ForceReleaseAll();
        if (thread.joinable()) thread.join();
    }
};

control::FetchReply Pcm(size_t samples, int16_t value) {
    return {ESP_OK, std::vector<int16_t>(samples, value), VAD_SPEECH};
}
}

RODAK_TEST("AFE cancelled failure is accounted separately from active fetch errors") {
    Fixture fixture;
    const auto pending = control::QueueFetch({});
    frontend_host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFetchEntered(pending));
    RODAK_CHECK(fixture.frontend.ArmAecDiagnosticCapture(1000));
    const auto generation = Generation(fixture.frontend);
    Stopper stopper(fixture.frontend);
    const bool generation_changed = WaitUntil([&] {
        return Generation(fixture.frontend) != generation;
    });
    control::ReleaseFetch(pending);
    stopper.Join();
    RODAK_CHECK(generation_changed);
    RODAK_CHECK(stopper.returned);
    // Old full TU fails this precise assertion: its post-cancel ESP_FAIL emits rejected.
    std::printf("AFE_CANCELLED_OBSERVED rejected_warnings=%zu generation_changed=%d\n",
                control::RejectedWarningCount(), generation_changed);
    RODAK_CHECK_EQ(control::RejectedWarningCount(), size_t{0});
    const auto summary = control::LastSummary();
    RODAK_CHECK(summary.observed);
    RODAK_CHECK_EQ(summary.current_failures, 0U);
    RODAK_CHECK(summary.cancelled_results >= 1U);
    const auto diagnostic = fixture.frontend.GetAecDiagnosticCaptureStatus();
    RODAK_CHECK_EQ(diagnostic.afe_discontinuities, 0U);
    RODAK_CHECK_EQ(diagnostic.afe_samples, size_t{0});
    RODAK_CHECK_EQ(control::DestroyDuringOperationCount(), size_t{0});
}

RODAK_TEST("AFE cancelled valid PCM never enters the next recording generation") {
    Fixture fixture;
    const auto pending = control::QueueFetch(Pcm(320, 17));
    frontend_host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFetchEntered(pending));
    RODAK_CHECK(fixture.frontend.ArmAecDiagnosticCapture(1000));
    const auto generation = Generation(fixture.frontend);
    Stopper stopper(fixture.frontend);
    const bool generation_changed = WaitUntil([&] {
        return Generation(fixture.frontend) != generation;
    });
    control::ReleaseFetch(pending);
    stopper.Join();
    RODAK_CHECK(generation_changed);
    rodakos::VoicePcmFrame frame;
    RODAK_CHECK_FALSE(fixture.frontend.PopFrame(frame));
    RODAK_CHECK(control::LastSummary().cancelled_results >= 1U);
    RODAK_CHECK_EQ(fixture.frontend.GetAecDiagnosticCaptureStatus().afe_samples, size_t{0});
    RODAK_CHECK_EQ(control::DestroyDuringOperationCount(), size_t{0});

    control::BeginScript();
    const auto fresh = control::QueueFetch(Pcm(320, 42));
    const auto sentinel = control::QueueFetch({});
    frontend_host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFetchEntered(fresh));
    control::ReleaseFetch(fresh);
    RODAK_CHECK(control::WaitFetchEntered(sentinel));
    RODAK_CHECK(fixture.frontend.PopFrame(frame));
    RODAK_CHECK(frame.samples == std::vector<int16_t>(320, 42));
    RODAK_CHECK(frame.vad_valid);
    RODAK_CHECK_FALSE(fixture.frontend.PopFrame(frame));
}

RODAK_TEST("AFE active fetch error preserves PCM while marking only the gap frame invalid") {
    Fixture fixture;
    const auto before = control::QueueFetch(Pcm(160, 17));
    const auto failure = control::QueueFetch({});
    const auto after = control::QueueFetch(Pcm(160, 23));
    const auto normal = control::QueueFetch(Pcm(320, 42));
    const auto sentinel = control::QueueFetch({});
    frontend_host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(fixture.frontend.ArmAecDiagnosticCapture(1000));
    for (const auto ticket : {before, failure, after, normal}) {
        RODAK_CHECK(control::WaitFetchEntered(ticket));
        control::ReleaseFetch(ticket);
    }
    RODAK_CHECK(control::WaitFetchEntered(sentinel));
    RODAK_CHECK_EQ(control::RejectedWarningCount(), size_t{1});
    const auto diagnostic = fixture.frontend.GetAecDiagnosticCaptureStatus();
    RODAK_CHECK_EQ(diagnostic.afe_discontinuities, 1U);
    RODAK_CHECK_EQ(diagnostic.afe_samples, size_t{640});
    rodakos::VoicePcmFrame frame;
    RODAK_CHECK(fixture.frontend.PopFrame(frame));
    std::vector<int16_t> expected(160, 17);
    expected.insert(expected.end(), 160, 23);
    RODAK_CHECK(frame.samples == expected);
    std::printf("AFE_GAP_OBSERVED samples=%zu vad_valid=%d current_errors=%zu\n",
                frame.samples.size(), frame.vad_valid, control::RejectedWarningCount());
    RODAK_CHECK_FALSE(frame.vad_valid);
    RODAK_CHECK(fixture.frontend.PopFrame(frame));
    RODAK_CHECK(frame.samples == std::vector<int16_t>(320, 42));
    RODAK_CHECK(frame.vad_valid);
    RODAK_CHECK_FALSE(fixture.frontend.PopFrame(frame));
    const auto generation = Generation(fixture.frontend);
    Stopper stopper(fixture.frontend);
    const bool generation_changed = WaitUntil([&] {
        return Generation(fixture.frontend) != generation;
    });
    stopper.Join();
    RODAK_CHECK(generation_changed);
    RODAK_CHECK_EQ(control::LastSummary().current_failures, 1U);
    RODAK_CHECK_EQ(control::DestroyDuringOperationCount(), size_t{0});
}

RODAK_TEST("AFE cancellation keeps fetching until the blocked feed lease can drain") {
    Fixture fixture;
    const auto first = control::QueueFetch({});
    const auto draining = control::QueueFetch({});
    control::BlockFeedUntilFetch(2, 2);
    frontend_host::SupplyAudioReads(2);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFetchEntered(first));
    RODAK_CHECK(control::WaitFeedBlocked(2));
    const auto generation = Generation(fixture.frontend);
    Stopper stopper(fixture.frontend);
    const bool generation_changed = WaitUntil([&] {
        return Generation(fixture.frontend) != generation;
    });
    control::ReleaseFetch(first);
    const bool fetched_to_drain = control::WaitFetchEntered(draining);
    // Always release both fake directions before asserting, including the early-break mutant.
    stopper.Join();
    std::printf("AFE_DRAIN_OBSERVED next_fetch=%d stop_returned=%d\n",
                fetched_to_drain, stopper.returned.load());
    RODAK_CHECK(generation_changed);
    RODAK_CHECK(fetched_to_drain);
    RODAK_CHECK(stopper.returned);
    RODAK_CHECK_EQ(control::RejectedWarningCount(), size_t{0});
    RODAK_CHECK(control::LastSummary().cancelled_results >= 1U);
    RODAK_CHECK_EQ(control::DestroyDuringOperationCount(), size_t{0});
}

RODAK_TEST("AFE diagnostic input EOF keeps zero feed and active errors remain observable until recovery") {
    Fixture fixture;
    RODAK_CHECK(fixture.frontend.StartListening([](const std::string&) {}));
    RODAK_CHECK(fixture.frontend.LoadDiagnosticAudio("audio_begin 256"));
    std::string chunk = "audio_chunk 0 ";
    for (size_t i = 0; i < 256; ++i) chunk += "0b00";
    RODAK_CHECK(fixture.frontend.LoadDiagnosticAudio(chunk));
    RODAK_CHECK(fixture.frontend.ArmDiagnosticAudio());
    const auto failure = control::QueueFetch({});
    const auto recovered = control::QueueFetch(Pcm(320, 29));
    const auto sentinel = control::QueueFetch({});
    RODAK_CHECK(fixture.frontend.Start({}));
    frontend_host::SupplyAudioReads(2);
    RODAK_CHECK(control::WaitFetchEntered(failure));
    const bool fed = WaitUntil([] { return control::FedMicrophoneSamples().size() == 640; });
    control::ReleaseFetch(failure);
    RODAK_CHECK(control::WaitFetchEntered(recovered));
    RODAK_CHECK_EQ(control::RejectedWarningCount(), size_t{1});
    RODAK_CHECK(fixture.frontend.IsRunning());
    control::ReleaseFetch(recovered);
    RODAK_CHECK(control::WaitFetchEntered(sentinel));
    rodakos::VoicePcmFrame frame;
    RODAK_CHECK(fixture.frontend.PopFrame(frame));
    RODAK_CHECK(frame.samples == std::vector<int16_t>(320, 29));
    RODAK_CHECK_FALSE(frame.vad_valid);
    RODAK_CHECK(fed);
    auto expected = std::vector<int16_t>(256, 11);
    expected.resize(640, 0);
    RODAK_CHECK(control::FedMicrophoneSamples() == expected);
    const auto generation = Generation(fixture.frontend);
    Stopper stopper(fixture.frontend);
    const bool generation_changed = WaitUntil([&] {
        return Generation(fixture.frontend) != generation;
    });
    stopper.Join();
    RODAK_CHECK(generation_changed);
    RODAK_CHECK_EQ(control::LastSummary().current_failures, 1U);
    RODAK_CHECK_EQ(control::DestroyDuringOperationCount(), size_t{0});
}
