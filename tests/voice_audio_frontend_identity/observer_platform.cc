#include "observer_platform.h"
#include "observation_control.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace retirement_host { const char* CurrentTaskName(); }

namespace {
std::mutex hooks_mutex;
std::array<std::array<FrontendObserverTickHook, 4>, 2> hooks{};
thread_local void* tick_handle = nullptr;
std::atomic<unsigned> handle_reads{0};
std::atomic<void*> last_handle{nullptr};
std::atomic<bool> reject_capture_try{false};
std::atomic<unsigned> rejected_capture_tries{0};
}

bool frontend_observer_try_lock(FrontendObserverHostMux* mux, int) {
    if (std::strcmp(retirement_host::CurrentTaskName(), "voice_frontend") == 0 &&
        reject_capture_try.exchange(false)) {
        ++rejected_capture_tries;
        return false;
    }
    uint32_t expected = 0;
    return std::atomic_ref(mux->locked).compare_exchange_strong(expected, 1,
        std::memory_order_acquire, std::memory_order_relaxed);
}
void frontend_observer_lock(FrontendObserverHostMux* mux) {
    while (!frontend_observer_try_lock(mux, 0)) std::this_thread::yield();
}
void frontend_observer_unlock(FrontendObserverHostMux* mux) {
    if (std::atomic_ref(mux->locked).exchange(0, std::memory_order_release) != 1) std::abort();
}
int64_t frontend_observer_now_us() {
    return rodakos_test::afe_observation::NowUs();
}
int frontend_observer_scheduler_state() { return taskSCHEDULER_RUNNING; }
bool frontend_observer_cache_enabled() { return true; }
void* frontend_observer_current_handle() {
    ++handle_reads;
    last_handle = tick_handle;
    return tick_handle;
}
int frontend_observer_register_hook(FrontendObserverTickHook hook, UBaseType_t core) {
    std::lock_guard<std::mutex> lock(hooks_mutex);
    if (core >= hooks.size()) return -1;
    for (auto& slot : hooks[core]) {
        if (slot == hook) return ESP_OK;
        if (slot == nullptr) { slot = hook; return ESP_OK; }
    }
    return -1;
}
void frontend_observer_deregister_hook(FrontendObserverTickHook hook, UBaseType_t core) {
    std::lock_guard<std::mutex> lock(hooks_mutex);
    if (core >= hooks.size()) return;
    for (auto& slot : hooks[core]) if (slot == hook) slot = nullptr;
}
void frontend_observer_log(const char* tag, const char* format, ...) {
    char text[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    rodakos_test::afe_observation::OnLog(std::string(tag) + ": " + text);
}

namespace rodakos_test::frontend_observer {
void Tick(unsigned core, void* current_handle) {
    std::array<FrontendObserverTickHook, 4> callbacks;
    {
        std::lock_guard<std::mutex> lock(hooks_mutex);
        if (core >= hooks.size()) std::abort();
        callbacks = hooks[core];
    }
    tick_handle = current_handle;
    for (const auto hook : callbacks) if (hook != nullptr) hook();
    tick_handle = nullptr;
}
unsigned RegisteredHooks(unsigned core) {
    std::lock_guard<std::mutex> lock(hooks_mutex);
    if (core >= hooks.size()) return 0;
    unsigned count = 0;
    for (const auto hook : hooks[core]) if (hook != nullptr) ++count;
    return count;
}
unsigned CurrentHandleReads() { return handle_reads.load(); }
void* LastTickHandle() { return last_handle.load(); }
void FailNextCaptureTry() { reject_capture_try = true; }
unsigned RejectedCaptureTries() { return rejected_capture_tries.load(); }
}
