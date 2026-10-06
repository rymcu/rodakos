#pragma once
#include "freertos/task.h"
#include <atomic>
#include <mutex>
namespace camera_test {
inline std::recursive_mutex ui_mutex;
inline std::atomic<int> worker_lock_attempts{0};
inline std::atomic<uint32_t> last_lock_timeout{0};
inline std::atomic<bool> deny_worker_lock{true};
inline std::atomic<bool> fail_next_ui_lock{false};
}
extern "C" {
inline bool lvgl_port_lock(uint32_t timeout) {
    camera_test::last_lock_timeout = timeout;
    if (camera_test::capture_worker) {
        ++camera_test::worker_lock_attempts;
        if (camera_test::deny_worker_lock) return false;
    }
    if (timeout != 0 && camera_test::fail_next_ui_lock.exchange(false)) return false;
    camera_test::ui_mutex.lock();
    return true;
}
inline void lvgl_port_unlock() { camera_test::ui_mutex.unlock(); }
}
