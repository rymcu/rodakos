#pragma once

#include <cstdint>

namespace rodakos {

// Host-testable reconnect decision state.  This policy only computes state and
// delay; it never creates a task, opens a socket, or starts an automatic retry.
enum class RealtimeVoiceReconnectState {
    kIdle,
    kActive,
    kWaiting,
    kAttempting,
    kTerminal,
    kCancelled,
};

struct RealtimeVoiceReconnectConfig {
    uint32_t max_attempts = 3;
    uint32_t initial_backoff_ms = 250;
    uint32_t max_backoff_ms = 8000;
};

struct RealtimeVoiceReconnectPlan {
    bool accepted = false;
    bool should_retry = false;
    bool terminal = false;
    uint32_t generation = 0;
    uint32_t attempt = 0;
    uint32_t delay_ms = 0;
};

// A caller owns transport scheduling and invokes this policy from its own
// lifecycle.  Generation zero is never accepted, and stale callbacks cannot
// schedule work for a newer generation.
class RealtimeVoiceReconnectPolicy {
public:
    explicit RealtimeVoiceReconnectPolicy(
        RealtimeVoiceReconnectConfig config = {});

    bool Begin(uint32_t generation);
    RealtimeVoiceReconnectPlan OnFailure(uint32_t generation, bool retryable);
    bool BeginRetry(uint32_t generation);
    bool CompleteAttempt(uint32_t generation, bool success);
    bool Cancel(uint32_t generation);

    RealtimeVoiceReconnectState state() const { return state_; }
    uint32_t generation() const { return generation_; }
    uint32_t attempt() const { return attempt_; }
    uint32_t next_backoff_ms() const { return next_backoff_ms_; }
    const RealtimeVoiceReconnectConfig& config() const { return config_; }

private:
    bool IsCurrent(uint32_t generation) const;
    void BecomeTerminal();

    RealtimeVoiceReconnectConfig config_;
    RealtimeVoiceReconnectState state_ = RealtimeVoiceReconnectState::kIdle;
    uint32_t generation_ = 0;
    uint32_t attempt_ = 0;
    uint32_t next_backoff_ms_ = 0;
};

}  // namespace rodakos
