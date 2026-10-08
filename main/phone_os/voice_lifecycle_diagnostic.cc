#include "phone_os/voice_lifecycle_diagnostic.h"

#ifdef RODAKOS_RELEASE_TESTS
#include <cstdio>
#include <inttypes.h>
#include <limits>

namespace rodakos {
namespace {
constexpr std::string_view kCommand = "RODAK_RELEASE_TEST_V1 voice_cycle";
bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.substr(0, prefix.size()) == prefix;
}
bool ParseId(std::string_view text, uint32_t& id) {
    if (text.empty() || text.size() > 10 || text.front() == '0') return false;
    uint32_t value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') return false;
        const uint32_t digit = static_cast<uint32_t>(character - '0');
        if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    id = value;
    return true;
}
bool All(const VoiceLifecycleTasks& tasks) {
    return tasks.assistant && tasks.capture && tasks.supervisor;
}
bool None(const VoiceLifecycleTasks& tasks) {
    return !tasks.assistant && !tasks.capture && !tasks.supervisor;
}
const char* Boolean(bool value) { return value ? "true" : "false"; }
const char* Phase(VoiceLifecycleAssistantPhase phase) {
    switch (phase) {
        case VoiceLifecycleAssistantPhase::kIdle: return "idle";
        case VoiceLifecycleAssistantPhase::kConnecting: return "connecting";
        case VoiceLifecycleAssistantPhase::kListening: return "listening";
        case VoiceLifecycleAssistantPhase::kSpeaking: return "speaking";
        case VoiceLifecycleAssistantPhase::kError: return "error";
        default: return "unknown";
    }
}
}

VoiceLifecycleDiagnostic::VoiceLifecycleDiagnostic(VoiceLifecycleDependencies dependencies)
    : dependencies_(dependencies) {}

bool VoiceLifecycleDiagnostic::HandleSerialLine(std::string_view line) {
    if (!StartsWith(line, kCommand)) return false;
    uint32_t id = 0;
    if (line.size() <= kCommand.size() || line[kCommand.size()] != ' ' ||
        !ParseId(line.substr(kCommand.size() + 1), id)) {
        Emit({.phase = "rejected", .result = "rejected", .reason = "invalid_id"});
        return true;
    }
    Enqueue(id);
    return true;
}

void VoiceLifecycleDiagnostic::Enqueue(uint32_t id) {
    const char* rejection = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (slot_ != Slot::kIdle) rejection = "slot_busy";
        else if (id <= accepted_id_watermark_) rejection = "stale_id";
        else { slot_ = Slot::kAccepting; pending_id_ = id; }
    }
    if (rejection) {
        Emit({.id = id, .result = "rejected", .reason = rejection});
        return;
    }
    if (!dependencies_.busy_reason || !dependencies_.snapshot || !dependencies_.wake_state ||
        !dependencies_.deinit || !dependencies_.restart || !dependencies_.emit) {
        Emit({.id = id, .result = "rejected", .reason = "unavailable"});
        CompleteSlot();
        return;
    }
    if (const auto* busy = dependencies_.busy_reason(dependencies_.context)) {
        Emit({.id = id, .result = "rejected", .reason = busy});
        CompleteSlot();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepted_id_watermark_ = id;
    }
    // kAccepting remains busy while admission is printed. main cannot publish
    // begin/result before the serial caller's accepted record is complete.
    Emit({.id = id, .phase = "accepted", .result = "queued"});
    {
        std::lock_guard<std::mutex> lock(mutex_);
        slot_ = Slot::kPending;
    }
}

bool VoiceLifecycleDiagnostic::IsBusy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return slot_ != Slot::kIdle;
}

bool VoiceLifecycleDiagnostic::BlocksSerialMutation(std::string_view line) const {
    if (!IsBusy()) return false;
    constexpr std::string_view voice = "RODAK_VOICE_TEST_V1 ";
    if (StartsWith(line, voice)) {
        const auto command = line.substr(voice.size());
        return command != "aec_status" && !StartsWith(command, "aec_read ");
    }
    return StartsWith(line, "RODAK_RELEASE_TEST_V1 ") ||
           StartsWith(line, "RODAK_APP_LAUNCH_V1 ") ||
           StartsWith(line, "RODAK_PROVISION_V1 ");
}

bool VoiceLifecycleDiagnostic::Pump() {
    VoiceLifecycleEvent event;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (slot_ != Slot::kPending) return false;
        slot_ = Slot::kExecuting;
        event.id = pending_id_;
    }
    if (const auto* busy = dependencies_.busy_reason(dependencies_.context)) {
        event.phase = "complete";
        event.result = "rejected";
        event.reason = busy;
        Emit(event);
        CompleteSlot();
        return true;
    }
    const auto before_wake = dependencies_.wake_state(dependencies_.context);
    event.enabled_before = before_wake.enabled;
    event.snapshot = dependencies_.snapshot(dependencies_.context);
    const auto before = event.snapshot.tasks;
    event.assistant_phase_before = event.snapshot.assistant_phase;
    event.assistant_stopping_before = event.snapshot.assistant_stopping;
    const bool three_task_target = event.enabled_before && All(before) && !event.assistant_stopping_before &&
                                  event.assistant_phase_before == VoiceLifecycleAssistantPhase::kListening;
    const bool idle_target = !before.assistant && before.supervisor && before.capture == event.enabled_before &&
                             !event.assistant_stopping_before &&
                             event.assistant_phase_before == VoiceLifecycleAssistantPhase::kIdle;
    event.scope = three_task_target ? "three_tasks" : idle_target ?
                  (event.enabled_before ? "idle" : "disabled_idle") : "partial";
    event.has_snapshot = true;
    event.phase = "before";
    Emit(event);

    dependencies_.deinit(dependencies_.context);
    // Do not call Wake GetState/IsEnabled here: both can initialize the service.
    event.snapshot = dependencies_.snapshot(dependencies_.context);
    event.stopped = None(event.snapshot.tasks);
    event.phase = "after";
    Emit(event);

    // Even failed absence must get exactly one best-effort restoration attempt.
    event.restart_returned = dependencies_.restart(dependencies_.context);
    const auto after_wake = dependencies_.wake_state(dependencies_.context);
    event.enabled_after = after_wake.enabled;
    event.listening_after = after_wake.listening;
    event.snapshot = dependencies_.snapshot(dependencies_.context);
    const auto recovered = event.snapshot.tasks;
    event.recovered = event.restart_returned && event.enabled_before == event.enabled_after &&
                      recovered.supervisor && !recovered.assistant &&
                      (event.enabled_before ? recovered.capture && event.listening_after
                                            : !recovered.capture && !event.listening_after);
    event.phase = "recovered";
    Emit(event);
    event.phase = "complete";
    if (!event.stopped) { event.result = "failed"; event.reason = "tasks_remain"; }
    else if (!event.recovered) { event.result = "failed"; event.reason = "restore_failed"; }
    else if (three_task_target) event.result = "three_task_pass";
    else if (idle_target) event.result = event.enabled_before ? "idle_cycle_pass" : "disabled_idle_cycle_pass";
    else { event.result = "failed"; event.reason = "incomplete_before"; }
    Emit(event);
    CompleteSlot();
    return true;
}

void VoiceLifecycleDiagnostic::CompleteSlot() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_id_ = 0;
    slot_ = Slot::kIdle;
}

void VoiceLifecycleDiagnostic::Emit(const VoiceLifecycleEvent& event) const {
    if (dependencies_.emit) dependencies_.emit(dependencies_.context, event);
}

void PrintVoiceLifecycleEvent(void*, const VoiceLifecycleEvent& event) {
    std::printf("RODAK_VOICE_CYCLE {\"id\":%" PRIu32 ",\"phase\":\"%s\",\"result\":\"%s\","
                "\"reason\":\"%s\",\"scope\":\"%s\",\"has_snapshot\":%s,"
                "\"assistant\":%s,\"capture\":%s,\"supervisor\":%s,"
                "\"assistant_phase\":\"%s\",\"assistant_stopping\":%s,"
                "\"assistant_phase_before\":\"%s\",\"assistant_stopping_before\":%s,"
                "\"uptime_ms\":%" PRIu64 ",\"internal_free\":%" PRIu32 ",\"internal_min\":%" PRIu32 ",\"internal_largest\":%" PRIu32 ","
                "\"psram_free\":%" PRIu32 ",\"psram_largest\":%" PRIu32 ","
                "\"enabled_before\":%s,\"enabled_after\":%s,\"listening_after\":%s,"
                "\"stopped\":%s,\"restart_returned\":%s,\"recovered\":%s}\n",
                event.id, event.phase, event.result, event.reason, event.scope,
                Boolean(event.has_snapshot), Boolean(event.snapshot.tasks.assistant),
                Boolean(event.snapshot.tasks.capture), Boolean(event.snapshot.tasks.supervisor),
                Phase(event.snapshot.assistant_phase), Boolean(event.snapshot.assistant_stopping),
                Phase(event.assistant_phase_before), Boolean(event.assistant_stopping_before),
                event.snapshot.uptime_ms, event.snapshot.internal_free,
                event.snapshot.internal_min, event.snapshot.internal_largest,
                event.snapshot.psram_free, event.snapshot.psram_largest,
                Boolean(event.enabled_before), Boolean(event.enabled_after), Boolean(event.listening_after),
                Boolean(event.stopped), Boolean(event.restart_returned), Boolean(event.recovered));
    std::fflush(stdout);
}

}  // namespace rodakos
#endif
