#include "task-retirement.h"

#include <cstdlib>
#include <limits>
#include <new>
#include <utility>

#include <esp_heap_caps.h>
#include <freertos/portmacro.h>

namespace rodakos {
namespace {

enum class State : uint8_t { kEmpty, kReserved, kPublished, kFinished, kReaping, kReaped };
struct Record {
    TaskHandle_t handle;
    TaskRetirementBody body;
    void* context;
    uint32_t generation;
    uint32_t owner;
    uint32_t references;
    State state;
};
static_assert(sizeof(Record) <= 48, "The fixed retirement pool must stay small");

// Only the lock, pointer and admission counters reside in internal memory.
// The fixed record table is explicitly allocated in PSRAM and never freed.
portMUX_TYPE registry_lock = portMUX_INITIALIZER_UNLOCKED;
Record* records = nullptr;
uint32_t next_owner = 1;
uint32_t next_generation = 1;
bool initializing = false;

class RegistryLock {
public:
    RegistryLock() { portENTER_CRITICAL(&registry_lock); }
    ~RegistryLock() { portEXIT_CRITICAL(&registry_lock); }
    RegistryLock(const RegistryLock&) = delete;
    RegistryLock& operator=(const RegistryLock&) = delete;
};

void Require(bool condition) {
    // These are API/lifetime invariants, including in builds without asserts.
    if (!condition) std::abort();
}

bool EnsureRecords() {
    for (;;) {
        bool initialize = false;
        {
            RegistryLock lock;
            if (records != nullptr) return true;
            if (!initializing) {
                initializing = true;
                initialize = true;
            }
        }
        if (!initialize) {
            vTaskDelay(1);
            continue;
        }
        auto* allocated = static_cast<Record*>(heap_caps_calloc(
            kTaskRetirementSlots, sizeof(Record), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (allocated != nullptr) {
            for (uint32_t slot = 0; slot < kTaskRetirementSlots; ++slot) {
                new (&allocated[slot]) Record{};
            }
        }
        {
            RegistryLock lock;
            records = allocated;
            initializing = false;
        }
        return allocated != nullptr;
    }
}

Record& GetRecord(uint32_t slot, uint32_t generation) {
    Require(records != nullptr && slot < kTaskRetirementSlots && generation != 0);
    Record& record = records[slot];
    Require(record.generation == generation && record.state != State::kEmpty);
    return record;
}

void Retain(uint32_t slot, uint32_t generation) {
    if (generation == 0) return;
    RegistryLock lock;
    auto& record = GetRecord(slot, generation);
    Require(record.references != std::numeric_limits<uint32_t>::max());
    ++record.references;
}

void Release(uint32_t slot, uint32_t generation) {
    if (generation == 0) return;
    RegistryLock lock;
    auto& record = GetRecord(slot, generation);
    Require(record.references != 0);
    --record.references;
    if (record.references == 0 && record.state == State::kReaped) record = {};
}

bool TryReap(uint32_t slot, uint32_t generation, TaskHandle_t current) {
    TaskHandle_t handle = nullptr;
    {
        RegistryLock lock;
        // Pump holds no ticket. A competing reaper may already have freed and
        // reused this slot, so its observed generation must still match.
        auto& record = records[slot];
        if (record.generation != generation || record.state == State::kEmpty ||
            record.state == State::kReaped) return true;
        if (record.state != State::kFinished || record.handle == current) return false;
        Require(record.handle != nullptr);
        record.state = State::kReaping;
        handle = record.handle;
    }
    // No registry/service lock is held. The pinned IDF external-delete path
    // suspends the saved task and waits until it is current on no core before
    // deleting and freeing the WithCaps TCB/stack. There is one claimer only.
    vTaskDeleteWithCaps(handle);
    {
        RegistryLock lock;
        auto& record = GetRecord(slot, generation);
        Require(record.state == State::kReaping);
        record.handle = nullptr;
        record.state = State::kReaped;
        if (record.references == 0) record = {};
    }
    return true;
}

}  // namespace

TaskRetirementTicket::TaskRetirementTicket(const TaskRetirementTicket& other)
    : slot_(other.slot_), generation_(other.generation_) {
    Retain(slot_, generation_);
}

TaskRetirementTicket::TaskRetirementTicket(TaskRetirementTicket&& other) noexcept
    : slot_(other.slot_), generation_(std::exchange(other.generation_, 0)) {}

TaskRetirementTicket& TaskRetirementTicket::operator=(const TaskRetirementTicket& other) {
    if (this == &other) return *this;
    Retain(other.slot_, other.generation_);
    Reset();
    slot_ = other.slot_;
    generation_ = other.generation_;
    return *this;
}

TaskRetirementTicket& TaskRetirementTicket::operator=(TaskRetirementTicket&& other) noexcept {
    if (this == &other) return *this;
    Reset();
    slot_ = other.slot_;
    generation_ = std::exchange(other.generation_, 0);
    return *this;
}

TaskRetirementTicket::~TaskRetirementTicket() { Reset(); }

void TaskRetirementTicket::Reset() {
    Release(slot_, std::exchange(generation_, 0));
}

bool TaskRetirementTicket::IsCurrentTask() const {
    if (generation_ == 0) return false;
    const auto current = xTaskGetCurrentTaskHandle();
    RegistryLock lock;
    const auto& record = GetRecord(slot_, generation_);
    return (record.state == State::kPublished || record.state == State::kFinished) &&
        record.handle != nullptr && record.handle == current;
}

void TaskRetirementTicket::Join() const {
    if (generation_ == 0 || IsCurrentTask()) return;
    const auto current = xTaskGetCurrentTaskHandle();
    while (!TryReap(slot_, generation_, current)) vTaskDelay(1);
}

TaskRetirementOwner::TaskRetirementOwner() {
    RegistryLock lock;
    if (next_owner != std::numeric_limits<uint32_t>::max()) id_ = next_owner++;
}

TaskRetirementOwner::~TaskRetirementOwner() { Drain(); }

void TaskRetirementOwner::Close() {
    RegistryLock lock;
    closed_ = true;
}

bool TaskRetirementOwner::IsClosed() const {
    RegistryLock lock;
    return closed_;
}

void TaskRetirementOwner::Drain() {
    Close();
    if (id_ == 0) return;
    const auto current = xTaskGetCurrentTaskHandle();
    for (;;) {
        TaskRetirementTicket pending;
        {
            RegistryLock lock;
            if (records == nullptr) return;
            for (uint32_t slot = 0; slot < kTaskRetirementSlots; ++slot) {
                auto& record = records[slot];
                if (record.owner != id_ || record.state == State::kEmpty ||
                    record.state == State::kReaped) continue;
                // Self-destruction cannot safely release a worker's service.
                Require(record.state == State::kReaping || record.handle == nullptr ||
                        record.handle != current);
                Require(record.references != std::numeric_limits<uint32_t>::max());
                ++record.references;
                pending.slot_ = slot;
                pending.generation_ = record.generation;
                break;
            }
        }
        if (!pending) return;
        pending.Join();
    }
}

TaskRetirementTicket ReserveTaskRetirement(
    TaskRetirementOwner& owner, TaskRetirementBody body, void* context) {
    {
        RegistryLock lock;
        if (owner.closed_ || owner.id_ == 0 || body == nullptr) return {};
    }
    if (!EnsureRecords()) return {};
    RegistryLock lock;
    if (owner.closed_ || next_generation == std::numeric_limits<uint32_t>::max()) return {};
    for (uint32_t slot = 0; slot < kTaskRetirementSlots; ++slot) {
        auto& record = records[slot];
        if (record.state != State::kEmpty) continue;
        record = {nullptr, body, context, next_generation++, owner.id_, 1, State::kReserved};
        return TaskRetirementTicket(slot, record.generation);
    }
    return {};
}

void* TaskRetirementContext(const TaskRetirementTicket& ticket) {
    RegistryLock lock;
    auto& record = GetRecord(ticket.slot_, ticket.generation_);
    Require(record.state == State::kReserved);
    return &record;
}

void PublishTaskRetirement(const TaskRetirementTicket& ticket, TaskHandle_t handle) {
    RegistryLock lock;
    auto& record = GetRecord(ticket.slot_, ticket.generation_);
    Require(record.state == State::kReserved && handle != nullptr);
    record.handle = handle;
    record.state = State::kPublished;
}

void CancelTaskRetirement(const TaskRetirementTicket& ticket) {
    RegistryLock lock;
    auto& record = GetRecord(ticket.slot_, ticket.generation_);
    Require(record.state == State::kReserved && record.handle == nullptr);
    record.body = nullptr;
    record.context = nullptr;
    record.state = State::kReaped;
}

void TaskRetirementEntry(void* context) {
    auto* record = static_cast<Record*>(context);
    Require(record != nullptr);
    TaskRetirementBody body = nullptr;
    void* argument = nullptr;
    for (;;) {
        {
            RegistryLock lock;
            if (record->state == State::kPublished) {
                body = record->body;
                argument = record->context;
                break;
            }
            Require(record->state == State::kReserved);
        }
        vTaskDelay(1);
    }
    body(argument);
    // All business frames, their RAII locals and service unlocks have returned.
    // Nothing with an owning destructor may remain on the parked task stack.
    {
        RegistryLock lock;
        Require(record->state == State::kPublished);
        record->body = nullptr;
        record->context = nullptr;
        record->state = State::kFinished;
    }
    // A reaper can preempt immediately after the unlock above. No service or
    // record access follows, and no exit-time allocation/task creation occurs.
    for (;;) vTaskSuspend(nullptr);
}

void PumpTaskRetirements() {
    const auto current = xTaskGetCurrentTaskHandle();
    for (uint32_t slot = 0; slot < kTaskRetirementSlots; ++slot) {
        uint32_t generation = 0;
        {
            RegistryLock lock;
            if (records == nullptr) return;
            if (records[slot].state == State::kFinished) generation = records[slot].generation;
        }
        if (generation != 0) TryReap(slot, generation, current);
    }
}

}  // namespace rodakos
