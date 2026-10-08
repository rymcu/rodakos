#include "test_framework.h"
#include "host_runtime.h"
#include "afe_fetch_control.h"
#include "observation_control.h"
#include "phone_os/voice_audio_frontend.h"

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <thread>

namespace {
using namespace std::chrono_literals;
namespace control = rodakos_test::afe_fetch;
namespace host = rodakos_test::voice_frontend;
namespace observe = rodakos_test::afe_observation;
struct Fixture {
    rodakos::AudioCodecInput input;
    rodakos::VoiceAudioFrontend frontend{input};
    explicit Fixture(bool block_first = false) {
        host::Reset();
        control::BeginStreaming(block_first);
    }
    ~Fixture() {
        observe::ReleaseAll();
        control::ForceReleaseAll();
        host::ReleaseAllAudioReads();
        frontend.Deinit();
    }
};
template<class T> bool WaitUntil(T predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}
void Stop(rodakos::VoiceAudioFrontend& frontend) {
    control::ForceReleaseAll();
    frontend.Stop();
}
}

RODAK_TEST("AFE compiled device AEC format accepts its exact SDK shape and rejects drift") {
#if CONFIG_USE_DEVICE_AEC
    constexpr int feed = 256;
#else
    constexpr int feed = 160;
#endif
    {
        Fixture fixture;
        host::SupplyAudioReads(4);
        RODAK_CHECK(fixture.frontend.Start({}));
        RODAK_CHECK(control::WaitFlowSamples(512));
        RODAK_CHECK_EQ(control::FlowSnapshot().partial_lost, size_t{0});
        RODAK_CHECK(fixture.frontend.IsRunning());
        std::printf("AFE_FORMAT_OBSERVED aec=%d feed=%d fetch=512 channels=1 sample_rate=16000\n",
                    CONFIG_USE_DEVICE_AEC, feed);
    }
    const int invalid[][4] = {{0, 512, 1, 16000}, {512, 512, 1, 16000},
        {feed, 511, 1, 16000}, {feed, 513, 1, 16000},
        {feed, 512, 2, 16000}, {feed, 512, 1, 48000}};
    for (const auto& format : invalid) {
        Fixture fixture;
        control::SetFormat(format[0], format[1], format[2], format[3]);
        RODAK_CHECK_FALSE(fixture.frontend.Start({}));
        RODAK_CHECK_FALSE(fixture.frontend.IsRunning());
        RODAK_CHECK_EQ(control::FlowSnapshot().feeds, size_t{0});
    }
}

RODAK_TEST("AFE startup does not fetch before the first producer completes") {
    Fixture fixture(true);
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFeedBlocked(1));
    RODAK_CHECK(WaitUntil([] {
        return control::FlowSnapshot().stalls != 0 || control::RejectedWarningCount() != 0;
    }));
    const auto flow = control::FlowSnapshot();
    RODAK_CHECK_EQ(flow.returns, size_t{0});
    RODAK_CHECK_EQ(flow.fetches, size_t{0});
    RODAK_CHECK_EQ(flow.stalls, size_t{1});
    RODAK_CHECK(fixture.frontend.IsRunning());
    control::ReleaseFirstFeed();
    RODAK_CHECK(control::WaitFeedReturns(1));
}

RODAK_TEST("AFE complete output credit preserves partial startup PCM across an input stall") {
    Fixture fixture;
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFeedReturns(1));
    RODAK_CHECK(WaitUntil([] {
        auto state = control::FlowSnapshot();
        return state.stalls != 0 || state.partial_lost != 0;
    }));
    auto state = control::FlowSnapshot();
    std::printf("AFE_STARTUP_OBSERVED lost_samples=%zu fetches=%zu queued=%zu stalls=%zu\n",
                state.partial_lost, state.fetches, state.queued_samples, state.stalls);
    RODAK_CHECK_EQ(state.partial_lost, size_t{0});
    RODAK_CHECK_EQ(state.fetches, size_t{0});
    RODAK_CHECK_EQ(state.queued_samples, size_t{160});
    RODAK_CHECK_EQ(state.stalls, size_t{1});
    host::SupplyAudioReads(2);
    RODAK_CHECK(control::WaitFlowSamples(512));
    state = control::FlowSnapshot();
    RODAK_CHECK_EQ(state.partial_lost, size_t{0});
    RODAK_CHECK_EQ(state.queued_samples, size_t{128});
    rodakos::VoicePcmFrame frame;
    RODAK_CHECK(WaitUntil([&] { return fixture.frontend.PopFrame(frame); }));
    RODAK_CHECK(frame.samples == std::vector<int16_t>(320, 250));
    RODAK_CHECK_FALSE(frame.vad_valid);
    RODAK_CHECK(fixture.frontend.IsRunning());
    Stop(fixture.frontend);
    RODAK_CHECK_EQ(control::LastSummary().current_failures, 0U);
}

RODAK_TEST("AFE one partial fetch failure recovers using conservative output credit") {
    Fixture fixture;
    control::QueueStreamingFailure(160);
    host::SupplyAudioReads(5);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFlowSamples(512));
    RODAK_CHECK_EQ(control::RejectedWarningCount(), size_t{1});
    const auto state = control::FlowSnapshot();
    RODAK_CHECK_EQ(state.partial_lost, size_t{160});
    RODAK_CHECK_EQ(state.resets, size_t{0});
    rodakos::VoicePcmFrame frame;
    RODAK_CHECK(WaitUntil([&] { return fixture.frontend.PopFrame(frame); }));
    RODAK_CHECK_FALSE(frame.vad_valid);
    RODAK_CHECK(fixture.frontend.IsRunning());
}

RODAK_TEST("AFE repeated uncertain reads resynchronize only after feed leases return") {
    Fixture fixture;
    for (int index = 0; index < 4; ++index) control::QueueStreamingFailure(0);
    host::SupplyAudioReads(8);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFlowResets(1));
    RODAK_CHECK(WaitUntil([] { return control::FlowSnapshot().vad_resets == 1; }));
    auto state = control::FlowSnapshot();
    RODAK_CHECK_EQ(state.reset_during_operation, size_t{0});
    RODAK_CHECK(fixture.frontend.IsRunning());
    host::SupplyAudioReads(3);
    RODAK_CHECK(control::WaitFlowSamples(512));
    rodakos::VoicePcmFrame frame;
    RODAK_CHECK(WaitUntil([&] { return fixture.frontend.PopFrame(frame); }));
    RODAK_CHECK_FALSE(frame.vad_valid);
    RODAK_CHECK(frame.samples == std::vector<int16_t>(320, 250));
}

RODAK_TEST("AFE feed zero is observable and malformed feed returns recover through reset") {
    for (const int injected : {0, -1, 1, 318, 960}) {
        Fixture fixture;
        control::SetFeedResult(injected);
        // The SDK has returned here, but Capture still owns its producer lease.
        observe::ArmLog("AFE feed rejected:");
        host::SupplyAudioReads(1);
        RODAK_CHECK(fixture.frontend.Start({}));
        RODAK_CHECK(observe::WaitLogBlocked());
        RODAK_CHECK(control::WaitFeedReturns(1));
        const size_t expected_partial = injected == -1 || injected == 960 ? 160U
                                        : injected == 318 ? 159U : 0U;
        if (expected_partial != 0) {
            RODAK_CHECK(WaitUntil([&] {
                return control::FlowSnapshot().partial_lost == expected_partial;
            }));
        } else if (injected != 0) {
            RODAK_CHECK(WaitUntil([] { return control::FlowSnapshot().fetches != 0; }));
        }
        const auto draining = control::FlowSnapshot();
        RODAK_CHECK_EQ(draining.returns, size_t{1});
        RODAK_CHECK_EQ(draining.feed_errors, size_t{1});
        RODAK_CHECK_EQ(draining.partial_lost, expected_partial);
        RODAK_CHECK_EQ(draining.resets, size_t{0});
        RODAK_CHECK_EQ(draining.vad_resets, size_t{0});
        RODAK_CHECK_EQ(draining.reset_during_operation, size_t{0});
        RODAK_CHECK_EQ(draining.valid_samples, size_t{0});
        if (injected == 0) RODAK_CHECK_EQ(draining.fetches, size_t{0});
        rodakos::VoicePcmFrame frame;
        RODAK_CHECK_FALSE(fixture.frontend.PopFrame(frame));
        std::printf("AFE_MALFORMED_DRAIN return=%d partial_lost=%zu fetches=%zu resets=%zu\n",
                    injected, draining.partial_lost, draining.fetches, draining.resets);
        observe::ReleaseLog();
        if (injected != 0) {
            RODAK_CHECK(control::WaitFlowResets(1));
            RODAK_CHECK(WaitUntil([] { return control::FlowSnapshot().vad_resets == 1; }));
        }
        RODAK_CHECK(fixture.frontend.IsRunning());
        RODAK_CHECK_FALSE(fixture.frontend.PopFrame(frame));
        host::SupplyAudioReads(4);
        RODAK_CHECK(control::WaitFlowSamples(512));
        RODAK_CHECK(WaitUntil([&] { return fixture.frontend.PopFrame(frame); }));
        RODAK_CHECK(frame.samples == std::vector<int16_t>(320, 250));
        RODAK_CHECK_FALSE(frame.vad_valid);
        const auto state = control::FlowSnapshot();
        RODAK_CHECK_EQ(state.partial_lost, expected_partial);
        RODAK_CHECK_EQ(state.reset_during_operation, size_t{0});
        RODAK_CHECK_EQ(state.resets, injected == 0 ? size_t{0} : size_t{1});
        RODAK_CHECK_EQ(state.vad_resets, state.resets);
        std::printf("AFE_MALFORMED_RECOVERY return=%d partial_lost=%zu resets=%zu vad_resets=%zu complete_samples=%zu frame_samples=%zu vad_valid=%d\n",
                    injected, state.partial_lost, state.resets, state.vad_resets,
                    state.valid_samples, frame.samples.size(), frame.vad_valid);
    }
}

RODAK_TEST("AFE three reset failures expose a terminal recorder error") {
    Fixture fixture;
    control::SetFeedResult(-1);
    control::SetResetResult(-1);
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFlowResets(3));
    RODAK_CHECK(WaitUntil([&] { return !fixture.frontend.IsRunning(); }));
    RODAK_CHECK(fixture.frontend.LastErrorSnapshot() == "AFE buffer recovery failed");
    const auto state = control::FlowSnapshot();
    RODAK_CHECK_EQ(state.vad_resets, size_t{3});
    RODAK_CHECK_EQ(state.reset_during_operation, size_t{0});
    Stop(fixture.frontend);
    RODAK_CHECK_EQ(control::DestroyDuringOperationCount(), size_t{0});
}

RODAK_TEST("AFE output credit overflow cannot wrap and resync waits for the current reader") {
    Fixture fixture;
    control::BeginScript();
    const auto pending = control::QueueFetch({ESP_OK, std::vector<int16_t>(512, 17), VAD_SPEECH});
    host::SupplyAudioReads(110);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFetchEntered(pending));
    RODAK_CHECK(WaitUntil([] { return control::FlowSnapshot().feed_errors != 0; }));
    RODAK_CHECK_EQ(control::FlowSnapshot().resets, size_t{0});
    control::ReleaseFetch(pending);
    RODAK_CHECK(control::WaitFlowResets(1));
    RODAK_CHECK(WaitUntil([] { return control::FlowSnapshot().vad_resets == 1; }));
    RODAK_CHECK_EQ(control::FlowSnapshot().reset_during_operation, size_t{0});
    rodakos::VoicePcmFrame frame;
    RODAK_CHECK_FALSE(fixture.frontend.PopFrame(frame));
    RODAK_CHECK(fixture.frontend.IsRunning());
}

RODAK_TEST("AFE stopping an incomplete output frame never waits for more producer input") {
    Fixture fixture;
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFeedReturns(1));
    Stop(fixture.frontend);
    RODAK_CHECK_FALSE(fixture.frontend.IsRunning());
    RODAK_CHECK_EQ(control::LastSummary().current_failures, 0U);
    RODAK_CHECK_EQ(control::DestroyDuringOperationCount(), size_t{0});
    control::BeginStreaming();
    host::SupplyAudioReads(3);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFlowSamples(512));
    RODAK_CHECK_EQ(control::FlowSnapshot().partial_lost, size_t{0});
    RODAK_CHECK(fixture.frontend.IsRunning());
}

RODAK_TEST("AFE resync drains a blocked producer before resetting buffer and VAD") {
    Fixture fixture;
    control::BeginScript();
    size_t failures[4];
    for (auto& ticket : failures) ticket = control::QueueFetch({});
    const auto draining = control::QueueFetch({});
    control::BlockFeedUntilFetch(14, 5);
    host::SupplyAudioReads(12);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(control::WaitFeedBlocked(14));
    for (auto ticket : failures) {
        RODAK_CHECK(control::WaitFetchEntered(ticket));
        control::ReleaseFetch(ticket);
    }
    RODAK_CHECK(control::WaitFetchEntered(draining));
    RODAK_CHECK_EQ(control::FlowSnapshot().resets, size_t{0});
    control::ReleaseFetch(draining);
    RODAK_CHECK(control::WaitFlowResets(1));
    RODAK_CHECK(WaitUntil([] { return control::FlowSnapshot().vad_resets == 1; }));
    RODAK_CHECK_EQ(control::FlowSnapshot().reset_during_operation, size_t{0});
    RODAK_CHECK(fixture.frontend.IsRunning());
    Stop(fixture.frontend);
    RODAK_CHECK_EQ(control::LastSummary().current_failures, 4U);
    RODAK_CHECK_EQ(control::DestroyDuringOperationCount(), size_t{0});
}

RODAK_TEST("AFE resync epoch fences a pending raw read and the old local input tail") {
    Fixture fixture;
    control::BeginScript();
    size_t failures[4];
    for (auto& ticket : failures) ticket = control::QueueFetch({});
    host::SetAudioSampleValue(111);
    host::BlockAudioReadAt(10);
    host::SupplyAudioReads(10);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(host::WaitAudioReadBlocked());
    RODAK_CHECK_EQ(control::FedMicrophoneSamples().size(), size_t{2816});
    RODAK_CHECK(fixture.frontend.ArmAecDiagnosticCapture(1000));
    for (auto ticket : failures) {
        RODAK_CHECK(control::WaitFetchEntered(ticket));
        control::ReleaseFetch(ticket);
    }
    RODAK_CHECK(control::WaitFlowResets(1));
    RODAK_CHECK(WaitUntil([] { return control::FlowSnapshot().vad_resets == 1; }));
    host::SetAudioSampleValue(222);
    host::BlockAudioReadAt(14);
    host::SupplyAudioReads(4);
    host::ReleaseAudioRead();
    RODAK_CHECK(host::WaitAudioReadBlocked());
    const auto fed = control::FedMicrophoneSamples();
    const auto stale = std::count(fed.begin() + 2816, fed.end(), int16_t{111});
    std::printf("AFE_EPOCH_OBSERVED old_feed_samples=2816 stale_after_reset=%zu\n", static_cast<size_t>(stale));
    RODAK_CHECK_EQ(stale, 0);
    RODAK_CHECK(std::all_of(fed.begin() + 2816, fed.end(), [](int16_t sample) { return sample == 222; }));
    RODAK_CHECK_EQ(control::FlowSnapshot().reset_during_operation, size_t{0});
    const auto diagnostic = fixture.frontend.GetAecDiagnosticCaptureStatus();
    std::printf("AFE_RAW_GAP_OBSERVED raw_samples=%zu raw_discontinuities=%u\n",
                diagnostic.raw_samples, diagnostic.raw_discontinuities);
    RODAK_CHECK_EQ(diagnostic.raw_samples, size_t{960});
    RODAK_CHECK_EQ(diagnostic.raw_discontinuities, 1U);
    RODAK_CHECK(diagnostic.afe_discontinuities >= 5U);
    RODAK_CHECK(fixture.frontend.IsRunning());
}
