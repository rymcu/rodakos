#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>

#include <esp_heap_caps.h>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#include <thread>
#endif

namespace rodakos {

class JpegEncodeGate {
public:
    class ScratchLease {
    public:
        ScratchLease() = default;
        ~ScratchLease() {
            if (gate_ != nullptr) gate_->ReleaseScratchLease();
        }
        ScratchLease(const ScratchLease&) = delete;
        ScratchLease& operator=(const ScratchLease&) = delete;
        ScratchLease(ScratchLease&& other) noexcept : gate_(std::exchange(other.gate_, nullptr)) {}
        ScratchLease& operator=(ScratchLease&& other) noexcept {
            if (this != &other) {
                if (gate_ != nullptr) gate_->ReleaseScratchLease();
                gate_ = std::exchange(other.gate_, nullptr);
            }
            return *this;
        }
        explicit operator bool() const { return gate_ != nullptr; }

    private:
        friend class JpegEncodeGate;
        explicit ScratchLease(JpegEncodeGate* gate) : gate_(gate) {}
        JpegEncodeGate* gate_ = nullptr;
    };

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

        // Borrows a 16-byte aligned PSRAM input buffer owned by the gate. Retained across
        // encodes while any ScratchLease is active so streaming and shutter captures reuse
        // one contiguous block instead of fragmenting PSRAM between frames.
        uint8_t* AcquireInputBuffer(size_t bytes) {
            return gate_ != nullptr ? gate_->AcquireInputBufferForCurrentOwner(bytes) : nullptr;
        }

    private:
        friend class JpegEncodeGate;
        explicit Permit(JpegEncodeGate* gate) : gate_(gate) {}
        JpegEncodeGate* gate_ = nullptr;
    };

    static constexpr uint32_t kPriorityTimeoutMs = 600;

    ~JpegEncodeGate() {
        if (shared_input_ != nullptr) {
            heap_caps_free(shared_input_);
            shared_input_ = nullptr;
            shared_input_capacity_ = 0;
        }
    }

    ScratchLease AcquireScratchLease() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++scratch_leases_;
        return ScratchLease(this);
    }

    Permit TryAcquireStream() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ || priority_waiters_ != 0) return {};
        active_ = true;
        active_owner_ = CurrentOwner();
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
        active_owner_ = CurrentOwner();
        return Permit(this);
    }

    bool IsHeldByCurrentTask() {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_ && active_owner_ == CurrentOwner();
    }

    uint8_t* AcquireInputBufferForCurrentOwner(size_t bytes) {
        if (bytes == 0) return nullptr;
        uint8_t* existing = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!active_ || active_owner_ != CurrentOwner()) return nullptr;
            if (shared_input_ != nullptr && shared_input_capacity_ >= bytes) {
                return shared_input_;
            }
            existing = shared_input_;
        }
        // While active_ is true, only this permit holder can read, replace, or free shared_input_.
        uint8_t* replacement = AllocAligned(bytes);
        if (replacement == nullptr && existing != nullptr) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                shared_input_ = nullptr;
                shared_input_capacity_ = 0;
            }
            heap_caps_free(existing);
            existing = nullptr;
            replacement = AllocAligned(bytes);
        }
        if (replacement == nullptr) return nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shared_input_ = replacement;
            shared_input_capacity_ = bytes;
        }
        if (existing != nullptr) heap_caps_free(existing);
        return replacement;
    }

private:
#ifdef ESP_PLATFORM
    using OwnerId = TaskHandle_t;
    static OwnerId CurrentOwner() { return xTaskGetCurrentTaskHandle(); }
    static OwnerId EmptyOwner() { return nullptr; }
#else
    using OwnerId = std::thread::id;
    static OwnerId CurrentOwner() { return std::this_thread::get_id(); }
    static OwnerId EmptyOwner() { return {}; }
#endif

    static uint8_t* AllocAligned(size_t bytes) {
        auto* buffer = static_cast<uint8_t*>(
            heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buffer == nullptr) {
            buffer = static_cast<uint8_t*>(heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_8BIT));
        }
        return buffer;
    }

    void ReleaseScratchLease() {
        uint8_t* to_free = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (scratch_leases_ > 0) --scratch_leases_;
            if (scratch_leases_ == 0 && !active_ && shared_input_ != nullptr) {
                to_free = shared_input_;
                shared_input_ = nullptr;
                shared_input_capacity_ = 0;
            }
        }
        if (to_free != nullptr) heap_caps_free(to_free);
    }

    void Release() {
        uint8_t* to_free = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_ = false;
            active_owner_ = EmptyOwner();
            if (scratch_leases_ == 0 && shared_input_ != nullptr) {
                to_free = shared_input_;
                shared_input_ = nullptr;
                shared_input_capacity_ = 0;
            }
        }
        if (to_free != nullptr) heap_caps_free(to_free);
        ready_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    uint32_t priority_waiters_ = 0;
    uint32_t scratch_leases_ = 0;
    bool active_ = false;
    OwnerId active_owner_ = EmptyOwner();
    uint8_t* shared_input_ = nullptr;
    size_t shared_input_capacity_ = 0;
};

}  // namespace rodakos
