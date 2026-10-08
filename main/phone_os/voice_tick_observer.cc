#include "phone_os/voice_tick_observer.h"
#include "phone_os/voice_feed_progress_observer.h"

#include <freertos/FreeRTOS.h>
#include <climits>
#include <cinttypes>
#include <esp_attr.h>
#include <esp_freertos_hooks.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/task.h>
#include <esp_private/cache_utils.h>

#ifndef RODAKOS_RELEASE_TESTS
#error "The tick observer must not enter an ordinary firmware image"
#endif

namespace rodakos {
namespace {
constexpr int64_t kGenerationBudgetUs = 20 * 1000 * 1000;
constexpr uint32_t kSchedulerSuspended = 1;
constexpr uint32_t kCacheDisabled = 2;

struct TickPrivate {
    int64_t previous_call_us;
    uint32_t calls;
    uint32_t drops;
};
struct TickCore {
    TickPrivate tick;
    portMUX_TYPE mux;
    VoiceTickCoreSnapshot published;
};
struct TickControl {
    portMUX_TYPE mux;
    int64_t opened_us;
    uint32_t generation;
    uint32_t epoch;
    uint32_t token;
    uint32_t active;
    uint32_t registered_mask;
    uint32_t next_token;
    uint32_t initialized;
    int32_t registration_error[2];
    uint32_t reserved[3];
};
struct TickObserver {
    TickCore cores[2];
    TickControl control;
};
static_assert(sizeof(VoiceTickCoreSnapshot) == 104);
static_assert(sizeof(TickPrivate) == 16);
static_assert(sizeof(TickCore) == 128);
static_assert(sizeof(TickControl) == 64);
static_assert(sizeof(TickObserver) == 320);

}  // namespace

DRAM_ATTR TickObserver g_voice_tick_observer = {
    {{{}, portMUX_INITIALIZER_UNLOCKED, {}}, {{}, portMUX_INITIALIZER_UNLOCKED, {}}},
    {portMUX_INITIALIZER_UNLOCKED, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0}, {0, 0, 0}}
};

namespace {

void IRAM_ATTR Increment(uint32_t& value) {
    if (value != UINT32_MAX) ++value;
}

void IRAM_ATTR ObserveVoiceTick(unsigned core_id) {
    auto& core = g_voice_tick_observer.cores[core_id];
    const int64_t entry_us = esp_timer_get_time();
    const uintptr_t current_handle = core_id == 0
        ? reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle()) : 0;
    const uint32_t state = (xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED
                               ? kSchedulerSuspended : 0U) |
                           (!spi_flash_cache_enabled() ? kCacheDisabled : 0U);
    const int64_t sample_end_us = esp_timer_get_time();
    if (core_id == 0)
        ObserveVoiceFeedProgressTick(entry_us, current_handle, sample_end_us);
    const int64_t previous_us = core.tick.previous_call_us;
    const bool clock_valid = entry_us > 0 && previous_us >= 0 && sample_end_us >= entry_us &&
                             (previous_us == 0 || entry_us >= previous_us);
    core.tick.previous_call_us = clock_valid ? entry_us : 0;
    Increment(core.tick.calls);
    if (!clock_valid) Increment(core.tick.drops);
    if (!portTRY_ENTER_CRITICAL_ISR(&core.mux, portMUX_TRY_LOCK)) {
        if (clock_valid) Increment(core.tick.drops);
        return;
    }
    auto& out = core.published;
    if (clock_valid && out.window_token != 0 &&
        (entry_us >= out.deadline_us || sample_end_us > out.deadline_us)) {
        out.flags |= kVoiceTickDeadlineReached;
        portEXIT_CRITICAL_ISR(&core.mux);
        return;
    }
    if (!clock_valid) {
        out.flags |= kVoiceTickClockInvalid;
    }
    out.published_calls = core.tick.calls;
    out.published_drops = core.tick.drops;
    if (core.tick.calls == UINT32_MAX || core.tick.drops == UINT32_MAX)
        out.flags |= kVoiceTickCounterSaturated;
    if (out.published_drops != out.baseline_drops) out.flags |= kVoiceTickDropped;
    if (!clock_valid) {
        portEXIT_CRITICAL_ISR(&core.mux);
        return;
    }
    // Inactive callbacks keep the published lifetime counters current without attributing them.
    if (out.window_token == 0) {
        out.last_published_sample_end_us = sample_end_us;
        portEXIT_CRITICAL_ISR(&core.mux);
        return;
    }
    if (sample_end_us < out.window_open_us) {
        out.flags |= kVoiceTickBeforeWindow | kVoiceTickPrefixUncovered | kVoiceTickDropped;
        Increment(core.tick.drops);
        out.published_drops = core.tick.drops;
        portEXIT_CRITICAL_ISR(&core.mux);
        return;
    }
    if (entry_us < out.window_open_us) out.flags |= kVoiceTickCrossingSample;
    if (previous_us == 0 || previous_us < out.window_open_us)
        out.flags |= kVoiceTickPrefixUncovered | kVoiceTickCrossingInterval;
    const uint64_t span = static_cast<uint64_t>(sample_end_us - entry_us);
    const uint32_t sample_span = span > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(span);
    out.last_published_sample_end_us = sample_end_us;
    if (out.first_published_sample_end_us == 0)
        out.first_published_sample_end_us = sample_end_us;
    if (sample_span > out.max_sample_span_us) out.max_sample_span_us = sample_span;
    if ((state & kSchedulerSuspended) != 0) {
        Increment(out.suspended_samples);
        out.last_suspended_sample_end_us = sample_end_us;
    }
    if ((state & kCacheDisabled) != 0) {
        Increment(out.cache_disabled_samples);
        out.last_cache_disabled_sample_end_us = sample_end_us;
    }
    if (previous_us != 0 && entry_us >= out.window_open_us &&
        (out.max_current_call_us == 0 || entry_us - previous_us >
         out.max_current_call_us - out.max_previous_call_us)) {
        out.max_previous_call_us = previous_us;
        out.max_current_call_us = entry_us;
        out.max_state_bits = state;
    }
    portEXIT_CRITICAL_ISR(&core.mux);
}

void IRAM_ATTR VoiceTickHook0() { ObserveVoiceTick(0); }
void IRAM_ATTR VoiceTickHook1() { ObserveVoiceTick(1); }

// Fixed ordering and one attempt per lock. A failure has no scope/state side effects.
bool TryScopeLocks() {
    auto& state = g_voice_tick_observer;
    if (!portTRY_ENTER_CRITICAL(&state.control.mux, portMUX_TRY_LOCK)) return false;
    if (!portTRY_ENTER_CRITICAL(&state.cores[0].mux, portMUX_TRY_LOCK)) {
        portEXIT_CRITICAL(&state.control.mux);
        return false;
    }
    if (!portTRY_ENTER_CRITICAL(&state.cores[1].mux, portMUX_TRY_LOCK)) {
        portEXIT_CRITICAL(&state.cores[0].mux);
        portEXIT_CRITICAL(&state.control.mux);
        return false;
    }
    return true;
}

void ReleaseScopeLocks() {
    auto& state = g_voice_tick_observer;
    portEXIT_CRITICAL(&state.cores[1].mux);
    portEXIT_CRITICAL(&state.cores[0].mux);
    portEXIT_CRITICAL(&state.control.mux);
}

void InstallWindow(uint32_t token, int64_t opened_us, int64_t deadline_us) {
    for (auto& core : g_voice_tick_observer.cores) {
        const auto calls = core.published.published_calls;
        const auto drops = core.published.published_drops;
        const auto last_end = core.published.last_published_sample_end_us;
        core.published = {};
        core.published.window_token = token;
        core.published.window_open_us = opened_us;
        core.published.deadline_us = deadline_us;
        core.published.published_calls = core.published.baseline_calls = calls;
        core.published.published_drops = core.published.baseline_drops = drops;
        core.published.last_published_sample_end_us = last_end;
        core.published.flags = kVoiceTickPrefixUncovered;
    }
}

}  // namespace

bool InitializeVoiceTickObserver() {
    // The caller's lifecycle serializes initialization before a Fetch worker can read control.
    auto& control = g_voice_tick_observer.control;
    if (control.initialized != 0) return control.registered_mask == 3;
    control.initialized = 1;
    control.registration_error[0] = esp_register_freertos_tick_hook_for_cpu(VoiceTickHook0, 0);
    if (control.registration_error[0] == ESP_OK) control.registered_mask |= 1;
    control.registration_error[1] = esp_register_freertos_tick_hook_for_cpu(VoiceTickHook1, 1);
    if (control.registration_error[1] == ESP_OK) control.registered_mask |= 2;
    if (control.registered_mask != 3) {
        if ((control.registered_mask & 1) != 0)
            esp_deregister_freertos_tick_hook_for_cpu(VoiceTickHook0, 0);
        if ((control.registered_mask & 2) != 0)
            esp_deregister_freertos_tick_hook_for_cpu(VoiceTickHook1, 1);
        control.registered_mask = 0;
    }
    ESP_LOGI("VoiceTickObserver", "registration: mask=%u error0=%d error1=%d static_bytes=%u budget_us=%lld",
             static_cast<unsigned>(control.registered_mask), static_cast<int>(control.registration_error[0]), static_cast<int>(control.registration_error[1]),
             static_cast<unsigned>(sizeof(g_voice_tick_observer)),
             static_cast<long long>(kGenerationBudgetUs));
    return control.registered_mask == 3;
}

void LogVoiceTickBegin(uint32_t generation, uint32_t epoch, const VoiceTickBeginSnapshot& begin) {
    ESP_LOGI("VoiceTickObserver", "begin: generation=%u epoch=%u token=%u before_us=%lld after_us=%lld",
             static_cast<unsigned>(generation), static_cast<unsigned>(epoch), static_cast<unsigned>(begin.token),
             static_cast<long long>(begin.before_us), static_cast<long long>(begin.after_us));
}

uint32_t BeginVoiceTickObservation(uint32_t generation, uint32_t epoch, int64_t began_us,
                                   VoiceTickBeginSnapshot* begin) {
    const int64_t attempt_us = esp_timer_get_time();
    if (!TryScopeLocks()) {
        const int64_t end_us = esp_timer_get_time();
        if (begin != nullptr) *begin = {attempt_us, end_us, 0};
        else LogVoiceTickBegin(generation, epoch, {attempt_us, end_us, 0});
        return 0;
    }
    auto& control = g_voice_tick_observer.control;
    uint32_t token = 0;
    const bool newer = generation > control.generation ||
                       (generation == control.generation && epoch > control.epoch);
    if (control.registered_mask == 3 && generation != 0 && newer && began_us > 0 &&
        began_us <= attempt_us && began_us <= INT64_MAX - kGenerationBudgetUs &&
        control.next_token != UINT32_MAX) {
        const int64_t deadline = generation == control.generation
            ? g_voice_tick_observer.cores[0].published.deadline_us
            : began_us + kGenerationBudgetUs;
        token = ++control.next_token;
        control.generation = generation;
        control.epoch = epoch;
        control.token = token;
        control.active = 1;
        control.opened_us = attempt_us;
        InstallWindow(token, attempt_us, deadline);
    }
    ReleaseScopeLocks();
    const int64_t end_us = esp_timer_get_time();
    if (begin != nullptr) *begin = {attempt_us, end_us, token};
    else LogVoiceTickBegin(generation, epoch, {attempt_us, end_us, token});
    return token;
}

uint32_t CurrentVoiceTickObservation(uint32_t generation, uint32_t epoch) {
    auto& control = g_voice_tick_observer.control;
    if (!portTRY_ENTER_CRITICAL(&control.mux, portMUX_TRY_LOCK)) return 0;
    const uint32_t token = control.active && control.generation == generation && control.epoch == epoch
                              ? control.token : 0;
    portEXIT_CRITICAL(&control.mux);
    return token;
}

uint32_t FreezeVoiceTickObservation(uint32_t generation, uint32_t epoch,
                                  uint32_t expected_token, uint32_t gap_id,
                                  int64_t afe_event_observed_us, bool finish,
                                  VoiceTickSnapshot& snapshot) {
    snapshot = {};
    snapshot.generation = generation;
    snapshot.epoch = epoch;
    snapshot.window_token = expected_token;
    snapshot.gap_id = gap_id;
    snapshot.afe_event_observed_us = afe_event_observed_us;
    snapshot.freeze_begin_us = esp_timer_get_time();
    snapshot.status = kVoiceTickBusy;
    uint32_t next = 0;
    if (TryScopeLocks()) {
        auto& state = g_voice_tick_observer;
        auto& control = state.control;
        snapshot.registered_mask = control.registered_mask;
        snapshot.status = kVoiceTickUnavailable;
        if (expected_token != 0 && control.registered_mask == 3) {
            snapshot.status = kVoiceTickStale;
            if (control.active && control.generation == generation && control.epoch == epoch &&
                control.token == expected_token) {
                snapshot.cores[0] = state.cores[0].published;
                snapshot.cores[1] = state.cores[1].published;
                const int64_t deadline = state.cores[0].published.deadline_us;
                const bool expired = snapshot.freeze_begin_us >= deadline;
                snapshot.status = expired ? kVoiceTickExpired : kVoiceTickOk;
                if (!finish && !expired && control.next_token != UINT32_MAX)
                    next = ++control.next_token;
                else if (!finish && !expired) snapshot.status = kVoiceTickTokenExhausted;
                control.token = next;
                control.active = next != 0;
                control.opened_us = snapshot.freeze_begin_us;
                InstallWindow(next, snapshot.freeze_begin_us, deadline);
            }
        }
        ReleaseScopeLocks();
    }
    snapshot.freeze_end_us = esp_timer_get_time();
    return next;
}

void LogVoiceTickObservation(const char* event, const VoiceTickSnapshot& snapshot) {
    ESP_LOGI("VoiceTickObserver", "window: event=%s generation=%u epoch=%u token=%u gap_id=%u status=%u registered=%u afe_event_us=%lld freeze_begin_us=%lld freeze_end_us=%lld",
             event, static_cast<unsigned>(snapshot.generation), static_cast<unsigned>(snapshot.epoch),
             static_cast<unsigned>(snapshot.window_token), static_cast<unsigned>(snapshot.gap_id),
             static_cast<unsigned>(snapshot.status), static_cast<unsigned>(snapshot.registered_mask),
             static_cast<long long>(snapshot.afe_event_observed_us),
             static_cast<long long>(snapshot.freeze_begin_us), static_cast<long long>(snapshot.freeze_end_us));
    if (snapshot.status != kVoiceTickOk && snapshot.status != kVoiceTickExpired &&
        snapshot.status != kVoiceTickTokenExhausted) return;
    for (unsigned core = 0; core < 2; ++core) {
        const auto& value = snapshot.cores[core];
        ESP_LOGI("VoiceTickObserver", "core: token=%u cpu=%u flags=%u open_us=%lld deadline_us=%lld published_end_us=%lld first_published_end_us=%lld max_previous_us=%lld max_current_us=%lld calls=%u drops=%u baseline_calls=%u baseline_drops=%u max_span_us=%u max_state=%u suspended=%u cache_disabled=%u last_suspended_end_us=%lld last_cache_disabled_end_us=%lld",
                 static_cast<unsigned>(snapshot.window_token), core, static_cast<unsigned>(value.flags),
                 static_cast<long long>(value.window_open_us), static_cast<long long>(value.deadline_us),
                 static_cast<long long>(value.last_published_sample_end_us),
                 static_cast<long long>(value.first_published_sample_end_us),
                 static_cast<long long>(value.max_previous_call_us), static_cast<long long>(value.max_current_call_us),
                 static_cast<unsigned>(value.published_calls), static_cast<unsigned>(value.published_drops),
                 static_cast<unsigned>(value.baseline_calls), static_cast<unsigned>(value.baseline_drops),
                 static_cast<unsigned>(value.max_sample_span_us), static_cast<unsigned>(value.max_state_bits),
                 static_cast<unsigned>(value.suspended_samples), static_cast<unsigned>(value.cache_disabled_samples),
                 static_cast<long long>(value.last_suspended_sample_end_us),
                 static_cast<long long>(value.last_cache_disabled_sample_end_us));
    }
}

uint32_t ObserveVoiceTickBoundary(const char* event, uint32_t generation, uint32_t epoch,
                                 uint32_t expected_token, uint32_t gap_id,
                                 int64_t afe_event_observed_us, bool finish) {
    VoiceTickSnapshot snapshot;
    const uint32_t token = FreezeVoiceTickObservation(generation, epoch, expected_token, gap_id,
                                                     afe_event_observed_us, finish, snapshot);
    LogVoiceTickObservation(event, snapshot);
    return token;
}

}  // namespace rodakos
