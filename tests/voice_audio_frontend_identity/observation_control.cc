#include "observation_control.h"
#include "task_retirement_host.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace rodakos_test::afe_observation {
namespace {
std::mutex mutex;
std::condition_variable changed;
void* target_semaphore = nullptr;
std::string target_task;
SemaphorePoint target_point;
std::function<bool()> semaphore_predicate;
bool semaphore_blocked = false;
bool semaphore_released = false;
unsigned semaphore_version = 0;
std::string target_log;
bool log_blocked = false;
bool log_released = false;
std::vector<std::string> messages;
std::atomic<int64_t> time_offset{0};
void* target_critical = nullptr;
std::string critical_task;
bool critical_blocked = false;
bool critical_released = false;
}

void Reset() {
    std::lock_guard<std::mutex> lock(mutex);
    target_semaphore = nullptr;
    target_task.clear();
    semaphore_predicate = {};
    semaphore_blocked = semaphore_released = false;
    ++semaphore_version;
    target_log.clear();
    log_blocked = log_released = false;
    messages.clear();
    time_offset = 0;
    target_critical = nullptr;
    critical_task.clear();
    critical_blocked = critical_released = false;
}
void ArmSemaphore(void* semaphore, const char* task, SemaphorePoint point,
                  std::function<bool()> predicate) {
    std::lock_guard<std::mutex> lock(mutex);
    target_semaphore = semaphore;
    target_task = task;
    target_point = point;
    semaphore_predicate = std::move(predicate);
    semaphore_blocked = semaphore_released = false;
    ++semaphore_version;
}
bool WaitSemaphoreBlocked() {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, std::chrono::seconds(3), [] { return semaphore_blocked; });
}
void ReleaseSemaphore() {
    std::lock_guard<std::mutex> lock(mutex);
    semaphore_released = true;
    changed.notify_all();
}
void OnSemaphore(void* semaphore, SemaphorePoint point) {
    std::unique_lock<std::mutex> lock(mutex);
    if (semaphore_released || semaphore_blocked || semaphore != target_semaphore ||
        point != target_point || target_task != retirement_host::CurrentTaskName()) return;
    const auto predicate = semaphore_predicate;
    const auto version = semaphore_version;
    lock.unlock();
    const bool matches = !predicate || predicate();
    lock.lock();
    if (!matches || version != semaphore_version || semaphore_released || semaphore_blocked) return;
    semaphore_blocked = true;
    changed.notify_all();
    changed.wait(lock, [] { return semaphore_released; });
}
void ArmLog(const std::string& contains) {
    std::lock_guard<std::mutex> lock(mutex);
    target_log = contains;
    log_blocked = log_released = false;
}
bool WaitLogBlocked() {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, std::chrono::seconds(3), [] { return log_blocked; });
}
void ReleaseLog() {
    std::lock_guard<std::mutex> lock(mutex);
    log_released = true;
    changed.notify_all();
}
void ReleaseAll() {
    std::lock_guard<std::mutex> lock(mutex);
    semaphore_released = log_released = critical_released = true;
    changed.notify_all();
}
void OnLog(const std::string& message) {
    std::unique_lock<std::mutex> lock(mutex);
    messages.push_back(message);
    changed.notify_all();
    if (target_log.empty() || log_blocked || log_released || message.find(target_log) == std::string::npos) return;
    log_blocked = true;
    changed.notify_all();
    changed.wait(lock, [] { return log_released; });
}
bool WaitLogContaining(const std::string& contains) {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, std::chrono::seconds(3), [&] {
        return std::any_of(messages.begin(), messages.end(), [&](const auto& message) {
            return message.find(contains) != std::string::npos;
        });
    });
}
std::vector<std::string> Logs() {
    std::lock_guard<std::mutex> lock(mutex);
    return messages;
}
int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() + time_offset.load();
}
void AdvanceUs(int64_t amount) { time_offset.fetch_add(amount); }
void ArmCritical(void* mux, const char* task) {
    std::lock_guard<std::mutex> lock(mutex);
    target_critical = mux;
    critical_task = task;
    critical_blocked = critical_released = false;
}
bool WaitCriticalBlocked() {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, std::chrono::seconds(3), [] { return critical_blocked; });
}
void ReleaseCritical() {
    std::lock_guard<std::mutex> lock(mutex);
    critical_released = true;
    changed.notify_all();
}
void OnCriticalEnter(void* mux) {
    std::unique_lock<std::mutex> lock(mutex);
    if (critical_released || critical_blocked || mux != target_critical ||
        critical_task != retirement_host::CurrentTaskName()) return;
    critical_blocked = true;
    changed.notify_all();
    changed.wait(lock, [] { return critical_released; });
}
}

extern "C" void __real_retirement_host_enter_critical(portMUX_TYPE* mux);
extern "C" void __wrap_retirement_host_enter_critical(portMUX_TYPE* mux) {
    rodakos_test::afe_observation::OnCriticalEnter(mux);
    __real_retirement_host_enter_critical(mux);
}
