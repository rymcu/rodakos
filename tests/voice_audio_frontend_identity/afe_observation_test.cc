#include "test_framework.h"
#include "host_runtime.h"
#include "afe_fetch_control.h"
#include "observation_control.h"

#include "phone_os/voice_audio_frontend.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <limits>
#include <mutex>
#include <thread>

namespace {
namespace host = rodakos_test::voice_frontend;
namespace flow = rodakos_test::afe_fetch;
namespace observe = rodakos_test::afe_observation;
using Stage = rodakos::AfeProducerStage;
using Point = observe::SemaphorePoint;
using namespace std::chrono_literals;
struct Fixture {
    rodakos::AudioCodecInput input;
    rodakos::VoiceAudioFrontend frontend{input};
    explicit Fixture(bool block_first = false) {
        host::Reset();
        flow::BeginStreaming(block_first);
    }
    ~Fixture() {
        observe::ReleaseAll();
        flow::ForceReleaseAll();
        host::ReleaseAllAudioReads();
        frontend.Deinit();
    }
    rodakos::AfeProducerObservation Snapshot() const { return frontend.afe_producer_diagnostics_.Snapshot(); }
};
template<class T> bool WaitUntil(T predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}
std::string FindLog(const std::string& contains) {
    for (const auto& line : observe::Logs())
        if (line.find(contains) != std::string::npos) return line;
    return {};
}
unsigned Number(const std::string& line, const std::string& key) {
    const auto begin = line.find(key + "=");
    if (begin == std::string::npos) return 0;
    return static_cast<unsigned>(std::stoul(line.substr(begin + key.size() + 1)));
}
}

RODAK_TEST("AFE observation tuples are coherent and maxima stay within their generation and epoch") {
    rodakos::AfeProducerDiagnostics diagnostics;
    std::atomic<bool> finished{false};
    std::atomic<bool> coherent{true};
    std::atomic<unsigned> seen_a{0};
    std::atomic<unsigned> seen_b{0};
    std::thread reader([&] {
        while (!finished.load()) {
            const auto value = diagnostics.Snapshot();
            if (value.stage == Stage::kUnknown) continue;
            const bool a = value.generation == 1 && value.epoch == 9 && value.sequence == 17 &&
                value.stage == Stage::kReturnedWaitPublish &&
                value.began_us == 123456789012LL && value.detail == 320 && value.previous_elapsed_us == 300 &&
                value.max_api_us == 300 && value.max_api_sequence == 17 && value.max_read_us == 0 &&
                value.max_read_sequence == 0 && value.max_return_to_publish_us == 0 && value.max_publish_sequence == 0;
            const bool b = value.generation == 2 && value.epoch == 3 && value.sequence == 23 &&
                value.stage == Stage::kReadReturned &&
                value.began_us == 223456789012LL && value.detail == 640 && value.previous_elapsed_us == 100 &&
                value.max_read_us == 100 && value.max_read_sequence == 23 && value.max_api_us == 0 &&
                value.max_api_sequence == 0 && value.max_return_to_publish_us == 0 && value.max_publish_sequence == 0;
            if (!a && !b) coherent = false;
            if (a) ++seen_a;
            if (b) ++seen_b;
        }
    });
    for (unsigned index = 0; index < 20000; ++index) {
        diagnostics.Publish(Stage::kReturnedWaitPublish, 1, 9, 17, 123456789012LL, 320, 300);
        if (index == 0) WaitUntil([&] { return seen_a.load() >= 100; });
        diagnostics.Publish(Stage::kReadReturned, 2, 3, 23, 223456789012LL, 640, 100);
        if (index == 0) WaitUntil([&] { return seen_b.load() >= 100; });
    }
    finished = true;
    reader.join();
    RODAK_CHECK(coherent.load());
    RODAK_CHECK(seen_a.load() >= 100 && seen_b.load() >= 100);
    diagnostics.Publish(Stage::kReturnedWaitPublish, 2, 3, 23, 223456789012LL, 640, 100);
    diagnostics.Publish(Stage::kReturnedWaitPublish, 2, 3, 24, 223456789112LL, 320, 1);
    RODAK_CHECK_EQ(diagnostics.Snapshot().max_api_us, 100U);
    RODAK_CHECK_EQ(diagnostics.Snapshot().max_api_sequence, 23U);
    diagnostics.Publish(Stage::kReadReturned, 2, 4, 25, 223456789212LL, 320, 7);
    RODAK_CHECK_EQ(diagnostics.Snapshot().max_api_us, 0U);
    RODAK_CHECK_EQ(diagnostics.Snapshot().max_read_us, 7U);
    RODAK_CHECK_EQ(rodakos::AfeElapsedUs(100, 99), 0U);
    RODAK_CHECK_EQ(rodakos::AfeElapsedUs(0, static_cast<int64_t>(UINT32_MAX) + 100), UINT32_MAX);
    RODAK_CHECK_EQ(rodakos::AfeElapsedUs(INT64_MIN, INT64_MAX), UINT32_MAX);
    RODAK_CHECK_EQ(rodakos::AfeElapsedUs(4294967290LL, 4294967310LL), 20U);
    diagnostics.Publish(Stage::kReturnedWaitPublish, UINT32_MAX, UINT32_MAX, UINT32_MAX, 1, 320, 100);
    diagnostics.Publish(Stage::kReturnedWaitPublish, UINT32_MAX, UINT32_MAX, 0, 2, 640, 1);
    RODAK_CHECK_EQ(diagnostics.Snapshot().max_api_sequence, UINT32_MAX);
    diagnostics.Publish(Stage::kReadReturned, UINT32_MAX, 0, 1, 3, 320, 2);
    RODAK_CHECK_EQ(diagnostics.Snapshot().max_api_us, 0U);
    diagnostics.Publish(Stage::kReturnedWaitPublish, 0, 0, 2, 4, 320, 3);
    RODAK_CHECK_EQ(diagnostics.Snapshot().max_read_us, 0U);
}

RODAK_TEST("AFE raw read stall retains the completed read maximum through recovery") {
    Fixture fixture;
    host::BlockAudioReadAt(1);
    host::SupplyAudioReads(3);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(host::WaitAudioReadBlocked());
    RODAK_CHECK(observe::WaitLogContaining("stage=raw_read"));
    RODAK_CHECK_EQ(flow::FlowSnapshot().feeds, size_t{0});
    observe::AdvanceUs(200000);
    host::ReleaseAudioRead();
    RODAK_CHECK(flow::WaitFlowSamples(512));
    RODAK_CHECK(observe::WaitLogContaining("reason=recovered"));
    RODAK_CHECK(observe::WaitLogContaining("AFE gap maxima:"));
    RODAK_CHECK(Number(FindLog("AFE gap maxima:"), "read_us") >= 200000);
    RODAK_CHECK_EQ(flow::FlowSnapshot().partial_lost, size_t{0});
}

RODAK_TEST("AFE admitted feed before API is observed without claiming SDK execution") {
    Fixture fixture;
    observe::ArmSemaphore(fixture.frontend.mutex_, "voice_frontend", Point::kAfterGive, [&] {
        return fixture.Snapshot().stage == Stage::kFeedAdmitted;
    });
    host::SupplyAudioReads(3);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(observe::WaitSemaphoreBlocked());
    RODAK_CHECK_EQ(flow::FlowSnapshot().feeds, size_t{0});
    RODAK_CHECK(observe::WaitLogContaining("stage=feed_admitted"));
    observe::ReleaseSemaphore();
    RODAK_CHECK(flow::WaitFlowSamples(512));
    RODAK_CHECK(observe::WaitLogContaining("reason=recovered"));
}

RODAK_TEST("AFE SDK boundary stall closes with the completed API wall maximum") {
    Fixture fixture(true);
    host::SupplyAudioReads(3);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    RODAK_CHECK(observe::WaitLogContaining("stage=api_boundary"));
    RODAK_CHECK_EQ(flow::FlowSnapshot().fetches, size_t{0});
    observe::AdvanceUs(200000);
    flow::ReleaseFirstFeed();
    RODAK_CHECK(flow::WaitFlowSamples(512));
    RODAK_CHECK(observe::WaitLogContaining("AFE gap maxima:"));
    RODAK_CHECK(Number(FindLog("AFE gap maxima:"), "api_wall_us") >= 200000);
    RODAK_CHECK(fixture.frontend.IsRunning());
}

RODAK_TEST("AFE SDK return is visible before the frontend credit publication lock") {
    Fixture fixture(true);
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    std::unique_lock<std::mutex> business_lock(fixture.frontend.mutex_->ordinary);
    observe::ArmSemaphore(fixture.frontend.mutex_, "voice_frontend", Point::kBeforeTake, [] {
        return flow::FlowSnapshot().returns == 1;
    });
    flow::ReleaseFirstFeed();
    RODAK_CHECK(observe::WaitSemaphoreBlocked());
    const auto value = fixture.Snapshot();
    const bool returned_visible = value.stage == Stage::kReturnedWaitPublish && value.detail == 320;
    std::printf("AFE_RETURN_OBSERVED returned_visible=%d stage=%u\n", returned_visible,
                static_cast<unsigned>(value.stage));
    RODAK_CHECK(returned_visible);
    RODAK_CHECK_EQ(fixture.frontend.afe_feed_returns_, 0U);
    observe::AdvanceUs(200000);
    observe::ReleaseSemaphore();
    business_lock.unlock();
    RODAK_CHECK(WaitUntil([&] { return fixture.Snapshot().max_return_to_publish_us >= 200000; }));
    RODAK_CHECK_EQ(fixture.Snapshot().max_api_us, value.previous_elapsed_us);
    RODAK_CHECK_EQ(fixture.Snapshot().max_publish_sequence, 1U);
}

RODAK_TEST("AFE stalled logger never owns the credit publication mutex") {
    Fixture fixture(true);
    observe::ArmLog("AFE input stalled:");
    host::SupplyAudioReads(3);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    RODAK_CHECK(observe::WaitLogBlocked());
    flow::ReleaseFirstFeed();
    // A second real SDK call requires Capture to publish first-call credits and return to its loop.
    const bool producer_progressed = WaitUntil([] { return flow::FlowSnapshot().feeds >= 2; });
    std::printf("AFE_LOG_UNLOCK_OBSERVED producer_progressed=%d\n", producer_progressed);
    observe::ReleaseLog();
    RODAK_CHECK(producer_progressed);
    RODAK_CHECK(flow::WaitFlowSamples(512));
    RODAK_CHECK(observe::WaitLogContaining("reason=recovered"));
}

RODAK_TEST("AFE delayed consumer post-fetch lock is retained in closed gap maxima") {
    Fixture fixture;
    host::BlockAudioReadAt(1);
    host::SupplyAudioReads(3);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(host::WaitAudioReadBlocked());
    RODAK_CHECK(observe::WaitLogContaining("stage=raw_read"));
    observe::ArmSemaphore(fixture.frontend.mutex_, "afe_fetch", Point::kBeforeTake, [] {
        return flow::FlowSnapshot().valid_samples >= 512;
    });
    host::ReleaseAudioRead();
    RODAK_CHECK(observe::WaitSemaphoreBlocked());
    observe::AdvanceUs(300000);
    observe::ReleaseSemaphore();
    RODAK_CHECK(observe::WaitLogContaining("reason=recovered"));
    const auto closed = FindLog("reason=recovered");
    RODAK_CHECK(Number(closed, "observe_lock_max_us") >= 300000);
    RODAK_CHECK(Number(closed, "observe_gap_max_us") >= 300000);
    RODAK_CHECK(Number(closed, "fetch_return_age_us") >= 300000);
    RODAK_CHECK_EQ(flow::FlowSnapshot().partial_lost, size_t{0});
}

RODAK_TEST("AFE repeated warnings after transient failure share one recovery gap") {
    Fixture fixture;
    host::BlockAudioReadAt(1);
    host::SupplyAudioReads(3);
    flow::QueueStreamingFailure(160);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(host::WaitAudioReadBlocked());
    RODAK_CHECK(observe::WaitLogContaining("stage=raw_read"));
    host::ReleaseAudioRead();
    RODAK_CHECK(WaitUntil([] { return flow::RejectedWarningCount() == 1; }));
    RODAK_CHECK(flow::WaitFlowStalls(2));
    host::SupplyAudioReads(2);
    RODAK_CHECK(flow::WaitFlowSamples(512));
    RODAK_CHECK(observe::WaitLogContaining("reason=recovered"));
    const auto closed = FindLog("reason=recovered");
    RODAK_CHECK_EQ(Number(closed, "gap_id"), 1U);
    RODAK_CHECK_EQ(Number(closed, "warnings"), 2U);
    RODAK_CHECK_EQ(Number(closed, "last_stall"), 2U);
    RODAK_CHECK_EQ(flow::FlowSnapshot().resets, size_t{0});
    for (const auto& line : observe::Logs())
        if (line.find("AFE input stalled:") == 0) RODAK_CHECK_EQ(Number(line, "gap_id"), 1U);
}

RODAK_TEST("AFE stop closes an old gap without recovering it in the next generation") {
    Fixture fixture;
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedReturns(1));
    RODAK_CHECK(observe::WaitLogContaining("AFE stall producer:"));
    const auto old_generation = fixture.Snapshot().generation;
    fixture.frontend.Stop();
    RODAK_CHECK(observe::WaitLogContaining("reason=cancelled"));
    RODAK_CHECK(FindLog("reason=recovered").empty());
    flow::BeginStreaming();
    host::SupplyAudioReads(3);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFlowSamples(512));
    RODAK_CHECK(fixture.Snapshot().generation != old_generation);
    RODAK_CHECK(FindLog("reason=recovered").empty());
}

RODAK_TEST("AFE reset closes a stalled gap with the old producer epoch maxima") {
    Fixture fixture(true);
    host::SupplyAudioReads(3);
    flow::SetFeedResult(-1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    RODAK_CHECK(observe::WaitLogContaining("stage=api_boundary"));
    observe::AdvanceUs(200000);
    flow::ReleaseFirstFeed();
    RODAK_CHECK(flow::WaitFlowResets(1));
    RODAK_CHECK(observe::WaitLogContaining("reason=resynced"));
    RODAK_CHECK(observe::WaitLogContaining("AFE gap maxima:"));
    const auto maxima = FindLog("AFE gap maxima:");
    RODAK_CHECK(maxima.find("scope=current") != std::string::npos);
    RODAK_CHECK(Number(maxima, "api_wall_us") >= 200000);
    RODAK_CHECK(FindLog("reason=recovered").empty());
    host::SupplyAudioReads(4);
    RODAK_CHECK(flow::WaitFlowSamples(512));
    RODAK_CHECK(fixture.frontend.IsRunning());
}

RODAK_TEST("AFE late raw read is explicitly stale for a new recording generation") {
    Fixture fixture;
    host::BlockAudioReadAt(1);
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(host::WaitAudioReadBlocked());
    const auto old_generation = fixture.Snapshot().generation;
    RODAK_CHECK(observe::WaitLogContaining("stage=raw_read"));
    fixture.frontend.Stop();
    RODAK_CHECK(observe::WaitLogContaining("reason=cancelled"));
    RODAK_CHECK_EQ(Number(FindLog("reason=cancelled"), "generation"), old_generation);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(observe::WaitLogContaining("scope=stale"));
    const auto stale = FindLog("scope=stale");
    RODAK_CHECK(Number(stale, "generation") != old_generation);
    RODAK_CHECK_EQ(Number(stale, "producer_generation"), old_generation);
    RODAK_CHECK_EQ(Number(stale, "age_us"), 0U);
    host::SupplyAudioReads(3);
    host::ReleaseAudioRead();
    RODAK_CHECK(flow::WaitFlowSamples(512));
    RODAK_CHECK(observe::WaitLogContaining("reason=recovered"));
    RODAK_CHECK(Number(FindLog("reason=recovered"), "generation") != old_generation);
    RODAK_CHECK_EQ(flow::FlowSnapshot().partial_lost, size_t{0});
}

RODAK_TEST("AFE observer maximum retains a long interval before its first warning") {
    Fixture fixture;
    host::BlockAudioReadAt(1);
    host::SupplyAudioReads(3);
    unsigned observations = 0;
    observe::ArmSemaphore(fixture.frontend.mutex_, "afe_fetch", Point::kBeforeTake, [&] {
        return fixture.Snapshot().stage == Stage::kRawRead && ++observations == 2;
    });
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(host::WaitAudioReadBlocked());
    RODAK_CHECK(observe::WaitSemaphoreBlocked());
    observe::AdvanceUs(60000);
    observe::ReleaseSemaphore();
    RODAK_CHECK(observe::WaitLogContaining("AFE stall producer:"));
    const auto stalled = FindLog("AFE input stalled:");
    RODAK_CHECK(Number(stalled, "observe_gap_max_us") >= 60000);
    RODAK_CHECK(Number(stalled, "observe_lock_max_us") >= 60000);
    RODAK_CHECK(Number(stalled, "observe_gap_us") < 60000);
    host::ReleaseAudioRead();
    RODAK_CHECK(observe::WaitLogContaining("reason=recovered"));
    RODAK_CHECK(Number(FindLog("reason=recovered"), "observe_gap_max_us") >= 60000);
}

RODAK_TEST("AFE observer timestamp follows a producer publication before its tuple copy") {
    Fixture fixture(true);
    host::SupplyAudioReads(1);
    observe::ArmCritical(&fixture.frontend.afe_producer_diagnostics_.mux_, "afe_fetch");
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    RODAK_CHECK(observe::WaitCriticalBlocked());
    observe::AdvanceUs(200000);
    flow::ReleaseFirstFeed();
    RODAK_CHECK(WaitUntil([&] { return fixture.Snapshot().stage == Stage::kReturnedWaitPublish; }));
    observe::AdvanceUs(100000);
    observe::ReleaseCritical();
    RODAK_CHECK(observe::WaitLogContaining("stage=returned_wait_publish"));
    const auto producer = FindLog("stage=returned_wait_publish");
    RODAK_CHECK(Number(producer, "age_us") >= 100000);
    RODAK_CHECK(Number(producer, "previous_us") >= 200000);
}

RODAK_TEST("AFE closure snapshots remain frozen while the closed log blocks") {
    Fixture fixture(true);
    host::BlockAudioReadAt(4);
    host::SupplyAudioReads(4);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    RODAK_CHECK(observe::WaitLogContaining("stage=api_boundary"));
    observe::ArmSemaphore(fixture.frontend.mutex_, "afe_fetch", Point::kBeforeTake, [] {
        return flow::FlowSnapshot().valid_samples >= 512;
    });
    observe::ArmLog("AFE input gap closed:");
    flow::ReleaseFirstFeed();
    RODAK_CHECK(observe::WaitSemaphoreBlocked());
    RODAK_CHECK(host::WaitAudioReadBlocked());
    const auto saved = fixture.Snapshot();
    RODAK_CHECK(saved.stage == Stage::kRawRead);
    RODAK_CHECK_EQ(saved.sequence, 4U);
    observe::AdvanceUs(100000);
    observe::ReleaseSemaphore();
    RODAK_CHECK(observe::WaitLogBlocked());
    const auto closed = FindLog("AFE input gap closed:");
    const unsigned elapsed = Number(closed, "elapsed_us");
    const auto age_upper = rodakos::AfeElapsedUs(saved.began_us, observe::NowUs());
    host::BlockAudioReadAt(5);
    host::SupplyAudioReads(1);
    host::ReleaseAudioRead();
    RODAK_CHECK(host::WaitAudioReadBlocked());
    RODAK_CHECK(fixture.Snapshot().sequence != saved.sequence);
    observe::AdvanceUs(500000);
    observe::ReleaseLog();
    RODAK_CHECK(observe::WaitLogContaining("AFE gap producer:"));
    RODAK_CHECK_EQ(Number(FindLog("AFE input gap closed:"), "elapsed_us"), elapsed);
    const auto producer = FindLog("AFE gap producer:");
    RODAK_CHECK(producer.find("stage=raw_read") != std::string::npos);
    RODAK_CHECK_EQ(Number(producer, "seq"), saved.sequence);
    RODAK_CHECK_EQ(Number(producer, "producer_generation"), saved.generation);
    RODAK_CHECK_EQ(Number(producer, "producer_epoch"), saved.epoch);
    RODAK_CHECK(Number(producer, "age_us") >= 100000);
    RODAK_CHECK(Number(producer, "age_us") <= age_upper);
    RODAK_CHECK(FindLog("reason=recovered").size() > 0);
}
