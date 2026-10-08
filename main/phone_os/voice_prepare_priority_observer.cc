#include "phone_os/voice_prepare_priority_observer.h"

#include <climits>
#include <esp_attr.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef RODAKOS_RELEASE_TESTS
#error "Prepare priority observations are TEST-only"
#endif

namespace rodakos {
namespace {
enum class SlotState { kEmpty, kActive, kReady };

struct Observer {
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    SlotState state = SlotState::kEmpty;
    bool sampling = false;
    uint32_t last_scope_id = 0;
    uint32_t rejected_begin_count = 0;
    VoicePreparePrioritySnapshot snapshot;
};

DRAM_ATTR Observer observer{};
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(Observer) <= 256);
#endif

class ObserverLock {
public:
    ObserverLock() { portENTER_CRITICAL(&observer.mux); }
    ~ObserverLock() { portEXIT_CRITICAL(&observer.mux); }
};

void IncrementSaturating(uint32_t& value) {
    if (value != UINT32_MAX) ++value;
}

uintptr_t CurrentSelf() {
    return reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle());
}

VoicePreparePrioritySample CaptureSelf(VoicePreparePriorityStage stage) {
    VoicePreparePrioritySample sample;
    sample.stage = stage;
    sample.before_us = esp_timer_get_time();
    sample.self_handle = CurrentSelf();
    sample.effective_priority = static_cast<uint32_t>(uxTaskPriorityGet(nullptr));
    sample.after_us = esp_timer_get_time();
    return sample;
}

VoicePreparePriorityStatus ValidateLocked(const VoicePreparePriorityTicket& ticket,
                                          uintptr_t current_self) {
    if (ticket.status != VoicePreparePriorityStatus::kOk || ticket.scope_id == 0)
        return VoicePreparePriorityStatus::kUnavailable;
    if (observer.state != SlotState::kActive ||
        observer.snapshot.scope_id != ticket.scope_id)
        return VoicePreparePriorityStatus::kStale;
    if (current_self == 0 || current_self != ticket.self_handle ||
        current_self != observer.snapshot.self_handle)
        return VoicePreparePriorityStatus::kWrongOwner;
    if (observer.sampling) return VoicePreparePriorityStatus::kBusy;
    return VoicePreparePriorityStatus::kOk;
}

VoicePreparePriorityStatus PublishSample(const VoicePreparePriorityTicket& ticket,
                                         const VoicePreparePrioritySample& sample,
                                         bool ending) {
    ObserverLock lock;
    if (observer.state != SlotState::kActive || !observer.sampling ||
        observer.snapshot.scope_id != ticket.scope_id)
        return VoicePreparePriorityStatus::kStale;
    auto& out = observer.snapshot;
    if (sample.self_handle != out.self_handle)
        out.flags |= kVoicePreparePriorityIdentityInvalid;
    if (sample.before_us < 0 || sample.after_us < sample.before_us ||
        (out.count != 0 && sample.before_us < out.samples[out.count - 1].after_us))
        out.flags |= kVoicePreparePriorityClockInvalid;
    if (out.count < kVoicePreparePrioritySampleLimit) {
        out.samples[out.count++] = sample;
    } else {
        out.flags |= kVoicePreparePriorityOverflow;
    }
    if (sample.stage == VoicePreparePriorityStage::kOpenAcquired) out.open_acquired = true;
    observer.sampling = false;
    if (ending) observer.state = SlotState::kReady;
    return VoicePreparePriorityStatus::kOk;
}
}  // namespace

VoicePreparePriorityTicket BeginVoicePreparePriority() {
    VoicePreparePriorityTicket ticket;
    ticket.self_handle = CurrentSelf();
    if (ticket.self_handle == 0) return ticket;
    {
        ObserverLock lock;
        if (observer.state != SlotState::kEmpty) {
            IncrementSaturating(observer.rejected_begin_count);
            ticket.status = VoicePreparePriorityStatus::kBusy;
            return ticket;
        }
        if (observer.last_scope_id == UINT32_MAX) {
            ticket.status = VoicePreparePriorityStatus::kExhausted;
            return ticket;
        }
        ticket.scope_id = ++observer.last_scope_id;
        ticket.status = VoicePreparePriorityStatus::kOk;
        observer.snapshot = {};
        observer.snapshot.scope_id = ticket.scope_id;
        observer.snapshot.self_handle = ticket.self_handle;
        observer.state = SlotState::kActive;
        observer.sampling = true;
    }

    char name[kVoicePreparePriorityTaskNameBytes]{};
    bool truncated = false;
    const char* self_name = pcTaskGetName(nullptr);
    if (self_name != nullptr) {
        uint32_t index = 0;
        while (index + 1 < kVoicePreparePriorityTaskNameBytes && self_name[index] != '\0') {
            name[index] = self_name[index];
            ++index;
        }
        truncated = self_name[index] != '\0';
    }
    const auto sample = CaptureSelf(VoicePreparePriorityStage::kPrepareBegin);
    {
        ObserverLock lock;
        for (uint32_t index = 0; index < kVoicePreparePriorityTaskNameBytes; ++index)
            observer.snapshot.task_name[index] = name[index];
        if (truncated) observer.snapshot.flags |= kVoicePreparePriorityNameTruncated;
    }
    PublishSample(ticket, sample, false);
    return ticket;
}

VoicePreparePriorityStatus MarkVoicePreparePriority(const VoicePreparePriorityTicket& ticket,
                                                   VoicePreparePriorityStage stage) {
    const uintptr_t self = CurrentSelf();
    {
        ObserverLock lock;
        const auto status = ValidateLocked(ticket, self);
        if (status != VoicePreparePriorityStatus::kOk) return status;
        const bool valid = (stage == VoicePreparePriorityStage::kOpenAcquired &&
                            observer.snapshot.count == 1 && !observer.snapshot.open_acquired) ||
                           (stage == VoicePreparePriorityStage::kCloudReturned &&
                            observer.snapshot.count == 2 && observer.snapshot.open_acquired);
        if (!valid) {
            observer.snapshot.flags |= kVoicePreparePriorityInvalidOrder;
            return VoicePreparePriorityStatus::kInvalidStage;
        }
        observer.sampling = true;
    }
    return PublishSample(ticket, CaptureSelf(stage), false);
}

VoicePreparePriorityStatus EndVoicePreparePriority(const VoicePreparePriorityTicket& ticket) {
    const uintptr_t self = CurrentSelf();
    VoicePreparePriorityStage stage;
    {
        ObserverLock lock;
        const auto status = ValidateLocked(ticket, self);
        if (status != VoicePreparePriorityStatus::kOk) return status;
        stage = observer.snapshot.open_acquired ? VoicePreparePriorityStage::kOpenReleased
                                               : VoicePreparePriorityStage::kExitNoOpen;
        observer.sampling = true;
    }
    return PublishSample(ticket, CaptureSelf(stage), true);
}

bool TryTakeCompletedVoicePreparePrioritySnapshot(VoicePreparePrioritySnapshot& out) {
    ObserverLock lock;
    if (observer.state != SlotState::kReady || observer.sampling) return false;
    out = observer.snapshot;
    out.rejected_begin_count = observer.rejected_begin_count;
    observer.state = SlotState::kEmpty;
    return true;
}

VoicePreparePriorityScope::VoicePreparePriorityScope() : ticket_(BeginVoicePreparePriority()) {}
VoicePreparePriorityScope::~VoicePreparePriorityScope() {
    if (ticket_.status == VoicePreparePriorityStatus::kOk) EndVoicePreparePriority(ticket_);
}
VoicePreparePriorityStatus VoicePreparePriorityScope::Mark(VoicePreparePriorityStage stage) {
    return MarkVoicePreparePriority(ticket_, stage);
}

#ifdef RODAKOS_PREPARE_PRIORITY_OBSERVER_HOST_TEST
void ResetVoicePreparePriorityObserverForHostTests(uint32_t last_scope_id,
                                                 uint32_t rejected_begin_count) {
    ObserverLock lock;
    observer.state = SlotState::kEmpty;
    observer.sampling = false;
    observer.last_scope_id = last_scope_id;
    observer.rejected_begin_count = rejected_begin_count;
    observer.snapshot = {};
}
#endif

}  // namespace rodakos
