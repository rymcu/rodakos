#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
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
    };

    void Begin() {
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = std::make_shared<Instance>();
        last_sequence_ = 0;
        pending_.clear();
    }
    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        current_.reset();
        pending_.clear();
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
        if (pending_.size() >= 32) pending_.pop_front();
        pending_.push_back({instance, sequence, accepted, reason != nullptr ? reason : ""});
    }
    std::deque<Ack> Take() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::deque<Ack> pending;
        pending.swap(pending_);
        return pending;
    }

private:
    mutable std::mutex mutex_;
    InstancePtr current_;
    uint32_t last_sequence_ = 0;
    std::deque<Ack> pending_;
};

}  // namespace rodakos
