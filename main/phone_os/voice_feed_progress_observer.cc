#include "phone_os/voice_feed_progress_observer.h"

#include <freertos/FreeRTOS.h>
#include <atomic>
#include <climits>
#include <cinttypes>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_timer.h>

#ifndef RODAKOS_RELEASE_TESTS
#error "Feed progress observations are TEST-only"
#endif

namespace rodakos {
namespace {
constexpr int64_t kFeedProgressBudgetUs = 20000000;
constexpr int64_t kFeedProgressSlowUs = 50000;
constexpr uint32_t kFeedProgressLogLimit = 8;

struct FeedProgressLive {
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    uint32_t generation = 0;
    uint32_t epoch = 0;
    uint32_t sequence = 0;
    uint32_t ticket = 0;
    uint32_t next_ticket = 0;
    uintptr_t target_handle = 0;
    uintptr_t last_other_handle = 0;
    uint32_t target_samples = 0;
    uint32_t other_samples = 0;
    uint32_t published_drops = 0;
    uint32_t baseline_drops = 0;
    uint32_t flags = 0;
    uint32_t logged = 0;
    uint32_t suppressed = 0;
    int64_t api_begin_us = 0;
    int64_t arm_before_us = 0;
    int64_t arm_after_us = 0;  // Unknown in live/open state; the caller ticket owns the upper bound.
    int64_t deadline_us = 0;
    int64_t first_target_begin_us = 0;
    int64_t last_target_end_us = 0;
    int64_t first_other_begin_us = 0;
    int64_t last_other_end_us = 0;
};
struct FeedProgressTickPrivate {
    uint32_t calls = 0;
    uint32_t drops = 0;
};
struct FeedProgressObserver {
    FeedProgressLive live;
    FeedProgressTickPrivate tick;
    std::atomic<uint32_t> sampling_generation{0};
    std::atomic<uint32_t> sampling_ticket{0};
};
// S3 disables hardware RMW atomics; only fixed-width loads/stores are used and target code is audited.
static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t));
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(FeedProgressLive) == 128);
static_assert(sizeof(FeedProgressObserver) == 144);
#else
static_assert(sizeof(FeedProgressLive) == 144);
static_assert(sizeof(FeedProgressObserver) == 160);
#endif

void IRAM_ATTR IncrementFeedCounter(uint32_t& value) {
    if (value != UINT32_MAX) ++value;
}

void CopyFeedProgress(const FeedProgressLive& live, VoiceFeedProgressSnapshot& out) {
    out.identity.generation = live.generation;
    out.identity.epoch = live.epoch;
    out.identity.sequence = live.sequence;
    out.identity.ticket = live.ticket;
    out.identity.status = kVoiceFeedProgressOk;
    out.identity.target_handle = live.target_handle;
    out.identity.api_begin_us = live.api_begin_us;
    out.identity.arm_before_us = live.arm_before_us;
    out.identity.arm_after_us = 0;
    out.flags = live.flags | kVoiceFeedProgressArmAfterUnknown;
    out.target_samples = live.target_samples;
    out.other_samples = live.other_samples;
    out.baseline_drops = live.baseline_drops;
    out.published_drops = live.published_drops;
    out.last_other_handle = live.last_other_handle;
    out.deadline_us = live.deadline_us;
    out.first_target_begin_us = live.first_target_begin_us;
    out.last_target_end_us = live.last_target_end_us;
    out.first_other_begin_us = live.first_other_begin_us;
    out.last_other_end_us = live.last_other_end_us;
}
}

DRAM_ATTR FeedProgressObserver g_voice_feed_progress_observer{};

VoiceFeedProgressTicket ArmVoiceFeedProgress(uint32_t generation, uint32_t epoch,
                                           uint32_t sequence, uintptr_t target_handle,
                                           int64_t api_begin_us, int64_t afe_started_us) {
    VoiceFeedProgressTicket result{generation, epoch, sequence, 0, kVoiceFeedProgressBusy,
                                  target_handle, api_begin_us, esp_timer_get_time(), 0};
    auto& live = g_voice_feed_progress_observer.live;
    if (portTRY_ENTER_CRITICAL(&live.mux, portMUX_TRY_LOCK)) {
        if (generation == 0 || sequence == 0 || target_handle == 0 || afe_started_us <= 0 ||
            afe_started_us > api_begin_us || api_begin_us > result.arm_before_us ||
            afe_started_us > INT64_MAX - kFeedProgressBudgetUs) {
            result.status = kVoiceFeedProgressUnavailable;
        } else if (generation < live.generation ||
                   (generation == live.generation && epoch < live.epoch)) {
            result.status = kVoiceFeedProgressStale;
        } else if (generation == live.generation && live.ticket != 0) {
            // A failed close makes the rest of this generation inconclusive; never borrow its slot.
            result.status = kVoiceFeedProgressConflict;
        } else {
            const int64_t deadline = generation == live.generation
                ? live.deadline_us : afe_started_us + kFeedProgressBudgetUs;
            if (generation != live.generation) {
                const bool unfinished = live.ticket != 0;
                live.generation = generation;
                live.logged = live.suppressed = 0;
                live.flags = unfinished ? kVoiceFeedProgressUnfinishedReplaced : 0;
                live.deadline_us = deadline;
                live.ticket = 0;
                g_voice_feed_progress_observer.sampling_ticket.store(0, std::memory_order_release);
                g_voice_feed_progress_observer.sampling_generation.store(generation, std::memory_order_release);
                live.epoch = live.sequence = 0;
                live.target_handle = live.last_other_handle = 0;
                live.target_samples = live.other_samples = 0;
                live.baseline_drops = live.published_drops;
                live.api_begin_us = live.arm_before_us = live.arm_after_us = 0;
                live.first_target_begin_us = live.last_target_end_us = 0;
                live.first_other_begin_us = live.last_other_end_us = 0;
            }
            if (result.arm_before_us >= deadline) {
                result.status = kVoiceFeedProgressExpired;
            } else if (live.next_ticket == UINT32_MAX) {
                result.status = kVoiceFeedProgressExhausted;
            } else {
                live.epoch = epoch;
                live.sequence = sequence;
                live.ticket = ++live.next_ticket;
                live.target_handle = target_handle;
                live.last_other_handle = 0;
                live.target_samples = live.other_samples = 0;
                live.baseline_drops = live.published_drops;
                live.flags &= kVoiceFeedProgressUnfinishedReplaced;
                live.api_begin_us = api_begin_us;
                live.arm_before_us = result.arm_before_us;
                live.arm_after_us = 0;
                live.first_target_begin_us = live.last_target_end_us = 0;
                live.first_other_begin_us = live.last_other_end_us = 0;
                result.ticket = live.ticket;
                result.status = kVoiceFeedProgressOk;
                g_voice_feed_progress_observer.sampling_ticket.store(live.ticket, std::memory_order_release);
            }
        }
        portEXIT_CRITICAL(&live.mux);
    }
    result.arm_after_us = esp_timer_get_time();
    return result;
}

void IRAM_ATTR ObserveVoiceFeedProgressTick(int64_t getter_begin_us, uintptr_t current_handle,
                                          int64_t getter_end_us) {
    auto& observer = g_voice_feed_progress_observer;
    auto& live = observer.live;
    IncrementFeedCounter(observer.tick.calls);
    bool valid = getter_begin_us > 0 && getter_end_us >= getter_begin_us && current_handle != 0;
    if (!valid) IncrementFeedCounter(observer.tick.drops);
    if (!portTRY_ENTER_CRITICAL_ISR(&live.mux, portMUX_TRY_LOCK)) {
        if (valid) IncrementFeedCounter(observer.tick.drops);
        return;
    }
    if (live.ticket != 0 &&
        (observer.sampling_generation.load(std::memory_order_acquire) != live.generation ||
         observer.sampling_ticket.load(std::memory_order_acquire) != live.ticket)) {
        portEXIT_CRITICAL_ISR(&live.mux);
        return;
    }
    live.published_drops = observer.tick.drops;
    if (live.ticket != 0) {
        const int64_t previous_end = live.last_target_end_us > live.last_other_end_us
            ? live.last_target_end_us : live.last_other_end_us;
        if (valid && previous_end != 0 && getter_begin_us < previous_end) {
            valid = false;
            IncrementFeedCounter(observer.tick.drops);
            live.published_drops = observer.tick.drops;
        }
        if (!valid) live.flags |= kVoiceFeedProgressClockInvalid;
        if (live.published_drops != live.baseline_drops) live.flags |= kVoiceFeedProgressDropped;
        if (observer.tick.calls == UINT32_MAX || observer.tick.drops == UINT32_MAX)
            live.flags |= kVoiceFeedProgressSaturated;
        if (valid && getter_end_us > live.deadline_us) live.flags |= kVoiceFeedProgressDeadlineReached;
        if (valid && getter_begin_us >= live.arm_before_us && getter_end_us <= live.deadline_us) {
            if (current_handle == live.target_handle) {
                if (live.target_samples == 0) live.first_target_begin_us = getter_begin_us;
                live.last_target_end_us = getter_end_us;
                IncrementFeedCounter(live.target_samples);
            } else {
                if (live.other_samples == 0) live.first_other_begin_us = getter_begin_us;
                live.last_other_end_us = getter_end_us;
                live.last_other_handle = current_handle;
                IncrementFeedCounter(live.other_samples);
            }
            if (live.target_samples == UINT32_MAX || live.other_samples == UINT32_MAX)
                live.flags |= kVoiceFeedProgressSaturated;
        }
    }
    portEXIT_CRITICAL_ISR(&live.mux);
}

void CloseVoiceFeedProgress(const VoiceFeedProgressTicket& ticket, int64_t api_return_us,
                            VoiceFeedProgressSnapshot& out) {
    out = {};
    out.identity = ticket;
    out.api_return_us = api_return_us;
    out.freeze_before_us = esp_timer_get_time();
    out.status = ticket.status == kVoiceFeedProgressOk ? kVoiceFeedProgressBusy : ticket.status;
    auto& live = g_voice_feed_progress_observer.live;
    // Arm/Close have one writer: Capture on core 0. ISR/readers never mutate these identity fields.
    // Revoke before the fallible copy, but reject the entire stale/altered identity first.
    if (ticket.status == kVoiceFeedProgressOk && ticket.ticket != 0 && ticket.ticket == live.ticket &&
        ticket.generation == live.generation && ticket.epoch == live.epoch &&
        ticket.sequence == live.sequence && ticket.target_handle == live.target_handle &&
        ticket.api_begin_us == live.api_begin_us && ticket.arm_before_us == live.arm_before_us &&
        g_voice_feed_progress_observer.sampling_generation.load(std::memory_order_acquire) == ticket.generation &&
        g_voice_feed_progress_observer.sampling_ticket.load(std::memory_order_acquire) == ticket.ticket) {
        g_voice_feed_progress_observer.sampling_ticket.store(0, std::memory_order_release);
    }
    if (ticket.status == kVoiceFeedProgressOk && portTRY_ENTER_CRITICAL(&live.mux, portMUX_TRY_LOCK)) {
        if (ticket.ticket != 0 && ticket.ticket == live.ticket && ticket.generation == live.generation &&
            ticket.epoch == live.epoch && ticket.sequence == live.sequence && ticket.target_handle == live.target_handle &&
            ticket.api_begin_us == live.api_begin_us && ticket.arm_before_us == live.arm_before_us) {
            CopyFeedProgress(live, out);
            out.identity = ticket;
            out.flags &= ~kVoiceFeedProgressArmAfterUnknown;
            out.status = kVoiceFeedProgressOk;
            live.ticket = 0;
        } else {
            out.status = kVoiceFeedProgressStale;
        }
        portEXIT_CRITICAL(&live.mux);
    }
    out.freeze_after_us = esp_timer_get_time();
    const int64_t last = out.last_target_end_us > out.last_other_end_us
        ? out.last_target_end_us : out.last_other_end_us;
    if (last > api_return_us) out.flags |= kVoiceFeedProgressCloseOverrun;
    if (ticket.arm_after_us < ticket.arm_before_us || api_return_us < ticket.arm_after_us ||
        out.freeze_before_us < api_return_us || out.freeze_after_us < out.freeze_before_us)
        out.flags |= kVoiceFeedProgressClockInvalid;
    // These are only accepted samples attributable inside the API bracket, never complete coverage.
    out.strict_counts_known = out.status == kVoiceFeedProgressOk && last != 0 &&
        (out.flags & (kVoiceFeedProgressCloseOverrun | kVoiceFeedProgressClockInvalid |
                      kVoiceFeedProgressDropped | kVoiceFeedProgressSaturated)) == 0;
}

void SnapshotOpenVoiceFeedProgress(uint32_t generation, uint32_t epoch, uint32_t sequence,
                                   VoiceFeedProgressSnapshot& out) {
    out = {};
    out.identity.generation = generation;
    out.identity.epoch = epoch;
    out.identity.sequence = sequence;
    out.flags = kVoiceFeedProgressArmAfterUnknown;
    out.status = kVoiceFeedProgressBusy;
    out.freeze_before_us = esp_timer_get_time();
    auto& live = g_voice_feed_progress_observer.live;
    if (portTRY_ENTER_CRITICAL(&live.mux, portMUX_TRY_LOCK)) {
        if (live.ticket != 0 && live.generation == generation && live.epoch == epoch && live.sequence == sequence) {
            CopyFeedProgress(live, out);
            const bool sampling = g_voice_feed_progress_observer.sampling_generation.load(std::memory_order_acquire) == live.generation &&
                g_voice_feed_progress_observer.sampling_ticket.load(std::memory_order_acquire) == live.ticket;
            out.status = sampling ? kVoiceFeedProgressOpen : kVoiceFeedProgressRetired;
            if (!sampling) out.flags |= kVoiceFeedProgressSamplingRetired;
        } else {
            out.status = kVoiceFeedProgressStale;
        }
        portEXIT_CRITICAL(&live.mux);
    }
    out.freeze_after_us = esp_timer_get_time();
}

void LogVoiceFeedProgress(const char* event, const VoiceFeedProgressSnapshot& out,
                          int64_t credit_published_us) {
    const auto& id = out.identity;
    const bool eligible = out.status != kVoiceFeedProgressOk || id.sequence <= 3 ||
        out.api_return_us - id.api_begin_us >= kFeedProgressSlowUs;
    if (!eligible) return;
    auto& live = g_voice_feed_progress_observer.live;
    bool allowed = false;
    if (portTRY_ENTER_CRITICAL(&live.mux, portMUX_TRY_LOCK)) {
        if (id.generation == live.generation) {
            if (live.logged < kFeedProgressLogLimit) { ++live.logged; allowed = true; }
            else IncrementFeedCounter(live.suppressed);
        }
        portEXIT_CRITICAL(&live.mux);
    }
    if (!allowed) return;
    ESP_LOGI("VoiceFeedProgress", "feed: event=%s generation=%u epoch=%u seq=%u ticket=%u status=%u flags=%u target=%" PRIuPTR " other=%" PRIuPTR " target_samples=%u other_samples=%u strict_counts_known=%u baseline_drops=%u published_drops=%u api_begin_us=%lld arm_before_us=%lld arm_after_us=%lld api_return_us=%lld credit_published_us=%lld freeze_before_us=%lld freeze_after_us=%lld deadline_us=%lld first_target_begin_us=%lld last_target_end_us=%lld first_other_begin_us=%lld last_other_end_us=%lld",
        event, id.generation, id.epoch, id.sequence, id.ticket, out.status, out.flags,
        id.target_handle, out.last_other_handle, out.target_samples, out.other_samples,
        static_cast<unsigned>(out.strict_counts_known), out.baseline_drops, out.published_drops,
        static_cast<long long>(id.api_begin_us), static_cast<long long>(id.arm_before_us),
        static_cast<long long>(id.arm_after_us), static_cast<long long>(out.api_return_us),
        static_cast<long long>(credit_published_us), static_cast<long long>(out.freeze_before_us),
        static_cast<long long>(out.freeze_after_us), static_cast<long long>(out.deadline_us),
        static_cast<long long>(out.first_target_begin_us), static_cast<long long>(out.last_target_end_us),
        static_cast<long long>(out.first_other_begin_us), static_cast<long long>(out.last_other_end_us));
}

void LogVoiceFeedProgressSummary(uint32_t generation) {
    uint32_t status = kVoiceFeedProgressBusy, logged = 0, suppressed = 0, ticket = 0, epoch = 0, sequence = 0, flags = 0;
    uint32_t target_samples = 0, other_samples = 0, sampling_ticket = 0;
    auto& live = g_voice_feed_progress_observer.live;
    if (portTRY_ENTER_CRITICAL(&live.mux, portMUX_TRY_LOCK)) {
        status = generation == live.generation ? kVoiceFeedProgressOk : kVoiceFeedProgressStale;
        if (status == kVoiceFeedProgressOk) {
            logged = live.logged; suppressed = live.suppressed;
            ticket = live.ticket; flags = live.flags;
            if (ticket != 0) {
                epoch = live.epoch; sequence = live.sequence;
                target_samples = live.target_samples; other_samples = live.other_samples;
                sampling_ticket = g_voice_feed_progress_observer.sampling_ticket.load(std::memory_order_acquire);
                if (sampling_ticket != ticket) flags |= kVoiceFeedProgressSamplingRetired;
            }
        }
        portEXIT_CRITICAL(&live.mux);
    }
    ESP_LOGI("VoiceFeedProgress", "summary: generation=%u status=%u logged=%u suppressed=%u active_ticket=%u epoch=%u seq=%u flags=%u sampling_ticket=%u target_samples=%u other_samples=%u log_limit=8 pending_private_drops_unknown=1 log_contention_misses_unknown=1",
        generation, status, logged, suppressed, ticket, epoch, sequence, flags, sampling_ticket, target_samples, other_samples);
}

void RetireVoiceFeedProgress(uint32_t generation) {
    // Only the core-0 Fetch exit calls this. Stop has drained the Capture lease; lifecycle joins
    // Fetch before any new generation starts or its Capture TCB may be reclaimed/reused.
    auto& observer = g_voice_feed_progress_observer;
    if (observer.sampling_generation.load(std::memory_order_acquire) == generation)
        observer.sampling_ticket.store(0, std::memory_order_release);
}

}  // namespace rodakos
