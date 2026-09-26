#include "phone_os/voice_assistant_reconnect_coordinator.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace rodakos {

namespace {

int64_t RetryDeadline(int64_t now_ms, uint32_t delay_ms) {
  const int64_t base_ms = std::max<int64_t>(0, now_ms);
  const int64_t delay = static_cast<int64_t>(delay_ms);
  return base_ms > std::numeric_limits<int64_t>::max() - delay
             ? std::numeric_limits<int64_t>::max()
             : base_ms + delay;
}

int64_t CurrentTime(int64_t fallback_ms, const VoiceReconnectClock &clock) {
  return clock ? clock() : fallback_ms;
}

} // namespace

VoiceAssistantReconnectCoordinator::VoiceAssistantReconnectCoordinator(
    RealtimeVoiceReconnectConfig config)
    : policy_(config) {}

bool VoiceAssistantReconnectCoordinator::Begin(uint32_t interaction_generation,
                                               uint32_t transport_generation) {
  if (interaction_generation == 0 || transport_generation == 0) {
    return false;
  }
  if (interaction_generation_ != 0) {
    policy_.Cancel(policy_.generation());
  }
  interaction_generation_ = 0;
  transport_generation_ = 0;
  last_opened_transport_generation_ = 0;
  retry_not_before_ms_ = -1;
  last_failure_ = {};
  if (!policy_.Begin(transport_generation)) {
    return false;
  }
  interaction_generation_ = interaction_generation;
  transport_generation_ = transport_generation;
  last_opened_transport_generation_ = transport_generation;
  retry_not_before_ms_ = -1;
  last_failure_ = {};
  return true;
}

VoiceAssistantReconnectResult VoiceAssistantReconnectCoordinator::HandleFailure(
    uint32_t interaction_generation, const VoiceTransportFailure &failure,
    int64_t now_ms) {
  if (interaction_generation == 0 ||
      interaction_generation != interaction_generation_ ||
      failure.transport_generation == 0 ||
      failure.transport_generation != policy_.generation()) {
    return IgnoredResult();
  }
  return ScheduleCurrentFailure(failure, now_ms);
}

bool VoiceAssistantReconnectCoordinator::RetryDue(
    uint32_t interaction_generation, int64_t now_ms) const {
  return interaction_generation != 0 &&
         interaction_generation == interaction_generation_ &&
         policy_.state() == RealtimeVoiceReconnectState::kWaiting &&
         retry_not_before_ms_ >= 0 && now_ms >= retry_not_before_ms_;
}

VoiceAssistantReconnectResult VoiceAssistantReconnectCoordinator::Attempt(
    uint32_t interaction_generation, int64_t now_ms,
    VoiceAssistantTransport &transport, const std::string &wake_word,
    VoiceOpenGuard can_continue, VoiceReconnectClock clock,
    VoiceReconnectOpened on_opened) {
  if (!RetryDue(interaction_generation, now_ms)) {
    return IgnoredResult();
  }

  const uint32_t policy_generation = policy_.generation();
  if (!policy_.BeginRetry(policy_generation)) {
    return IgnoredResult();
  }
  retry_not_before_ms_ = -1;

  if (!CanContinue(can_continue)) {
    return CancelCurrent();
  }

  CloseTransport(transport);
  if (!CanContinue(can_continue)) {
    return CancelCurrent();
  }

  if (!transport.Start()) {
    const VoiceTransportFailure failure =
        NormalizeFailure(transport.last_failure(), "transport_start_failed",
                         "Voice transport failed to start", policy_generation);
    CloseTransport(transport);
    if (!CanContinue(can_continue) ||
        failure.kind == VoiceTransportFailureKind::kCancelled) {
      return CancelCurrent();
    }
    return ScheduleCurrentFailure(failure, CurrentTime(now_ms, clock));
  }
  if (!CanContinue(can_continue)) {
    CloseTransport(transport);
    return CancelCurrent();
  }

  if (!transport.OpenAudioChannel(can_continue)) {
    const VoiceTransportFailure failure =
        NormalizeFailure(transport.last_failure(), "transport_open_failed",
                         "Voice transport failed to open", policy_generation);
    CloseTransport(transport);
    if (!CanContinue(can_continue) ||
        failure.kind == VoiceTransportFailureKind::kCancelled) {
      return CancelCurrent();
    }
    return ScheduleCurrentFailure(failure, CurrentTime(now_ms, clock));
  }

  const uint32_t opened_generation = transport.connection_generation();
  if (!CanContinue(can_continue)) {
    CloseTransport(transport);
    return CancelCurrent();
  }
  if (opened_generation == 0 ||
      opened_generation == last_opened_transport_generation_) {
    VoiceTransportFailure failure;
    failure.kind = VoiceTransportFailureKind::kProtocol;
    failure.code = "reconnect_generation_not_advanced";
    failure.message = "Voice transport generation did not advance";
    failure.transport_generation = opened_generation;
    CloseTransport(transport);
    return ScheduleCurrentFailure(failure, CurrentTime(now_ms, clock));
  }
  last_opened_transport_generation_ = opened_generation;
  if (on_opened && !on_opened(opened_generation)) {
    CloseTransport(transport);
    return CancelCurrent();
  }

  if (!wake_word.empty() &&
      !transport.SendWakeWordDetected(wake_word, opened_generation)) {
    const VoiceTransportFailure failure = NormalizeOpenedFailure(
        transport.last_failure(), "wake_restore_failed",
        "Voice wake context could not be restored", opened_generation);
    CloseTransport(transport);
    if (!CanContinue(can_continue) ||
        failure.kind == VoiceTransportFailureKind::kCancelled) {
      return CancelCurrent();
    }
    return ScheduleCurrentFailure(failure, CurrentTime(now_ms, clock));
  }

  if (!transport.SendStartListening(VoiceListeningMode::kRealtime,
                                    opened_generation)) {
    const VoiceTransportFailure failure = NormalizeOpenedFailure(
        transport.last_failure(), "input_start_restore_failed",
        "Voice input could not be restored", opened_generation);
    CloseTransport(transport);
    if (!CanContinue(can_continue) ||
        failure.kind == VoiceTransportFailureKind::kCancelled) {
      return CancelCurrent();
    }
    return ScheduleCurrentFailure(failure, CurrentTime(now_ms, clock));
  }
  if (!CanContinue(can_continue)) {
    CloseTransport(transport);
    return CancelCurrent();
  }

  if (!policy_.CompleteAttempt(policy_generation, true) ||
      !policy_.Cancel(policy_generation) || !policy_.Begin(opened_generation)) {
    VoiceTransportFailure failure;
    failure.kind = VoiceTransportFailureKind::kProtocol;
    failure.code = "reconnect_commit_failed";
    failure.message = "Voice reconnect state could not be committed";
    failure.transport_generation = opened_generation;
    CloseTransport(transport);
    last_failure_ = failure;
    transport_generation_ = 0;
    retry_not_before_ms_ = -1;
    VoiceAssistantReconnectResult result = IgnoredResult();
    result.outcome = VoiceAssistantReconnectOutcome::kTerminal;
    result.failure = failure;
    return result;
  }

  transport_generation_ = opened_generation;
  retry_not_before_ms_ = -1;
  last_failure_ = {};

  VoiceAssistantReconnectResult result;
  result.outcome = VoiceAssistantReconnectOutcome::kReconnected;
  result.interaction_generation = interaction_generation_;
  result.policy_generation = policy_.generation();
  result.transport_generation = opened_generation;
  return result;
}

bool VoiceAssistantReconnectCoordinator::Cancel(
    uint32_t interaction_generation) {
  if (interaction_generation == 0 ||
      interaction_generation != interaction_generation_) {
    return false;
  }
  CancelCurrent();
  return true;
}

VoiceAssistantReconnectResult
VoiceAssistantReconnectCoordinator::ScheduleCurrentFailure(
    const VoiceTransportFailure &failure, int64_t now_ms) {
  const uint32_t policy_generation = policy_.generation();
  if (failure.kind == VoiceTransportFailureKind::kCancelled) {
    return CancelCurrent();
  }

  const RealtimeVoiceReconnectPlan plan =
      policy_.OnFailure(policy_generation, failure.retryable);
  if (!plan.accepted) {
    return IgnoredResult();
  }

  last_failure_ = failure;
  transport_generation_ = 0;
  retry_not_before_ms_ =
      plan.should_retry ? RetryDeadline(now_ms, plan.delay_ms) : -1;

  VoiceAssistantReconnectResult result;
  result.outcome = plan.should_retry
                       ? VoiceAssistantReconnectOutcome::kWaiting
                       : VoiceAssistantReconnectOutcome::kTerminal;
  result.interaction_generation = interaction_generation_;
  result.policy_generation = policy_generation;
  result.attempt = plan.attempt;
  result.delay_ms = plan.delay_ms;
  result.failure = failure;
  return result;
}

VoiceAssistantReconnectResult
VoiceAssistantReconnectCoordinator::CancelCurrent() {
  const uint32_t interaction_generation = interaction_generation_;
  const uint32_t policy_generation = policy_.generation();
  policy_.Cancel(policy_generation);
  interaction_generation_ = 0;
  transport_generation_ = 0;
  last_opened_transport_generation_ = 0;
  retry_not_before_ms_ = -1;
  last_failure_ = {};

  VoiceAssistantReconnectResult result;
  result.outcome = VoiceAssistantReconnectOutcome::kCancelled;
  result.interaction_generation = interaction_generation;
  result.policy_generation = policy_generation;
  return result;
}

VoiceAssistantReconnectResult
VoiceAssistantReconnectCoordinator::IgnoredResult() const {
  VoiceAssistantReconnectResult result;
  result.interaction_generation = interaction_generation_;
  result.policy_generation = policy_.generation();
  result.transport_generation = transport_generation_;
  result.failure = last_failure_;
  return result;
}

VoiceTransportFailure VoiceAssistantReconnectCoordinator::NormalizeFailure(
    VoiceTransportFailure failure, const char *fallback_code,
    const char *fallback_message, uint32_t fallback_generation) const {
  if (failure.kind == VoiceTransportFailureKind::kNone) {
    failure.kind = VoiceTransportFailureKind::kResource;
    failure.retryable = false;
  }
  if (failure.code.empty()) {
    failure.code = fallback_code;
  }
  if (failure.message.empty()) {
    failure.message = fallback_message;
  }
  if (failure.transport_generation == 0) {
    failure.transport_generation = fallback_generation;
  }
  return failure;
}

VoiceTransportFailure VoiceAssistantReconnectCoordinator::NormalizeOpenedFailure(
    VoiceTransportFailure failure, const char *fallback_code,
    const char *fallback_message, uint32_t opened_generation) const {
  failure = NormalizeFailure(std::move(failure), fallback_code, fallback_message,
                             opened_generation);
  if (failure.transport_generation == opened_generation) {
    return failure;
  }

  VoiceTransportFailure mismatch;
  mismatch.kind = VoiceTransportFailureKind::kProtocol;
  mismatch.code = "reconnect_failure_generation_mismatch";
  mismatch.message = "Voice reconnect failure does not belong to the opened channel";
  mismatch.transport_generation = opened_generation;
  return mismatch;
}

bool VoiceAssistantReconnectCoordinator::CanContinue(
    const VoiceOpenGuard &can_continue) const {
  return !can_continue || can_continue();
}

void VoiceAssistantReconnectCoordinator::CloseTransport(
    VoiceAssistantTransport &transport) const {
  transport.CloseAudioChannel();
  transport.WaitForAudioChannelClosed();
}

} // namespace rodakos
