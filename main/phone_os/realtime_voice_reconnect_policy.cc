#include "phone_os/realtime_voice_reconnect_policy.h"

#include <algorithm>
#include <limits>

namespace rodakos {

namespace {

constexpr uint32_t kDefaultMaxAttempts = 3;
constexpr uint32_t kDefaultInitialBackoffMs = 250;
constexpr uint32_t kDefaultMaxBackoffMs = 8000;

RealtimeVoiceReconnectConfig NormalizeConfig(RealtimeVoiceReconnectConfig config) {
    if (config.max_attempts == 0) config.max_attempts = kDefaultMaxAttempts;
    if (config.initial_backoff_ms == 0) {
        config.initial_backoff_ms = kDefaultInitialBackoffMs;
    }
    if (config.max_backoff_ms == 0) config.max_backoff_ms = kDefaultMaxBackoffMs;
    config.max_backoff_ms = std::max(config.max_backoff_ms, config.initial_backoff_ms);
    return config;
}

}  // namespace

RealtimeVoiceReconnectPolicy::RealtimeVoiceReconnectPolicy(
    RealtimeVoiceReconnectConfig config)
    : config_(NormalizeConfig(config)) {}

bool RealtimeVoiceReconnectPolicy::Begin(uint32_t generation) {
    if (generation == 0 || state_ == RealtimeVoiceReconnectState::kActive ||
        state_ == RealtimeVoiceReconnectState::kWaiting ||
        state_ == RealtimeVoiceReconnectState::kAttempting ||
        generation == generation_) {
        return false;
    }
    generation_ = generation;
    attempt_ = 0;
    next_backoff_ms_ = 0;
    state_ = RealtimeVoiceReconnectState::kActive;
    return true;
}

RealtimeVoiceReconnectPlan RealtimeVoiceReconnectPolicy::OnFailure(
    uint32_t generation, bool retryable) {
    RealtimeVoiceReconnectPlan plan;
    plan.generation = generation;
    if (!IsCurrent(generation) ||
        (state_ != RealtimeVoiceReconnectState::kActive &&
         state_ != RealtimeVoiceReconnectState::kAttempting)) {
        return plan;
    }
    plan.accepted = true;
    if (!retryable || attempt_ >= config_.max_attempts) {
        BecomeTerminal();
        plan.terminal = true;
        plan.attempt = attempt_;
        return plan;
    }

    ++attempt_;
    plan.should_retry = true;
    plan.attempt = attempt_;
    next_backoff_ms_ = attempt_ == 1
                           ? config_.initial_backoff_ms
                           : std::min<uint32_t>(
                                 config_.max_backoff_ms,
                                 next_backoff_ms_ >
                                         config_.max_backoff_ms / 2
                                     ? config_.max_backoff_ms
                                     : next_backoff_ms_ * 2);
    plan.delay_ms = next_backoff_ms_;
    state_ = RealtimeVoiceReconnectState::kWaiting;
    return plan;
}

bool RealtimeVoiceReconnectPolicy::BeginRetry(uint32_t generation) {
    if (!IsCurrent(generation) || state_ != RealtimeVoiceReconnectState::kWaiting) {
        return false;
    }
    state_ = RealtimeVoiceReconnectState::kAttempting;
    return true;
}

bool RealtimeVoiceReconnectPolicy::CompleteAttempt(uint32_t generation,
                                                   bool success) {
    if (!IsCurrent(generation) ||
        state_ != RealtimeVoiceReconnectState::kAttempting) {
        return false;
    }
    if (!success) return false;
    state_ = RealtimeVoiceReconnectState::kActive;
    attempt_ = 0;
    next_backoff_ms_ = 0;
    return true;
}

bool RealtimeVoiceReconnectPolicy::Cancel(uint32_t generation) {
    if (!IsCurrent(generation) || state_ == RealtimeVoiceReconnectState::kIdle ||
        state_ == RealtimeVoiceReconnectState::kTerminal ||
        state_ == RealtimeVoiceReconnectState::kCancelled) {
        return false;
    }
    state_ = RealtimeVoiceReconnectState::kCancelled;
    next_backoff_ms_ = 0;
    return true;
}

bool RealtimeVoiceReconnectPolicy::IsCurrent(uint32_t generation) const {
    return generation != 0 && generation_ == generation;
}

void RealtimeVoiceReconnectPolicy::BecomeTerminal() {
    state_ = RealtimeVoiceReconnectState::kTerminal;
    next_backoff_ms_ = 0;
}

}  // namespace rodakos
