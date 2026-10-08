#include "test_framework.h"
#include "host_runtime.h"
#include "afe_fetch_control.h"
#include "observation_control.h"
#include "observer_platform.h"
#include "task_retirement_host.h"

#include "phone_os/voice_audio_frontend.h"
#include "phone_os/voice_feed_progress_observer.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

namespace {
namespace host = rodakos_test::voice_frontend;
namespace flow = rodakos_test::afe_fetch;
namespace observe = rodakos_test::afe_observation;
namespace platform = rodakos_test::frontend_observer;
using Point = observe::SemaphorePoint;
using Stage = rodakos::AfeProducerStage;
using namespace std::chrono_literals;

template<class T> bool WaitUntil(T predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

std::string CompleteReport(const rodakos::AfeProducerObservation& producer) {
    const auto identity = "event=complete generation=" + std::to_string(producer.generation) +
        " epoch=" + std::to_string(producer.epoch) + " seq=" + std::to_string(producer.sequence) + " ";
    for (const auto& line : observe::Logs())
        if (line.find("VoiceFeedProgress: feed: ") == 0 && line.find(identity) != std::string::npos)
            return line;
    return {};
}
std::string UnalignedOpenReport(const rodakos::AfeProducerObservation& producer, size_t begin) {
    const auto identity = "event=open generation=" + std::to_string(producer.generation) +
        " epoch=" + std::to_string(producer.epoch) + " seq=" + std::to_string(producer.sequence) + " ";
    const auto lines = observe::Logs();
    for (size_t index = begin; index < lines.size(); ++index) {
        const auto& line = lines[index];
        if (line.find("VoiceFeedProgress: feed: ") == 0 && line.find(identity) != std::string::npos)
            return line;
    }
    return {};
}
uint64_t Field(const std::string& line, const std::string& key) {
    const auto at = line.find(" " + key + "=");
    RODAK_CHECK(at != std::string::npos);
    return std::stoull(line.substr(at + key.size() + 2));
}

struct Fixture {
    rodakos::AudioCodecInput input;
    rodakos::VoiceAudioFrontend frontend{input};
    explicit Fixture(bool blocked = true) {
        static unsigned generation_base = 100;
        static int64_t clock_offset = 0;
        host::Reset();
        // The production observers are process-lifetime objects; every fixture is a new scope.
        frontend.conversation_generation_ = generation_base;
        generation_base += 100;
        clock_offset += 60000000;
        observe::AdvanceUs(clock_offset);
        flow::BeginStreaming(blocked);
    }
    ~Fixture() {
        observe::ReleaseAll();
        flow::ForceReleaseAll();
        host::ReleaseAllAudioReads();
        frontend.Deinit();
    }
    rodakos::AfeProducerObservation Producer() const {
        return frontend.afe_producer_diagnostics_.Snapshot();
    }
    rodakos::VoiceFeedProgressSnapshot Open(const rodakos::AfeProducerObservation& producer) const {
        rodakos::VoiceFeedProgressSnapshot out;
        WaitUntil([&] {
            rodakos::SnapshotOpenVoiceFeedProgress(producer.generation, producer.epoch,
                                                  producer.sequence, out);
            return out.status != rodakos::kVoiceFeedProgressBusy;
        });
        return out;
    }
};

retirement_host::Gate between_read_gate;
std::atomic<rodakos::VoiceAudioFrontend*> between_read_frontend{nullptr};
void PauseBetweenReads(TickType_t) {
    auto* frontend = between_read_frontend.load();
    if (frontend != nullptr && std::string(retirement_host::CurrentTaskName()) == "voice_frontend" &&
        frontend->afe_producer_diagnostics_.Snapshot().stage == Stage::kBetweenReads)
        between_read_gate.Enter();
}
std::atomic<void*> capture_to_observe{nullptr};
std::atomic<bool> capture_deleted{false};
void ObserveCaptureDelete(TaskHandle_t task) {
    if (task == capture_to_observe.load()) capture_deleted = true;
}
}

RODAK_TEST("TEST frontend arms the actual feed before SDK entry and classifies the same capture handle") {
    Fixture fixture;
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    const auto producer = fixture.Producer();
    const auto armed = fixture.Open(producer);
    const bool armed_before_feed = producer.stage == Stage::kApiBoundary &&
        armed.status == rodakos::kVoiceFeedProgressOpen && armed.identity.sequence == 1 &&
        armed.identity.target_handle == reinterpret_cast<uintptr_t>(fixture.frontend.task_);
    std::printf("TEST_FRONTEND_FEED_ENTRY armed_before_feed=%d feeds=%zu\n",
                armed_before_feed, flow::FlowSnapshot().feeds);
    RODAK_CHECK(armed_before_feed);
    RODAK_CHECK_EQ(platform::RegisteredHooks(0), 1U);
    const unsigned reads_before = platform::CurrentHandleReads();
    void* unrelated_task = fixture.frontend.afe_fetch_task_;
    RODAK_CHECK_NE(unrelated_task, nullptr);
    RODAK_CHECK_NE(unrelated_task, fixture.frontend.task_);
    platform::Tick(0, fixture.frontend.task_);
    observe::AdvanceUs(10000);
    platform::Tick(0, unrelated_task);
    observe::AdvanceUs(10000);
    platform::Tick(0, fixture.frontend.task_);
    const auto sampled = fixture.Open(producer);
    RODAK_CHECK_EQ(sampled.identity.target_handle, armed.identity.target_handle);
    RODAK_CHECK_EQ(sampled.target_samples, 2U);
    RODAK_CHECK_EQ(sampled.other_samples, 1U);
    RODAK_CHECK_EQ(sampled.last_other_handle, reinterpret_cast<uintptr_t>(unrelated_task));
    RODAK_CHECK(sampled.first_target_begin_us >= armed.identity.api_begin_us);
    RODAK_CHECK(sampled.last_target_end_us <= observe::NowUs());
    RODAK_CHECK_EQ(platform::CurrentHandleReads() - reads_before, 3U);
    RODAK_CHECK_EQ(platform::LastTickHandle(), fixture.frontend.task_);
    RODAK_CHECK((sampled.flags & rodakos::kVoiceFeedProgressArmAfterUnknown) != 0);
    flow::ReleaseFirstFeed();
    RODAK_CHECK(flow::WaitFeedReturns(1));
}

RODAK_TEST("TEST frontend closes feed observation before waiting for credit publication") {
    Fixture fixture;
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    const auto producer = fixture.Producer();
    RODAK_CHECK_EQ(fixture.Open(producer).status, rodakos::kVoiceFeedProgressOpen);
    platform::Tick(0, fixture.frontend.task_);
    std::unique_lock<std::mutex> business_lock(fixture.frontend.mutex_->ordinary);
    observe::ArmSemaphore(fixture.frontend.mutex_, "voice_frontend", Point::kBeforeTake, [] {
        return flow::FlowSnapshot().returns == 1;
    });
    flow::ReleaseFirstFeed();
    const bool reached_publish_wait = observe::WaitSemaphoreBlocked();
    const auto returned = fixture.Producer();
    const bool closed_before_publish = reached_publish_wait &&
        returned.stage == Stage::kReturnedWaitPublish &&
        fixture.Open(producer).status != rodakos::kVoiceFeedProgressOpen &&
        fixture.frontend.afe_feed_returns_ == 0;
    observe::AdvanceUs(200000);
    platform::Tick(0, fixture.frontend.afe_fetch_task_);
    std::printf("TEST_FRONTEND_CLOSE_BOUNDARY closed_before_publish=%d\n", closed_before_publish);
    observe::ReleaseSemaphore();
    business_lock.unlock();
    RODAK_CHECK(closed_before_publish);
    RODAK_CHECK(WaitUntil([&] { return fixture.Producer().max_return_to_publish_us >= 200000; }));
    RODAK_CHECK_EQ(fixture.Open(producer).status, rodakos::kVoiceFeedProgressStale);
    RODAK_CHECK(WaitUntil([&] { return !CompleteReport(producer).empty(); }));
    const auto report = CompleteReport(producer);
    RODAK_CHECK_EQ(Field(report, "target_samples"), 1U);
    RODAK_CHECK_EQ(Field(report, "other_samples"), 0U);
    RODAK_CHECK_EQ(Field(report, "strict_counts_known"), 1U);
    RODAK_CHECK(Field(report, "api_begin_us") <= Field(report, "arm_before_us"));
    RODAK_CHECK(Field(report, "arm_before_us") <= Field(report, "arm_after_us"));
    RODAK_CHECK(Field(report, "arm_after_us") <= Field(report, "api_return_us"));
    RODAK_CHECK(Field(report, "api_return_us") <= Field(report, "freeze_before_us"));
    RODAK_CHECK(Field(report, "freeze_before_us") <= Field(report, "freeze_after_us"));
    RODAK_CHECK(Field(report, "freeze_after_us") <= Field(report, "credit_published_us"));
    RODAK_CHECK(Field(report, "credit_published_us") - Field(report, "api_return_us") >= 200000U);
}

RODAK_TEST("TEST frontend cancellation closes the old feed before a new generation arms") {
    Fixture fixture;
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    const auto old = fixture.Producer();
    RODAK_CHECK_EQ(fixture.Open(old).status, rodakos::kVoiceFeedProgressOpen);
    std::atomic<bool> stopped{false};
    std::thread stopper([&] { fixture.frontend.Stop(); stopped = true; });
    const bool cancelled_while_feeding = WaitUntil([&] { return !fixture.frontend.IsRunning(); });
    const bool stop_waited_for_feed = !stopped.load();
    flow::ReleaseFirstFeed();
    stopper.join();
    RODAK_CHECK(cancelled_while_feeding && stop_waited_for_feed);
    RODAK_CHECK_EQ(fixture.Open(old).status, rodakos::kVoiceFeedProgressStale);
    RODAK_CHECK_EQ(flow::DestroyDuringOperationCount(), size_t{0});
    flow::BeginStreaming(true);
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    const auto next = fixture.Producer();
    RODAK_CHECK_NE(next.generation, old.generation);
    const auto armed = fixture.Open(next);
    RODAK_CHECK_EQ(armed.status, rodakos::kVoiceFeedProgressOpen);
    RODAK_CHECK_EQ(armed.identity.generation, next.generation);
    RODAK_CHECK_EQ(armed.target_samples, 0U);
    RODAK_CHECK_EQ(armed.other_samples, 0U);
    RODAK_CHECK_EQ(fixture.Open(old).status, rodakos::kVoiceFeedProgressStale);
}

RODAK_TEST("TEST frontend malformed feed closes its observation before resynchronizing the epoch") {
    Fixture fixture;
    flow::SetFeedResult(-1);
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    const auto old = fixture.Producer();
    RODAK_CHECK_EQ(fixture.Open(old).status, rodakos::kVoiceFeedProgressOpen);
    platform::Tick(0, fixture.frontend.task_);
    flow::ReleaseFirstFeed();
    RODAK_CHECK(flow::WaitFlowResets(1));
    RODAK_CHECK_EQ(fixture.Open(old).status, rodakos::kVoiceFeedProgressStale);
    flow::BlockFeedUntilFetch(2, 9999);
    host::SupplyAudioReads(1);
    RODAK_CHECK(flow::WaitFeedBlocked(2));
    const auto next = fixture.Producer();
    const auto armed = fixture.Open(next);
    RODAK_CHECK_EQ(next.generation, old.generation);
    RODAK_CHECK_NE(next.epoch, old.epoch);
    RODAK_CHECK_EQ(armed.status, rodakos::kVoiceFeedProgressOpen);
    RODAK_CHECK_EQ(armed.identity.epoch, next.epoch);
    RODAK_CHECK_EQ(armed.target_samples, 0U);
    RODAK_CHECK_EQ(flow::FlowSnapshot().reset_during_operation, size_t{0});
    RODAK_CHECK_EQ(flow::FlowSnapshot().feed_errors, size_t{1});
}

RODAK_TEST("TEST frontend expired observation leaves the real audio feed admitted") {
    Fixture fixture;
    host::BlockAudioReadAt(1);
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(host::WaitAudioReadBlocked());
    observe::AdvanceUs(21000000);
    host::ReleaseAudioRead();
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    const auto producer = fixture.Producer();
    RODAK_CHECK_EQ(producer.stage, Stage::kApiBoundary);
    const auto snapshot = fixture.Open(producer);
    RODAK_CHECK(snapshot.status != rodakos::kVoiceFeedProgressOpen);
    RODAK_CHECK_EQ(flow::FlowSnapshot().feeds, size_t{1});
    flow::ReleaseFirstFeed();
    RODAK_CHECK(flow::WaitFeedReturns(1));
    RODAK_CHECK(fixture.frontend.IsRunning());
    RODAK_CHECK_EQ(flow::FlowSnapshot().feed_errors, size_t{0});
}

RODAK_TEST("TEST frontend never lends an unfinished feed slot to the same numeric read sequence") {
    Fixture fixture;
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    const auto original = fixture.Producer();
    RODAK_CHECK_EQ(fixture.Open(original).status, rodakos::kVoiceFeedProgressOpen);
    observe::ArmSemaphore(fixture.frontend.mutex_, "afe_fetch", Point::kBeforeTake);
    RODAK_CHECK(observe::WaitSemaphoreBlocked());
    const auto rejections_before = platform::RejectedCaptureTries();
    between_read_frontend = &fixture.frontend;
    retirement_host::SetDelayHook(PauseBetweenReads);
    platform::FailNextCaptureTry();
    flow::ReleaseFirstFeed();
    const bool paused_between_reads = between_read_gate.Wait();
    const auto producer = fixture.Producer();
    const auto leftover = fixture.Open(original);
    const size_t logs_before = observe::Logs().size();
    observe::AdvanceUs(200000);
    observe::ReleaseSemaphore();
    const bool open_logged = WaitUntil([&] { return !UnalignedOpenReport(producer, logs_before).empty(); });
    const auto report = UnalignedOpenReport(producer, logs_before);
    retirement_host::SetDelayHook(nullptr);
    between_read_frontend = nullptr;
    between_read_gate.Release();
    // Cleanup comes before assertions so the precise negative cannot strand the real Capture.
    fixture.frontend.Stop();
    const bool read_scope_rejected = open_logged &&
        Field(report, "status") == rodakos::kVoiceFeedProgressStale &&
        (Field(report, "flags") & rodakos::kVoiceFeedProgressProducerUnaligned) != 0 &&
        Field(report, "ticket") == 0;
    std::printf("TEST_FRONTEND_READ_SCOPE paused=%d read_seq=%u feed_seq=%u read_scope_rejected=%d\n",
                paused_between_reads, producer.sequence, original.sequence, read_scope_rejected);
    RODAK_CHECK(paused_between_reads);
    RODAK_CHECK_EQ(platform::RejectedCaptureTries() - rejections_before, 1U);
    RODAK_CHECK_EQ(leftover.status, rodakos::kVoiceFeedProgressRetired);
    RODAK_CHECK((leftover.flags & rodakos::kVoiceFeedProgressSamplingRetired) != 0);
    RODAK_CHECK_EQ(producer.stage, Stage::kBetweenReads);
    RODAK_CHECK_EQ(producer.sequence, original.sequence);
    RODAK_CHECK(read_scope_rejected);
}

RODAK_TEST("TEST frontend retires failed close sampling before capture deletion and opaque reuse") {
    Fixture fixture;
    host::SupplyAudioReads(1);
    RODAK_CHECK(fixture.frontend.Start({}));
    RODAK_CHECK(flow::WaitFeedBlocked(1));
    const auto producer = fixture.Producer();
    const uintptr_t capture = reinterpret_cast<uintptr_t>(fixture.frontend.task_);
    const uintptr_t fetch = reinterpret_cast<uintptr_t>(fixture.frontend.afe_fetch_task_);
    platform::Tick(0, reinterpret_cast<void*>(capture));
    platform::Tick(0, reinterpret_cast<void*>(fetch));
    const auto before = fixture.Open(producer);
    RODAK_CHECK_EQ(before.target_samples, 1U);
    RODAK_CHECK_EQ(before.other_samples, 1U);
    const unsigned rejections_before = platform::RejectedCaptureTries();
    platform::FailNextCaptureTry();
    flow::ReleaseFirstFeed();
    RODAK_CHECK(WaitUntil([&] { return !CompleteReport(producer).empty(); }));
    RODAK_CHECK_EQ(Field(CompleteReport(producer), "status"), rodakos::kVoiceFeedProgressBusy);
    platform::Tick(0, reinterpret_cast<void*>(capture));
    platform::Tick(0, reinterpret_cast<void*>(fetch));
    const auto after_failed_close = fixture.Open(producer);
    const bool failed_close_stops_sampling = after_failed_close.status == rodakos::kVoiceFeedProgressRetired &&
        after_failed_close.target_samples == before.target_samples &&
        after_failed_close.other_samples == before.other_samples;

    capture_to_observe = reinterpret_cast<void*>(capture);
    capture_deleted = false;
    retirement_host::SetBeforeDeleteHook(ObserveCaptureDelete);
    fixture.frontend.Deinit();
    retirement_host::SetBeforeDeleteHook(nullptr);
    capture_to_observe = nullptr;
    platform::Tick(0, reinterpret_cast<void*>(capture));
    platform::Tick(0, reinterpret_cast<void*>(fetch));
    const auto after_delete = fixture.Open(producer);
    const bool retired_handle_not_sampled = capture_deleted.load() && fixture.frontend.task_ == nullptr &&
        after_delete.status == rodakos::kVoiceFeedProgressRetired &&
        after_delete.target_samples == after_failed_close.target_samples &&
        after_delete.other_samples == after_failed_close.other_samples;
    std::printf("TEST_FRONTEND_RETIRE failed_close_stops_sampling=%d retired_handle_not_sampled=%d\n",
                failed_close_stops_sampling, retired_handle_not_sampled);
    RODAK_CHECK_EQ(platform::RejectedCaptureTries() - rejections_before, 1U);
    RODAK_CHECK(failed_close_stops_sampling);
    RODAK_CHECK(retired_handle_not_sampled);
    RODAK_CHECK_NE(after_delete.identity.ticket, 0U);
    RODAK_CHECK_EQ(after_delete.identity.target_handle, capture);
    RODAK_CHECK((after_delete.flags & rodakos::kVoiceFeedProgressSamplingRetired) != 0);
    RODAK_CHECK_FALSE(after_delete.strict_counts_known);
    RODAK_CHECK_EQ(after_delete.api_return_us, 0);
}
