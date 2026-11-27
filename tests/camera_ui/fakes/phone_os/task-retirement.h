#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace camera_test {
inline std::atomic<bool> fail_retirement_reserve{false};
inline std::atomic<size_t> retirement_reserves{0};
inline std::atomic<size_t> retirement_publishes{0};
inline std::atomic<size_t> retirement_cancels{0};
inline std::atomic<size_t> retirement_joins{0};
inline void ResetRetirementStats() {
    fail_retirement_reserve = false;
    retirement_reserves = 0;
    retirement_publishes = 0;
    retirement_cancels = 0;
    retirement_joins = 0;
}
}

namespace rodakos {

using TaskRetirementBody = void (*)(void*);

namespace detail {
struct HostRetirementRecord {
    std::mutex mutex;
    std::condition_variable cv;
    TaskRetirementBody body = nullptr;
    void* context = nullptr;
    TaskHandle_t handle = nullptr;
    bool published = false;
    bool cancelled = false;
    bool finished = false;
};

inline std::mutex& LiveRecordsMutex() {
    static std::mutex mutex;
    return mutex;
}

inline std::vector<std::shared_ptr<HostRetirementRecord>>& LiveRecords() {
    static std::vector<std::shared_ptr<HostRetirementRecord>> records;
    return records;
}

inline std::shared_ptr<HostRetirementRecord> TakeLiveRecord(HostRetirementRecord* raw) {
    std::lock_guard<std::mutex> lock(LiveRecordsMutex());
    auto& records = LiveRecords();
    for (auto it = records.begin(); it != records.end(); ++it) {
        if (it->get() == raw) {
            auto owned = std::move(*it);
            records.erase(it);
            return owned;
        }
    }
    return {};
}
}  // namespace detail

class TaskRetirementOwner {
public:
    void Close() {}
    bool IsClosed() const { return false; }
    void Drain() {}
};

class TaskRetirementTicket {
public:
    TaskRetirementTicket() = default;
    explicit TaskRetirementTicket(std::shared_ptr<detail::HostRetirementRecord> record)
        : record_(std::move(record)) {}

    explicit operator bool() const { return record_ != nullptr; }
    bool IsCurrentTask() const { return false; }
    void Join() const {
        if (!record_) return;
        std::unique_lock<std::mutex> lock(record_->mutex);
        record_->cv.wait(lock, [&] { return record_->finished || record_->cancelled; });
        ++camera_test::retirement_joins;
    }
    void Reset() { record_.reset(); }

private:
    friend void* TaskRetirementContext(const TaskRetirementTicket&);
    friend void PublishTaskRetirement(const TaskRetirementTicket&, TaskHandle_t);
    friend void CancelTaskRetirement(const TaskRetirementTicket&);
    std::shared_ptr<detail::HostRetirementRecord> record_;
};

inline TaskRetirementTicket ReserveTaskRetirement(TaskRetirementOwner&, TaskRetirementBody body,
                                                  void* context) {
    ++camera_test::retirement_reserves;
    if (camera_test::fail_retirement_reserve || body == nullptr) return {};
    auto record = std::make_shared<detail::HostRetirementRecord>();
    record->body = body;
    record->context = context;
    {
        std::lock_guard<std::mutex> lock(detail::LiveRecordsMutex());
        detail::LiveRecords().push_back(record);
    }
    return TaskRetirementTicket(std::move(record));
}

inline void* TaskRetirementContext(const TaskRetirementTicket& ticket) {
    return ticket.record_.get();
}

inline void TaskRetirementEntry(void* raw_context) {
    auto record =
        detail::TakeLiveRecord(static_cast<detail::HostRetirementRecord*>(raw_context));
    if (!record) return;
    TaskRetirementBody body = nullptr;
    void* body_context = nullptr;
    {
        std::unique_lock<std::mutex> lock(record->mutex);
        record->cv.wait(lock, [&] { return record->published || record->cancelled; });
        if (record->cancelled) return;
        body = record->body;
        body_context = record->context;
    }
    if (body != nullptr) body(body_context);
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        record->finished = true;
    }
    record->cv.notify_all();
}

inline void PublishTaskRetirement(const TaskRetirementTicket& ticket, TaskHandle_t handle) {
    if (!ticket.record_) return;
    {
        std::lock_guard<std::mutex> lock(ticket.record_->mutex);
        ticket.record_->handle = handle;
        ticket.record_->published = true;
    }
    ++camera_test::retirement_publishes;
    ticket.record_->cv.notify_all();
}

inline void CancelTaskRetirement(const TaskRetirementTicket& ticket) {
    if (!ticket.record_) return;
    {
        std::lock_guard<std::mutex> lock(ticket.record_->mutex);
        ticket.record_->cancelled = true;
    }
    detail::TakeLiveRecord(ticket.record_.get());
    ++camera_test::retirement_cancels;
    ticket.record_->cv.notify_all();
}

inline void PumpTaskRetirements() {}

}  // namespace rodakos
