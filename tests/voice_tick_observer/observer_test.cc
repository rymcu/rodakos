#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <functional>
#include <string>
#include <vector>

// Include the exact production TU to inspect private lifetime state without production test APIs.
#include RODAK_OBSERVER_SOURCE

using namespace rodakos;
namespace {
int64_t now_us = 1;
int64_t sample_advance = 0;
int scheduler = taskSCHEDULER_RUNNING;
bool cache_enabled = true;
unsigned lock_calls = 0;
unsigned fail_lock_call = 0;
unsigned locks_held = 0;
int registration_fail = -1;
unsigned registrations = 0;
unsigned deregistrations = 0;
esp_freertos_tick_cb_t hooks[2] = {};
std::vector<std::string> logs;
std::function<void()> on_cache;
std::function<void()> after_timer;
unsigned timer_calls = 0;
unsigned after_timer_call = 0;
void Require(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "ASSERTION: %s\n", label); std::exit(1); }
}
void Reset() {
    Require(locks_held == 0, "no lock leaked");
    g_voice_tick_observer = {};
    now_us = 1; sample_advance = 0; scheduler = taskSCHEDULER_RUNNING;
    cache_enabled = true; lock_calls = fail_lock_call = locks_held = 0;
    registration_fail = -1; registrations = deregistrations = 0;
    hooks[0] = hooks[1] = nullptr; logs.clear(); on_cache = {}; after_timer = {};
    timer_calls = after_timer_call = 0;
}
uint32_t Begin(uint32_t generation = 1, uint32_t epoch = 0) {
    Require(InitializeVoiceTickObserver(), "registration succeeds");
    return BeginVoiceTickObservation(generation, epoch, now_us);
}
void Tick(unsigned core, int64_t at) {
    now_us = at;
    Require(hooks[core] != nullptr, "registered callback exists");
    hooks[core]();
}
VoiceTickSnapshot Freeze(uint32_t token, bool finish = false, uint32_t gen = 1, uint32_t epoch = 0) {
    VoiceTickSnapshot out;
    FreezeVoiceTickObservation(gen, epoch, token, 7, now_us, finish, out);
    return out;
}
void Registration() {
    for (int failed = 0; failed < 2; ++failed) {
        Reset(); registration_fail = failed;
        Require(!InitializeVoiceTickObserver(), "partial registration rejected");
        Require(deregistrations == 1 && !hooks[0] && !hooks[1], "partial registration rolled back");
        Require(!InitializeVoiceTickObserver() && registrations == 2, "registration failure latched");
        Require(BeginVoiceTickObservation(1, 0, 1) == 0, "missing hook cannot arm");
    }
    Reset(); Begin(); InitializeVoiceTickObserver();
    Require(registrations == 2, "lifetime registration idempotent");
}
void ActualTickAndCatchup() {
    Reset(); const auto token = Begin();
    Tick(0, 100); Tick(1, 200100); Tick(0, 210100); Tick(0, 210101);
    const auto out = Freeze(token);
    Require(out.cores[0].max_current_call_us - out.cores[0].max_previous_call_us == 210000,
            "actual clock gap survives tick catchup");
    Require(out.cores[0].first_published_sample_end_us == 100, "first accepted sample recorded");
    Require(out.cores[1].max_current_call_us == 0, "first ISR has no invented predecessor");
}
void DropAndPrivatePrevious() {
    Reset(); auto token = Begin(); Tick(0, 100);
    fail_lock_call = lock_calls + 1; Tick(0, 310100); fail_lock_call = 0;
    Tick(0, 320100);
    auto out = Freeze(token);
    Require(out.cores[0].published_drops == 1 && (out.cores[0].flags & kVoiceTickDropped), "lost large interval marks incomplete");
    Require(out.cores[0].max_current_call_us - out.cores[0].max_previous_call_us == 10000,
            "private ISR predecessor advances even on failed publish");
    token = CurrentVoiceTickObservation(1, 0); Tick(0, 330100); out = Freeze(token);
    Require(out.cores[0].baseline_drops == 1 && out.cores[0].published_drops == 1,
            "historical drop remains baseline not new-window drop");
}
void ThreeLocksAndFences() {
    for (unsigned failed = 1; failed <= 3; ++failed) {
        Reset(); const auto token = Begin(); Tick(0, 100);
        const auto before = g_voice_tick_observer;
        fail_lock_call = lock_calls + failed;
        const auto out = Freeze(token);
        Require(out.status == kVoiceTickBusy, "one of three locks unavailable");
        Require(std::memcmp(&before, &g_voice_tick_observer, sizeof(before)) == 0,
                "three-lock failure has no scope side effects");
    }
    Reset(); const auto old = Begin(); now_us = 1000;
    const auto newer = BeginVoiceTickObservation(2, 0, now_us);
    auto out = Freeze(old);
    Require(out.status == kVoiceTickStale && CurrentVoiceTickObservation(2, 0) == newer,
            "old generation cannot clear new scope");
    out = Freeze(newer + 1, false, 2);
    Require(out.status == kVoiceTickStale && CurrentVoiceTickObservation(2, 0) == newer,
            "old token cannot clear new scope");
}
void CrossingAndFreeze() {
    Reset(); InitializeVoiceTickObserver(); Tick(0, 100); Tick(0, 500100);
    now_us = 501000; const auto token = BeginVoiceTickObservation(1, 0, now_us);
    Tick(0, 510100); auto out = Freeze(token);
    Require(out.cores[0].max_previous_call_us == 500100 && out.cores[0].max_current_call_us == 510100,
            "cross-window predecessor interval retained without boot maximum");
    Require((out.cores[0].flags & kVoiceTickCrossingInterval) != 0, "crossing interval flagged");
    LogVoiceTickObservation("stall", out); const auto frozen_log = logs;
    Tick(0, 900100); logs.clear(); LogVoiceTickObservation("stall", out);
    Require(logs.size() == 3 && logs[1] == frozen_log[frozen_log.size() - 2],
            "log uses frozen copy after live updates");
}
void AnomaliesAndDeadline() {
    Reset(); const auto token = Begin(); Tick(0, 100);
    sample_advance = 35; scheduler = taskSCHEDULER_SUSPENDED; Tick(0, 10100);
    sample_advance = 57; scheduler = taskSCHEDULER_RUNNING; cache_enabled = false; Tick(0, 20100);
    sample_advance = 0; cache_enabled = true; Tick(0, 310100);
    auto out = Freeze(token);
    Require(out.cores[0].suspended_samples == 1 && out.cores[0].cache_disabled_samples == 1,
            "nonmaximum intermediate anomalies retained");
    Require(out.cores[0].max_sample_span_us == 57 && out.cores[0].last_cache_disabled_sample_end_us == 20157,
            "whole window sample envelope not only gap maximum");
    const auto next = CurrentVoiceTickObservation(1, 0);
    Tick(0, 20000101); out = Freeze(next);
    Require(out.status == kVoiceTickExpired && out.cores[0].deadline_us == 20000001,
            "rotation never extends generation deadline");
    Require(out.cores[0].flags & kVoiceTickDeadlineReached, "ISR deadline independent of worker");
}
void ClockAndSaturation() {
    Reset(); const auto token = Begin(); Tick(0, 100); Tick(0, INT64_MIN); Tick(0, 200);
    auto out = Freeze(token);
    Require(out.cores[0].flags & kVoiceTickClockInvalid, "negative clock recovery stays marked");
    Require(out.cores[0].published_drops != 0, "invalid clock loses observation explicitly");
    const auto next = CurrentVoiceTickObservation(1, 0);
    g_voice_tick_observer.cores[0].tick.calls = UINT32_MAX;
    Tick(0, 300); out = Freeze(next);
    Require(out.cores[0].flags & kVoiceTickCounterSaturated, "counter does not wrap into clean scope");
}
void SampleCrossesTransition() {
    Reset(); auto token = Begin(); Tick(0, 100);
    on_cache = [&]() { now_us = 1000; token = BeginVoiceTickObservation(2, 0, now_us); };
    Tick(0, 900); now_us = 1010;
    const auto out = Freeze(token, false, 2);
    Require(out.cores[0].flags & kVoiceTickCrossingSample, "sample envelope crossing new scope flagged");
    Require(out.cores[0].max_current_call_us == 0, "old entry is not new window gap");
}
void UnpublishedTailAndFinish() {
    Reset(); auto token = Begin(); Tick(0, 100);
    fail_lock_call = lock_calls + 1; Tick(0, 310100); fail_lock_call = 0;
    const auto private_before = g_voice_tick_observer.cores[0].tick;
    auto out = Freeze(token);
    Require(out.cores[0].published_drops == 0 && out.cores[0].last_published_sample_end_us == 100 &&
            out.freeze_end_us == 310100, "unpublished loss remains explicitly uncovered tail");
    Require(std::memcmp(&private_before, &g_voice_tick_observer.cores[0].tick, sizeof(private_before)) == 0,
            "foreground never consumes private ISR state");
    token = CurrentVoiceTickObservation(1, 0);
    const auto before = g_voice_tick_observer;
    fail_lock_call = lock_calls + 3; out = Freeze(token, true); fail_lock_call = 0;
    Require(out.status == kVoiceTickBusy && std::memcmp(&before, &g_voice_tick_observer, sizeof(before)) == 0,
            "failed finish preserves entire scope");
    out = Freeze(token, true);
    Require(out.status == kVoiceTickOk && CurrentVoiceTickObservation(1, 0) == 0,
            "successful finish clears active scope");
    Tick(0, 400100); now_us = 410000; token = BeginVoiceTickObservation(2, 0, now_us);
    out = Freeze(token, false, 2);
    Require(out.cores[0].baseline_calls == 3 && out.cores[0].baseline_drops == 1,
            "inactive publication becomes new generation baseline");
}
void SampleEntirelyBeforeNewWindow() {
    Reset(); auto token = Begin(); Tick(0, 100);
    after_timer_call = timer_calls + 2;
    after_timer = [&]() { now_us = 1000; token = BeginVoiceTickObservation(2, 0, now_us); };
    Tick(0, 900); now_us = 1010;
    const auto out = Freeze(token, false, 2);
    Require((out.cores[0].flags & (kVoiceTickBeforeWindow | kVoiceTickDropped)) ==
            (kVoiceTickBeforeWindow | kVoiceTickDropped), "late publication of old envelope rejected with quality gap");
    Require(out.cores[0].first_published_sample_end_us == 0 && out.cores[0].last_published_sample_end_us == 100,
            "old sample cannot advance new window coverage");
}
void EpochDeadlineAndExhaustion() {
    Reset(); const auto token = Begin(); Tick(0, 100);
    now_us = 1000; const auto epoch_token = BeginVoiceTickObservation(1, 1, now_us);
    auto out = Freeze(token);
    Require(out.status == kVoiceTickStale && CurrentVoiceTickObservation(1, 1) == epoch_token,
            "old epoch cannot clear advanced scope");
    Tick(0, 2000); out = Freeze(epoch_token, false, 1, 1);
    Require(out.cores[0].deadline_us == 20000001, "epoch advance preserves generation deadline");
    const auto active = CurrentVoiceTickObservation(1, 1);
    const auto calls = g_voice_tick_observer.cores[0].published.published_calls;
    const auto end = g_voice_tick_observer.cores[0].published.last_published_sample_end_us;
    Tick(0, 20000100); out = Freeze(active, false, 1, 1);
    Require(out.cores[0].published_calls == calls && out.cores[0].last_published_sample_end_us == end,
            "deadline counters and accepted coverage stop together");
    Reset(); const auto last = Begin(); Tick(0, 100);
    g_voice_tick_observer.control.next_token = UINT32_MAX;
    out = Freeze(last);
    Require(out.status == kVoiceTickTokenExhausted && CurrentVoiceTickObservation(1, 0) == 0,
            "token exhaustion explicit instead of clean rotation");
}
}  // namespace

bool observer_try_lock(portMUX_TYPE* mux, int timeout) {
    Require(timeout == 0, "critical acquisition is one nonwaiting attempt");
    ++lock_calls;
    if (lock_calls == fail_lock_call || mux->locked) return false;
    mux->locked = 1; ++locks_held; return true;
}
void observer_unlock(portMUX_TYPE* mux) { Require(mux->locked && locks_held, "matching unlock"); mux->locked = 0; --locks_held; }
int64_t esp_timer_get_time() {
    Require(locks_held == 0, "timer outside observer locks");
    const auto result = now_us;
    if (++timer_calls == after_timer_call && after_timer) {
        auto callback = std::move(after_timer); after_timer = {}; callback();
    }
    return result;
}
int xTaskGetSchedulerState() { return scheduler; }
bool spi_flash_cache_enabled() { if (on_cache) { auto callback = std::move(on_cache); on_cache = {}; callback(); } now_us += sample_advance; return cache_enabled; }
int esp_register_freertos_tick_hook_for_cpu(esp_freertos_tick_cb_t hook, UBaseType_t core) {
    ++registrations; if (registration_fail == static_cast<int>(core)) return 0x101; hooks[core] = hook; return ESP_OK;
}
void esp_deregister_freertos_tick_hook_for_cpu(esp_freertos_tick_cb_t hook, UBaseType_t core) { Require(hooks[core] == hook, "unregister exact registered callback"); ++deregistrations; hooks[core] = nullptr; }
void observer_log(const char*, const char* format, ...) {
    Require(locks_held == 0, "logging outside observer locks");
    char output[2048]; va_list args; va_start(args, format); std::vsnprintf(output, sizeof(output), format, args); va_end(args);
    logs.emplace_back(output);
}

int main() {
    Registration(); ActualTickAndCatchup(); DropAndPrivatePrevious(); ThreeLocksAndFences();
    CrossingAndFreeze(); AnomaliesAndDeadline(); ClockAndSaturation(); SampleCrossesTransition();
    UnpublishedTailAndFinish(); SampleEntirelyBeforeNewWindow(); EpochDeadlineAndExhaustion();
    std::puts("PASS: 11 actual-module test groups");
}
