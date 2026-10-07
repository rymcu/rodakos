#pragma once

#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace rodakos {

constexpr uint32_t kTaskRetirementSlots = 16;
using TaskRetirementBody = void (*)(void*);

class TaskRetirementOwner;

// A reference to one exact generation. Copy only while the source ticket is
// protected by its service lock; the registry protects the record, not the
// C++ object holding this value. An empty ticket never waits.
class TaskRetirementTicket {
public:
    TaskRetirementTicket() = default;
    TaskRetirementTicket(const TaskRetirementTicket& other);
    TaskRetirementTicket(TaskRetirementTicket&& other) noexcept;
    TaskRetirementTicket& operator=(const TaskRetirementTicket& other);
    TaskRetirementTicket& operator=(TaskRetirementTicket&& other) noexcept;
    ~TaskRetirementTicket();

    explicit operator bool() const { return generation_ != 0; }
    bool IsCurrentTask() const;
    // External callers wait for the complete body return and WithCaps reclaim.
    // A worker joining itself returns immediately; it never deletes itself.
    void Join() const;
    void Reset();

private:
    friend class TaskRetirementOwner;
    friend TaskRetirementTicket ReserveTaskRetirement(
        TaskRetirementOwner&, TaskRetirementBody, void*);
    friend void* TaskRetirementContext(const TaskRetirementTicket&);
    friend void PublishTaskRetirement(const TaskRetirementTicket&, TaskHandle_t);
    friend void CancelTaskRetirement(const TaskRetirementTicket&);

    TaskRetirementTicket(uint32_t slot, uint32_t generation)
        : slot_(slot), generation_(generation) {}
    uint32_t slot_ = 0;
    uint32_t generation_ = 0;
};

// Registry records retain only this monotonic numeric identity, never an owner
// pointer. Services must Close(), request their workers to stop, then Drain()
// before releasing their mutexes or other business state. Destruction from an
// owned worker is invalid and Drain fails closed rather than pretending to join.
class TaskRetirementOwner {
public:
    TaskRetirementOwner();
    ~TaskRetirementOwner();
    TaskRetirementOwner(const TaskRetirementOwner&) = delete;
    TaskRetirementOwner& operator=(const TaskRetirementOwner&) = delete;
    void Close();
    bool IsClosed() const;
    void Drain();

private:
    friend TaskRetirementTicket ReserveTaskRetirement(
        TaskRetirementOwner&, TaskRetirementBody, void*);
    uint32_t id_ = 0;
    bool closed_ = false;
};

// Reserve before task creation. Records use a bounded PSRAM pool allocated on
// first admission, never on exit. Empty means no admission (including OOM).
TaskRetirementTicket ReserveTaskRetirement(
    TaskRetirementOwner& owner, TaskRetirementBody body, void* context);
void* TaskRetirementContext(const TaskRetirementTicket& ticket);
// Pass Entry and Context(ticket) to xTaskCreate*WithCaps. Save the service ticket
// before creating the task. Publish exactly once after receiving the handle;
// a task scheduled inside create cannot enter its body before publication.
void TaskRetirementEntry(void* context);
void PublishTaskRetirement(const TaskRetirementTicket& ticket, TaskHandle_t handle);
// Only for a failed creation: no worker may have been created for this record.
void CancelTaskRetirement(const TaskRetirementTicket& ticket);

// Call from a permanent external task. Reclaims only bodies already returned;
// IDF's external WithCaps path suspends and converges all cores before freeing.
// It can yield while cores converge and is not a hard latency guarantee.
void PumpTaskRetirements();

}  // namespace rodakos
