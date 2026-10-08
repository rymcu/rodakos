#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include RODAK_FEED_PROGRESS_SOURCE

using namespace rodakos;
namespace {
int64_t now_us = 100;
unsigned locks = 0;
bool fail_lock = false;
std::function<void()> after_timer;
std::vector<std::string> logs;
void Check(bool value, const char* label) {
    if (!value) { std::fprintf(stderr, "ASSERTION: %s\n", label); std::exit(1); }
}
void Reset() {
    Check(locks == 0, "no lock leaked");
    g_voice_feed_progress_observer.live = {};
    g_voice_feed_progress_observer.tick = {};
    g_voice_feed_progress_observer.sampling_generation.store(0);
    g_voice_feed_progress_observer.sampling_ticket.store(0);
    now_us = 100; fail_lock = false; after_timer = {}; logs.clear();
}
VoiceFeedProgressTicket Arm(uint32_t seq = 1, uint32_t gen = 1, uint32_t epoch = 0,
                           int64_t started = 1) {
    return ArmVoiceFeedProgress(gen, epoch, seq, 0x100, now_us - 10, started);
}
VoiceFeedProgressSnapshot Close(const VoiceFeedProgressTicket& ticket, int64_t returned) {
    VoiceFeedProgressSnapshot out;
    CloseVoiceFeedProgress(ticket, returned, out);
    return out;
}
void IdentityAndEnvelope() {
    Reset(); const auto ticket = Arm();
    ObserveVoiceFeedProgressTick(110, 0x100, 112);
    ObserveVoiceFeedProgressTick(130, 0x200, 132);
    now_us = 200; const auto out = Close(ticket, 190);
    Check(out.status == kVoiceFeedProgressOk && out.strict_counts_known && out.target_samples == 1 &&
          out.other_samples == 1 && out.last_other_handle == 0x200, "own and other identities stay opaque");
    Check(out.identity.arm_before_us == 100 && out.identity.arm_after_us == 100 &&
          out.first_target_begin_us == 110 && out.last_target_end_us == 112 &&
          out.freeze_before_us == 200 && out.freeze_after_us == 200, "ticket and sample envelopes remain distinct");
    LogVoiceFeedProgress("complete", out, 210);
    Check(logs.size() == 1 && logs[0].find("credit_published_us=210") != std::string::npos,
          "same-sequence publication endpoint is logged outside locks");
}
void OpenAndFence() {
    Reset(); const auto ticket = Arm(7, 3, 2);
    ObserveVoiceFeedProgressTick(110, 0x100, 112);
    VoiceFeedProgressSnapshot out;
    SnapshotOpenVoiceFeedProgress(3, 2, 7, out);
    Check(out.status == kVoiceFeedProgressOpen && out.identity.status == kVoiceFeedProgressOk &&
          out.identity.arm_after_us == 0 && out.api_return_us == 0 && !out.strict_counts_known &&
          (out.flags & kVoiceFeedProgressArmAfterUnknown), "open retains unknown arm end and return");
    SnapshotOpenVoiceFeedProgress(3, 2, 8, out);
    Check(out.status == kVoiceFeedProgressStale && out.target_samples == 0, "different feed sequence cannot borrow an active slot");
    now_us = 200; auto altered = ticket; ++altered.epoch;
    Check(Close(altered, 190).status == kVoiceFeedProgressStale &&
          g_voice_feed_progress_observer.live.ticket == ticket.ticket, "old epoch cannot clear current ticket");
    ++altered.ticket;
    Check(Close(altered, 190).status == kVoiceFeedProgressStale, "wrong token remains stale");
}
void CloseOverrun() {
    Reset(); const auto ticket = Arm();
    ObserveVoiceFeedProgressTick(110, 0x100, 112);
    ObserveVoiceFeedProgressTick(199, 0x200, 205);
    now_us = 210; const auto out = Close(ticket, 200);
    Check(out.target_samples == 1 && out.other_samples == 1 &&
          (out.flags & kVoiceFeedProgressCloseOverrun) && !out.strict_counts_known,
          "post-return sample invalidates strict counts without subtracting aggregates");
}
void ArmPrefix() {
    Reset();
    after_timer = []() {
        now_us = 150;
        ObserveVoiceFeedProgressTick(120, 0x100, 125);
    };
    const auto ticket = Arm();
    ObserveVoiceFeedProgressTick(160, 0x100, 161);
    VoiceFeedProgressSnapshot open;
    SnapshotOpenVoiceFeedProgress(1, 0, 1, open);
    Check(ticket.api_begin_us == 90 && ticket.arm_before_us == 100 && ticket.arm_after_us == 150 &&
          open.identity.arm_after_us == 0 && open.target_samples == 1 &&
          open.first_target_begin_us == 160, "late Arm prefix stays uncovered and open does not invent upper bound");
    now_us = 220; const auto out = Close(ticket, 210);
    Check(out.strict_counts_known && out.identity.arm_after_us == 150 && out.target_samples == 1,
          "complete ticket restores measured Arm envelope without claiming SDK execution");
}
void RegressedClock() {
    Reset(); const auto ticket = Arm();
    ObserveVoiceFeedProgressTick(500, 0x100, 501);
    ObserveVoiceFeedProgressTick(300, 0x100, 301);
    now_us = 600; const auto out = Close(ticket, 400);
    Check(out.last_target_end_us == 501 && out.target_samples == 1 && out.published_drops == 1 &&
          (out.flags & kVoiceFeedProgressClockInvalid) && (out.flags & kVoiceFeedProgressCloseOverrun) &&
          !out.strict_counts_known, "regressed sample cannot wash away a post-return endpoint");
}
void BusyAndConflict() {
    Reset(); fail_lock = true; const auto failed = Arm(); fail_lock = false;
    Check(failed.status == kVoiceFeedProgressBusy && g_voice_feed_progress_observer.live.generation == 0,
          "failed Arm does not publish a partial scope");
    const auto ticket = Arm();
    const auto before = g_voice_feed_progress_observer.live;
    now_us = 200; fail_lock = true; const auto out = Close(ticket, 190); fail_lock = false;
    Check(out.status == kVoiceFeedProgressBusy &&
          std::memcmp(&before, &g_voice_feed_progress_observer.live, sizeof(before)) == 0,
          "failed Close preserves the unfinished identity");
    Check(g_voice_feed_progress_observer.sampling_ticket.load() == 0,
          "failed Close still revokes sample authorization");
    const auto conflict = Arm(2);
    Check(conflict.status == kVoiceFeedProgressConflict && g_voice_feed_progress_observer.live.ticket == ticket.ticket,
          "next call refuses to silently replace an unfinished same-generation slot");
    const auto next = Arm(1, 2);
    Check(next.status == kVoiceFeedProgressOk && (g_voice_feed_progress_observer.live.flags & kVoiceFeedProgressUnfinishedReplaced),
          "new generation records previous unfinished scope");
    Check(Close(ticket, 190).status == kVoiceFeedProgressStale &&
          g_voice_feed_progress_observer.live.ticket == next.ticket, "late old close cannot clear new generation");
}
void DeadlineAndEmpty() {
    Reset(); auto ticket = Arm(); now_us = 200; auto out = Close(ticket, 190);
    Check(!out.strict_counts_known && out.target_samples == 0 && out.other_samples == 0,
          "zero samples cannot become normal execution evidence");
    now_us = 1000; ticket = Arm(2, 1, 1, 500);
    Check(g_voice_feed_progress_observer.live.deadline_us == 20000001,
          "epoch advance does not renew generation deadline");
    ObserveVoiceFeedProgressTick(20000000, 0x100, 20000002);
    now_us = 20000010; out = Close(ticket, 20000005);
    Check((out.flags & kVoiceFeedProgressDeadlineReached) && out.target_samples == 0,
          "getter envelope crossing deadline is not an accepted endpoint");
    Check(Arm(3, 1, 2, 19000000).status == kVoiceFeedProgressExpired,
          "later epoch cannot extend observation budget");
}
void ExpiredNewGeneration() {
    Reset(); const auto old = Arm(9, 1, 3);
    (void) old;
    now_us = 20001000; const auto expired = Arm(1, 2, 0, 1);
    Check(expired.status == kVoiceFeedProgressExpired, "late new generation is expired");
    LogVoiceFeedProgressSummary(2);
    Check(logs.back().find("active_ticket=0 epoch=0 seq=0") != std::string::npos &&
          (g_voice_feed_progress_observer.live.flags & kVoiceFeedProgressUnfinishedReplaced),
          "expired new generation does not relabel an old sequence");
}
void RetirementGate() {
    Reset(); const auto ticket = Arm();
    ObserveVoiceFeedProgressTick(110, 0x100, 111);
    now_us = 200;
    for (unsigned field = 0; field < 5; ++field) {
        auto altered = ticket;
        if (field == 0) ++altered.epoch;
        if (field == 1) ++altered.sequence;
        if (field == 2) ++altered.target_handle;
        if (field == 3) ++altered.api_begin_us;
        if (field == 4) ++altered.arm_before_us;
        const auto out = Close(altered, 190);
        Check(out.status == kVoiceFeedProgressStale &&
              g_voice_feed_progress_observer.sampling_ticket.load() == ticket.ticket,
              "altered identity cannot revoke an otherwise matching ticket");
    }
    fail_lock = true; const auto failed = Close(ticket, 190); fail_lock = false;
    Check(failed.status == kVoiceFeedProgressBusy, "injected Close copy failure remains visible");
    ObserveVoiceFeedProgressTick(210, 0x100, 211);
    VoiceFeedProgressSnapshot retired;
    SnapshotOpenVoiceFeedProgress(1, 0, 1, retired);
    Check(retired.status == kVoiceFeedProgressRetired && retired.target_samples == 1 &&
          (retired.flags & kVoiceFeedProgressSamplingRetired),
          "failed Close retires sampling without relabeling unfinished metadata complete");
    now_us = 300; const auto next = Arm(1, 2);
    ObserveVoiceFeedProgressTick(310, 0x100, 311);
    RetireVoiceFeedProgress(1);
    Check(g_voice_feed_progress_observer.sampling_ticket.load() == next.ticket,
          "old generation retire cannot revoke the new owner's sampling");
    RetireVoiceFeedProgress(2);
    ObserveVoiceFeedProgressTick(320, 0x100, 321);
    SnapshotOpenVoiceFeedProgress(2, 0, 1, retired);
    Check(retired.status == kVoiceFeedProgressRetired && retired.target_samples == 1,
          "same opaque address after retirement is not attributed to the old owner");
}
void LossAndSaturation() {
    Reset(); const auto ticket = Arm();
    fail_lock = true; ObserveVoiceFeedProgressTick(110, 0x100, 111); fail_lock = false;
    VoiceFeedProgressSnapshot open; SnapshotOpenVoiceFeedProgress(1, 0, 1, open);
    Check(open.published_drops == 0, "unpublished private loss remains unknown to foreground");
    ObserveVoiceFeedProgressTick(120, 0x100, 121);
    now_us = 200; auto out = Close(ticket, 190);
    Check(out.published_drops == 1 && (out.flags & kVoiceFeedProgressDropped) && !out.strict_counts_known,
          "published loss marks incomplete attribution");
    now_us = 300; const auto saturated = Arm(2);
    g_voice_feed_progress_observer.live.target_samples = UINT32_MAX;
    ObserveVoiceFeedProgressTick(310, 0x100, 311);
    now_us = 400; out = Close(saturated, 390);
    Check(out.target_samples == UINT32_MAX && (out.flags & kVoiceFeedProgressSaturated) && !out.strict_counts_known,
          "sample counters saturate without fabricating exact totals");
}
void QuotaAcrossEpoch() {
    Reset();
    for (uint32_t i = 1; i <= 6; ++i) {
        now_us = static_cast<int64_t>(i) * 100000;
        const auto ticket = Arm(i, 1, i - 1);
        VoiceFeedProgressSnapshot open; SnapshotOpenVoiceFeedProgress(1, i - 1, i, open);
        LogVoiceFeedProgress("open", open);
        now_us += 60000; const auto out = Close(ticket, now_us - 1);
        LogVoiceFeedProgress("complete", out, now_us + 1);
    }
    Check(logs.size() == 8 && g_voice_feed_progress_observer.live.logged == 8 &&
          g_voice_feed_progress_observer.live.suppressed == 4,
          "open and complete share eight-record quota across epochs");
    LogVoiceFeedProgressSummary(1);
    Check(logs.size() == 9 && logs.back().find("logged=8 suppressed=4") != std::string::npos,
          "one existing flow-stop summary retains suppressed records");
}
void ThresholdAndFrozenLog() {
    Reset(); now_us = 1000; const auto ticket = Arm(4);
    ObserveVoiceFeedProgressTick(1010, 0x100, 1011);
    now_us = 1100; auto out = Close(ticket, 1090);
    LogVoiceFeedProgress("complete", out, 1120);
    Check(logs.empty(), "short calls after first three do not consume quota");
    out.identity.sequence = 1;
    LogVoiceFeedProgress("complete", out, 1120); const auto first = logs.back();
    now_us = 2000; Arm(5); ObserveVoiceFeedProgressTick(2010, 0x200, 2011);
    LogVoiceFeedProgress("complete", out, 1120);
    Check(logs.back() == first, "logging reads frozen fields instead of newer live profile");
}
}

bool observer_try_lock(portMUX_TYPE* mux, int timeout) {
    Check(timeout == 0, "one nonwaiting lock attempt");
    if (fail_lock || mux->locked) return false;
    mux->locked = 1; ++locks; return true;
}
void observer_unlock(portMUX_TYPE* mux) { Check(mux->locked && locks, "matching unlock"); mux->locked = 0; --locks; }
int64_t esp_timer_get_time() {
    Check(locks == 0, "timers stay outside feed observer locks");
    const auto result = now_us;
    if (after_timer) { auto callback = std::move(after_timer); after_timer = {}; callback(); }
    return result;
}
void observer_log(const char*, const char* format, ...) {
    Check(locks == 0, "logs stay outside feed observer locks");
    char output[2048]; va_list args; va_start(args, format); std::vsnprintf(output, sizeof(output), format, args); va_end(args);
    logs.emplace_back(output);
}
int main(int argc, char** argv) {
    const std::vector<std::pair<const char*, void(*)()>> cases = {
        {"identity", IdentityAndEnvelope}, {"open-fence", OpenAndFence}, {"overrun", CloseOverrun},
        {"arm-prefix", ArmPrefix},
        {"clock-regression", RegressedClock}, {"busy-conflict", BusyAndConflict}, {"deadline", DeadlineAndEmpty},
        {"expired-generation", ExpiredNewGeneration}, {"loss", LossAndSaturation},
        {"retirement", RetirementGate},
        {"quota", QuotaAcrossEpoch}, {"threshold-frozen", ThresholdAndFrozenLog}};
    unsigned executed = 0;
    for (const auto& item : cases) if (argc == 1 || item.first == std::string(argv[1])) { item.second(); ++executed; }
    Check(executed == (argc == 1 ? cases.size() : 1), "requested actual-module case exists");
    std::printf("PASS: %u actual feed-progress module groups\n", executed);
}
