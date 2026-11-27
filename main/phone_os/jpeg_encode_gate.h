#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <utility>

namespace rodakos {

class JpegEncodeGate {
public:
    class Permit {
    public:
        Permit() = default;
        ~Permit() {
            if (gate_ != nullptr) gate_->Release();
        }
        Permit(const Permit&) = delete;
        Permit& operator=(const Permit&) = delete;
        Permit(Permit&& other) noexcept : gate_(std::exchange(other.gate_, nullptr)) {}
        Permit& operator=(Permit&& other) noexcept {
            if (this != &other) {
                if (gate_ != nullptr) gate_->Release();
                gate_ = std::exchange(other.gate_, nullptr);
            }
            return *this;
        }
        explicit operator bool() const { return gate_ != nullptr; }

    private:
        friend class JpegEncodeGate;
        explicit Permit(JpegEncodeGate* gate) : gate_(gate) {}
        JpegEncodeGate* gate_ = nullptr;
    };

    static constexpr uint32_t kPriorityTimeoutMs = 600;

    Permit TryAcquireStream() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ || priority_waiters_ != 0) return {};
        active_ = true;
        return Permit(this);
    }

    Permit AcquirePriority(uint32_t timeout_ms = kPriorityTimeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);
        ++priority_waiters_;
        bool available;
        try {
            available = ready_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                        [this]() { return !active_; });
        } catch (...) {
            --priority_waiters_;
            throw;
        }
        --priority_waiters_;
        if (!available) return {};
        active_ = true;
        return Permit(this);
    }

private:
    void Release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_ = false;
        }
        ready_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    uint32_t priority_waiters_ = 0;
    bool active_ = false;
};

}  // namespace rodakos
