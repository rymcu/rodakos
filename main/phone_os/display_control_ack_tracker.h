#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>

namespace rodakos {

// The opaque shared token is never reused while an old callback retains it.
// Neither a repeated sessionId nor a reset sequence number identifies a peer.
class DisplayControlAckTracker : public std::enable_shared_from_this<DisplayControlAckTracker> {
public:
    struct Instance {};
    using InstancePtr = std::shared_ptr<const Instance>;
    struct Ack {
        InstancePtr instance;
        uint32_t sequence = 0;
        bool accepted = false;
        std::string reason;
        int64_t first_attempt_us = -1;
        uint32_t attempts = 0;
    };

    void Begin() {
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = std::make_shared<Instance>();
        last_sequence_ = 0;
        pending_.clear();
        overflowed_ = false;
    }
    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        current_.reset();
        pending_.clear();
        overflowed_ = false;
    }
    InstancePtr Current() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_;
    }
    bool IsCurrent(const InstancePtr& instance) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return instance && instance == current_;
    }
    bool AdmitSequence(const InstancePtr& instance, uint32_t sequence) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!instance || instance != current_ || sequence == 0 || sequence <= last_sequence_) return false;
        last_sequence_ = sequence;
        return true;
    }
    std::function<void(bool, const char*)> MakeReply(const InstancePtr& instance,
                                                   uint32_t sequence, std::string kind) {
        const auto weak = weak_from_this();
        return [weak, instance, sequence, kind = std::move(kind)](bool accepted, const char* reason) {
            const char* fallback = accepted ? nullptr :
                (kind == "text" ? "text_target_unavailable" : "control_disabled_or_invalid");
            if (auto tracker = weak.lock()) {
                tracker->Queue(instance, sequence, accepted, reason != nullptr ? reason : fallback);
            }
        };
    }
    void Queue(const InstancePtr& instance, uint32_t sequence, bool accepted, const char* reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!instance || instance != current_) return;
        // Losing an admitted result makes an applied input look like a timeout.
        // Keep the oldest result and let the peer terminate on overflow.
        if (pending_.size() >= 32) { overflowed_ = true; return; }
        try {
            pending_.push_back({instance, sequence, accepted, reason != nullptr ? reason : ""});
        } catch (const std::bad_alloc&) {
            // No allocation is required to make the peer fail closed instead
            // of unwinding through LVGL or silently losing an applied result.
            overflowed_ = true;
        }
    }
    bool Overflowed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return overflowed_;
    }
    bool HasPending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !pending_.empty();
    }
    bool Front(Ack& ack, bool& no_memory) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.empty()) return false;
        const auto& head = pending_.front();
        ack.instance = head.instance;
        ack.sequence = head.sequence;
        ack.accepted = head.accepted;
        ack.first_attempt_us = head.first_attempt_us;
        ack.attempts = head.attempts;
        no_memory = false;
        try { ack.reason = head.reason; }
        catch (const std::bad_alloc&) { no_memory = true; }
        return true;
    }
    void MarkAttempt(const Ack& ack, int64_t now_us) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!MatchesFront(ack)) return;
        if (pending_.front().first_attempt_us < 0) pending_.front().first_attempt_us = now_us;
        ++pending_.front().attempts;
    }
    void Complete(const Ack& ack) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (MatchesFront(ack)) pending_.pop_front();
    }
    std::deque<Ack> Take() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::deque<Ack> pending;
        pending.swap(pending_);
        return pending;
    }

private:
    bool MatchesFront(const Ack& ack) const {
        return !pending_.empty() && ack.instance == current_ &&
               pending_.front().instance == ack.instance && pending_.front().sequence == ack.sequence;
    }
    mutable std::mutex mutex_;
    InstancePtr current_;
    uint32_t last_sequence_ = 0;
    std::deque<Ack> pending_;
    bool overflowed_ = false;
};

}  // namespace rodakos
