#include "phone_os/voice_prepare_priority_observer.h"
#include <freertos/task.h>

#include <climits>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace {
using namespace rodakos;
std::mutex observer_mutex;
int lock_depth = 0;
uintptr_t current_handle = 0x1234;
uint32_t current_priority = 4;
const char* current_name = "wake_notify";
int64_t now_us = 100;
uint32_t priority_calls = 0;
uint32_t name_calls = 0;
std::function<void()> priority_hook;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void Reset(uint32_t high_watermark = 0, uint32_t collisions = 0) {
    ResetVoicePreparePriorityObserverForHostTests(high_watermark, collisions);
    current_handle = 0x1234;
    current_priority = 4;
    current_name = "wake_notify";
    now_us = 100;
    priority_calls = name_calls = 0;
    priority_hook = {};
}

VoicePreparePrioritySnapshot Take() {
    VoicePreparePrioritySnapshot out;
    Require(TryTakeCompletedVoicePreparePrioritySnapshot(out), "completed record expected");
    return out;
}

void FourPointsAndRaii() {
    Reset();
    struct BusinessLock {
        BusinessLock() { current_priority = 5; }
        ~BusinessLock() { current_priority = 4; }
    };
    {
        VoicePreparePriorityScope observation;
        BusinessLock business_lock;
        Require(observation.Mark(VoicePreparePriorityStage::kOpenAcquired) ==
                    VoicePreparePriorityStatus::kOk, "open mark");
        Require(observation.Mark(VoicePreparePriorityStage::kCloudReturned) ==
                    VoicePreparePriorityStatus::kOk, "cloud mark");
    }
    const auto out = Take();
    Require(out.scope_id == 1 && out.count == 4 && out.flags == 0 && out.open_acquired,
            "four point snapshot");
    Require(std::strcmp(out.task_name, "wake_notify") == 0, "self name");
    Require(priority_calls == 4 && name_calls == 1, "bounded getter count");
    for (uint32_t i = 0; i < out.count; ++i) {
        Require(out.samples[i].self_handle == 0x1234, "current self identity");
        Require(out.samples[i].effective_priority == (i == 0 || i == 3 ? 4U : 5U),
                "scripted priority sequence");
        Require(out.samples[i].after_us == out.samples[i].before_us + 1, "getter envelope");
    }
    Require(out.samples[3].stage == VoicePreparePriorityStage::kOpenReleased,
            "RAII after business lock destruction");
}

void ActiveAndReadyCannotBeOverwritten() {
    Reset();
    const auto ticket = BeginVoicePreparePriority();
    VoicePreparePrioritySnapshot out;
    out.scope_id = 999;
    Require(!TryTakeCompletedVoicePreparePrioritySnapshot(out) && out.scope_id == 999,
            "active take leaves output unchanged");
    Require(BeginVoicePreparePriority().status == VoicePreparePriorityStatus::kBusy,
            "active collision");
    Require(EndVoicePreparePriority(ticket) == VoicePreparePriorityStatus::kOk, "end");
    Require(BeginVoicePreparePriority().status == VoicePreparePriorityStatus::kBusy,
            "ready collision");
    const auto calls = priority_calls;
    current_handle = 0x8888;
    out = Take();
    Require(out.scope_id == ticket.scope_id && out.count == 2 && out.rejected_begin_count == 2,
            "original ready record retained");
    Require(priority_calls == calls && name_calls == 1, "take never queries owner");
    Require(!TryTakeCompletedVoicePreparePrioritySnapshot(out), "take consumes ready state");
    const auto next = BeginVoicePreparePriority();
    Require(next.scope_id == ticket.scope_id + 1, "take does not reset identity watermark");
    EndVoicePreparePriority(next);
    Take();
}

void EarlyReturnScopesClose() {
    Reset();
    const auto no_open = []() { VoicePreparePriorityScope scope; return; };
    no_open();
    auto out = Take();
    Require(out.count == 2 && !out.open_acquired &&
                out.samples[1].stage == VoicePreparePriorityStage::kExitNoOpen,
            "no-open return is not an unlock claim");
    const auto cancelled = []() {
        VoicePreparePriorityScope scope;
        scope.Mark(VoicePreparePriorityStage::kOpenAcquired);
        return;
    };
    cancelled();
    out = Take();
    Require(out.count == 3 && out.open_acquired &&
                out.samples[2].stage == VoicePreparePriorityStage::kOpenReleased,
            "cancel after open still closes");
}

void NonOwnerAndStaleTicketsAreRejected() {
    Reset();
    auto ticket = BeginVoicePreparePriority();
    const auto calls = priority_calls;
    current_handle = 0x9999;
    Require(MarkVoicePreparePriority(ticket, VoicePreparePriorityStage::kOpenAcquired) ==
                VoicePreparePriorityStatus::kWrongOwner, "non-owner mark rejected");
    Require(EndVoicePreparePriority(ticket) == VoicePreparePriorityStatus::kWrongOwner,
            "non-owner end rejected");
    Require(priority_calls == calls, "non-owner not priority sampled");
    current_handle = 0x1234;
    EndVoicePreparePriority(ticket);
    Take();
    const auto next = BeginVoicePreparePriority();
    Require(EndVoicePreparePriority(ticket) == VoicePreparePriorityStatus::kStale,
            "old same-address ticket rejected");
    auto forged = next;
    forged.self_handle = 0x9999;
    Require(EndVoicePreparePriority(forged) == VoicePreparePriorityStatus::kWrongOwner,
            "ticket self identity checked");
    EndVoicePreparePriority(next);
    Take();
}

void StageOrderPreservesTheFourPointBudget() {
    Reset();
    const auto ticket = BeginVoicePreparePriority();
    Require(MarkVoicePreparePriority(ticket, VoicePreparePriorityStage::kCloudReturned) ==
                VoicePreparePriorityStatus::kInvalidStage, "cloud cannot precede open");
    MarkVoicePreparePriority(ticket, VoicePreparePriorityStage::kOpenAcquired);
    Require(MarkVoicePreparePriority(ticket, VoicePreparePriorityStage::kOpenAcquired) ==
                VoicePreparePriorityStatus::kInvalidStage, "duplicate open rejected");
    MarkVoicePreparePriority(ticket, VoicePreparePriorityStage::kCloudReturned);
    Require(MarkVoicePreparePriority(ticket, VoicePreparePriorityStage::kOpenReleased) ==
                VoicePreparePriorityStatus::kInvalidStage, "only End emits released");
    EndVoicePreparePriority(ticket);
    const auto out = Take();
    Require(out.count == 4 && priority_calls == 4 &&
                (out.flags & kVoicePreparePriorityInvalidOrder) != 0,
            "rejected marks consume no sample budget and remain visible");
}

void SamplingReservationRejectsTakeAndNestedEnd() {
    Reset();
    const auto ticket = BeginVoicePreparePriority();
    uint32_t hook_calls = 0;
    priority_hook = [&]() {
        ++hook_calls;
        VoicePreparePrioritySnapshot out;
        Require(!TryTakeCompletedVoicePreparePrioritySnapshot(out), "sample cannot be consumed");
        Require(EndVoicePreparePriority(ticket) == VoicePreparePriorityStatus::kBusy,
                "nested end cannot publish over an unfinished getter");
    };
    MarkVoicePreparePriority(ticket, VoicePreparePriorityStage::kOpenAcquired);
    priority_hook = {};
    EndVoicePreparePriority(ticket);
    Require(Take().count == 3 && hook_calls == 1, "reserved sample published once");
}

void ClockAndNameLimitsAreVisible() {
    Reset();
    current_name = "wake_\"\\0123456789_extra";
    const auto ticket = BeginVoicePreparePriority();
    now_us = 0;
    EndVoicePreparePriority(ticket);
    const auto out = Take();
    Require(out.task_name[15] == '\0' && std::strlen(out.task_name) == 15,
            "task name is bounded and terminated");
    Require((out.flags & kVoicePreparePriorityNameTruncated) != 0 &&
                (out.flags & kVoicePreparePriorityClockInvalid) != 0,
            "truncation and regressing clock retained");
}

void SaturatingIdentitiesAndCollisions() {
    Reset(UINT32_MAX - 1, UINT32_MAX - 1);
    const auto last = BeginVoicePreparePriority();
    Require(last.scope_id == UINT32_MAX, "last distinct ID accepted");
    BeginVoicePreparePriority();
    BeginVoicePreparePriority();
    EndVoicePreparePriority(last);
    Require(Take().rejected_begin_count == UINT32_MAX, "collision counter saturates");
    const auto exhausted = BeginVoicePreparePriority();
    Require(exhausted.scope_id == 0 && exhausted.status == VoicePreparePriorityStatus::kExhausted,
            "identity exhaustion cannot wrap or reuse");
}

void UnavailableSelfDoesNotCreateARecord() {
    Reset();
    current_handle = 0;
    Require(BeginVoicePreparePriority().status == VoicePreparePriorityStatus::kUnavailable,
            "unavailable self rejected");
    VoicePreparePrioritySnapshot out;
    Require(!TryTakeCompletedVoicePreparePrioritySnapshot(out) && priority_calls == 0,
            "no synthetic record");
}
}  // namespace

void PreparePriorityHostEnter(portMUX_TYPE*) { observer_mutex.lock(); ++lock_depth; }
void PreparePriorityHostExit(portMUX_TYPE*) { --lock_depth; observer_mutex.unlock(); }
TaskHandle_t xTaskGetCurrentTaskHandle() {
    Require(lock_depth == 0, "self getter inside observer lock");
    return reinterpret_cast<TaskHandle_t>(current_handle);
}
UBaseType_t uxTaskPriorityGet(TaskHandle_t task) {
    Require(lock_depth == 0 && task == nullptr, "priority getter must be unlocked and self-only");
    ++priority_calls;
    if (priority_hook) priority_hook();
    return current_priority;
}
const char* pcTaskGetName(TaskHandle_t task) {
    Require(lock_depth == 0 && task == nullptr, "name getter must be unlocked and self-only");
    ++name_calls;
    return current_name;
}
int64_t esp_timer_get_time() {
    Require(lock_depth == 0, "timer getter inside observer lock");
    return now_us++;
}

int main() {
    const std::pair<const char*, void (*)()> cases[] = {
        {"four points and RAII", FourPointsAndRaii},
        {"active and ready collisions", ActiveAndReadyCannotBeOverwritten},
        {"early returns", EarlyReturnScopesClose},
        {"non-owner and stale identity", NonOwnerAndStaleTicketsAreRejected},
        {"stage order and budget", StageOrderPreservesTheFourPointBudget},
        {"sampling reservation", SamplingReservationRejectsTakeAndNestedEnd},
        {"clock and bounded name", ClockAndNameLimitsAreVisible},
        {"saturating identity and collisions", SaturatingIdentitiesAndCollisions},
        {"unavailable self", UnavailableSelfDoesNotCreateARecord},
    };
    try {
        for (const auto& [name, run] : cases) { run(); std::cout << "PASS " << name << '\n'; }
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    std::cout << "9 cases passed; scripted getters do not simulate FreeRTOS priority inheritance\n";
    return 0;
}
