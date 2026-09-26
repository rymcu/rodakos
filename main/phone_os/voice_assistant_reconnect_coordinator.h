#pragma once

#include "phone_os/realtime_voice_reconnect_policy.h"
#include "phone_os/voice_assistant_transport.h"

#include <cstdint>
#include <functional>
#include <string>

namespace rodakos {

enum class VoiceAssistantReconnectOutcome {
  kIgnored,
  kWaiting,
  kReconnected,
  kTerminal,
  kCancelled,
};

using VoiceReconnectClock = std::function<int64_t()>;
using VoiceReconnectOpened = std::function<bool(uint32_t transport_generation)>;

struct VoiceAssistantReconnectResult {
  VoiceAssistantReconnectOutcome outcome =
      VoiceAssistantReconnectOutcome::kIgnored;
  uint32_t interaction_generation = 0;
  uint32_t policy_generation = 0;
  uint32_t transport_generation = 0;
  uint32_t attempt = 0;
  uint32_t delay_ms = 0;
  VoiceTransportFailure failure;
};

// The service serializes coordinator calls. A reconnect attempt may block in
// the transport, so cancellation crosses that boundary through VoiceOpenGuard.
class VoiceAssistantReconnectCoordinator {
public:
  explicit VoiceAssistantReconnectCoordinator(
      RealtimeVoiceReconnectConfig config = {});

  bool Begin(uint32_t interaction_generation, uint32_t transport_generation);
  VoiceAssistantReconnectResult
  HandleFailure(uint32_t interaction_generation,
                const VoiceTransportFailure &failure, int64_t now_ms);
  bool RetryDue(uint32_t interaction_generation, int64_t now_ms) const;
  VoiceAssistantReconnectResult
  Attempt(uint32_t interaction_generation, int64_t now_ms,
          VoiceAssistantTransport &transport, const std::string &wake_word,
          VoiceOpenGuard can_continue = {}, VoiceReconnectClock clock = {},
          VoiceReconnectOpened on_opened = {});
  bool Cancel(uint32_t interaction_generation);

  uint32_t interaction_generation() const { return interaction_generation_; }
  uint32_t policy_generation() const { return policy_.generation(); }
  uint32_t transport_generation() const { return transport_generation_; }
  int64_t retry_not_before_ms() const { return retry_not_before_ms_; }
  RealtimeVoiceReconnectState state() const { return policy_.state(); }
  const VoiceTransportFailure &last_failure() const { return last_failure_; }

private:
  VoiceAssistantReconnectResult
  ScheduleCurrentFailure(const VoiceTransportFailure &failure, int64_t now_ms);
  VoiceAssistantReconnectResult CancelCurrent();
  VoiceAssistantReconnectResult IgnoredResult() const;
  VoiceTransportFailure NormalizeFailure(VoiceTransportFailure failure,
                                         const char *fallback_code,
                                         const char *fallback_message,
                                         uint32_t fallback_generation) const;
  VoiceTransportFailure NormalizeOpenedFailure(VoiceTransportFailure failure,
                                               const char *fallback_code,
                                               const char *fallback_message,
                                               uint32_t opened_generation) const;
  bool CanContinue(const VoiceOpenGuard &can_continue) const;
  void CloseTransport(VoiceAssistantTransport &transport) const;

  RealtimeVoiceReconnectPolicy policy_;
  uint32_t interaction_generation_ = 0;
  uint32_t transport_generation_ = 0;
  uint32_t last_opened_transport_generation_ = 0;
  int64_t retry_not_before_ms_ = -1;
  VoiceTransportFailure last_failure_;
};

} // namespace rodakos
