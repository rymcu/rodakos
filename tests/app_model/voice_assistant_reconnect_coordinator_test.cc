#include "phone_os/voice_assistant_reconnect_coordinator.h"
#include "test_framework.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

rodakos::VoiceTransportFailure Failure(rodakos::VoiceTransportFailureKind kind,
                                       const char *code, bool retryable,
                                       uint32_t generation) {
  rodakos::VoiceTransportFailure failure;
  failure.kind = kind;
  failure.code = code;
  failure.message = code;
  failure.retryable = retryable;
  failure.transport_generation = generation;
  return failure;
}

struct ScriptedAttempt {
  uint32_t generation = 0;
  bool open_succeeds = true;
  bool wake_succeeds = true;
  bool input_start_succeeds = true;
  rodakos::VoiceTransportFailure open_failure;
  rodakos::VoiceTransportFailure wake_failure;
  rodakos::VoiceTransportFailure input_start_failure;
};

class FakeVoiceTransport final : public rodakos::VoiceAssistantTransport {
public:
  bool Start() override {
    trace.push_back("start");
    if (on_start) {
      on_start();
    }
    if (!start_succeeds) {
      last_failure_ = start_failure;
    }
    return start_succeeds;
  }

  bool OpenAudioChannel(rodakos::VoiceOpenGuard can_continue = {}) override {
    trace.push_back("open");
    if (next_attempt_ >= attempts.size()) {
      last_failure_ = Failure(rodakos::VoiceTransportFailureKind::kResource,
                              "missing_scripted_attempt", false, generation_);
      return false;
    }
    active_attempt_ = attempts[next_attempt_++];
    if (on_open) {
      on_open();
    }
    if (can_continue && !can_continue()) {
      last_failure_ =
          Failure(rodakos::VoiceTransportFailureKind::kCancelled,
                  "open_cancelled", false, active_attempt_.generation);
      return false;
    }
    generation_ = active_attempt_.generation;
    if (!active_attempt_.open_succeeds) {
      last_failure_ = active_attempt_.open_failure;
      return false;
    }
    open_ = true;
    last_failure_ = {};
    return true;
  }

  void CloseAudioChannel() override {
    trace.push_back("close");
    open_ = false;
  }

  void WaitForAudioChannelClosed() override { trace.push_back("wait"); }

  bool IsAudioChannelOpen() const override { return open_; }
  uint32_t connection_generation() const override { return generation_; }

  bool SendAudio(const rodakos::VoiceAudioPacket &, uint32_t) override {
    return true;
  }

  bool SendStartListening(rodakos::VoiceListeningMode,
                          uint32_t expected_generation) override {
    trace.push_back("input:" + std::to_string(expected_generation));
    if (!active_attempt_.input_start_succeeds) {
      last_failure_ = active_attempt_.input_start_failure;
      return false;
    }
    return true;
  }

  bool SendStopListening(uint32_t) override { return true; }

  bool SendWakeWordDetected(const std::string &,
                            uint32_t expected_generation) override {
    trace.push_back("wake:" + std::to_string(expected_generation));
    if (!active_attempt_.wake_succeeds) {
      last_failure_ = active_attempt_.wake_failure;
      return false;
    }
    return true;
  }

  bool SendAbortSpeaking(rodakos::VoiceAbortReason, uint32_t,
                         uint32_t) override {
    return true;
  }

  bool SendVadStart(const char *, uint32_t, uint32_t, uint32_t,
                    uint32_t) override {
    return true;
  }

  bool SendVadEnd(const char *, uint32_t, uint32_t, uint32_t,
                  uint32_t) override {
    return true;
  }

  bool SendMcpMessage(const std::string &, uint32_t) override { return true; }
  void SetInboundHandler(rodakos::VoiceInboundHandler handler) override {
    inbound_handler_ = std::move(handler);
  }

  const char *name() const override { return "fake"; }
  std::string last_error() const override { return last_failure_.message; }
  rodakos::VoiceTransportFailure last_failure() const override {
    return last_failure_;
  }

  std::vector<ScriptedAttempt> attempts;
  std::vector<std::string> trace;
  std::function<void()> on_start;
  std::function<void()> on_open;
  bool start_succeeds = true;
  rodakos::VoiceTransportFailure start_failure;

private:
  size_t next_attempt_ = 0;
  uint32_t generation_ = 0;
  bool open_ = false;
  ScriptedAttempt active_attempt_;
  rodakos::VoiceTransportFailure last_failure_;
  rodakos::VoiceInboundHandler inbound_handler_;
};

void CheckTrace(const FakeVoiceTransport &transport,
                std::initializer_list<const char *> expected) {
  RODAK_CHECK_EQ(transport.trace.size(), expected.size());
  size_t index = 0;
  for (const char *event : expected) {
    RODAK_CHECK_EQ(transport.trace[index], std::string(event));
    ++index;
  }
}

} // namespace

RODAK_TEST("voice reconnect restores wake and input before committing the new "
           "generation") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({3, 25, 100});
  RODAK_CHECK(coordinator.Begin(7, 51));

  const auto waiting = coordinator.HandleFailure(
      7,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork,
              "socket_disconnected", true, 51),
      1000);
  RODAK_CHECK_EQ(waiting.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kWaiting);
  RODAK_CHECK_EQ(waiting.attempt, 1u);
  RODAK_CHECK_EQ(coordinator.retry_not_before_ms(), 1025);
  RODAK_CHECK_FALSE(coordinator.RetryDue(7, 1024));

  FakeVoiceTransport transport;
  ScriptedAttempt attempt;
  attempt.generation = 53;
  transport.attempts.push_back(attempt);
  uint32_t bound_generation = 0;
  const auto reconnected = coordinator.Attempt(
      7, 1025, transport, "wake-word", []() { return true; }, {},
      [&transport, &bound_generation](uint32_t generation) {
        bound_generation = generation;
        transport.trace.push_back("bind:" + std::to_string(generation));
        return true;
      });

  RODAK_CHECK_EQ(reconnected.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kReconnected);
  RODAK_CHECK_EQ(reconnected.transport_generation, 53u);
  RODAK_CHECK_EQ(bound_generation, 53u);
  RODAK_CHECK_EQ(coordinator.policy_generation(), 53u);
  RODAK_CHECK_EQ(coordinator.transport_generation(), 53u);
  RODAK_CHECK_EQ(coordinator.state(),
                 rodakos::RealtimeVoiceReconnectState::kActive);
  CheckTrace(transport,
             {"close", "wait", "start", "open", "bind:53", "wake:53", "input:53"});

  const auto stale = coordinator.HandleFailure(
      7,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "old_socket", true,
              51),
      2000);
  RODAK_CHECK_EQ(stale.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kIgnored);

  const auto next_failure = coordinator.HandleFailure(
      7,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "new_socket", true,
              53),
      2000);
  RODAK_CHECK_EQ(next_failure.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kWaiting);
  RODAK_CHECK_EQ(next_failure.attempt, 1u);
  RODAK_CHECK_EQ(next_failure.delay_ms, 25u);
}

RODAK_TEST(
    "voice reconnect applies capped backoff and exhausts its retry budget") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({3, 100, 250});
  RODAK_CHECK(coordinator.Begin(8, 61));
  const auto initial = coordinator.HandleFailure(
      8,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "disconnect", true,
              61),
      0);
  RODAK_CHECK_EQ(initial.delay_ms, 100u);

  FakeVoiceTransport transport;
  for (uint32_t generation : {63u, 65u, 67u}) {
    ScriptedAttempt attempt;
    attempt.generation = generation;
    attempt.open_succeeds = false;
    attempt.open_failure = Failure(rodakos::VoiceTransportFailureKind::kTimeout,
                                   "connect_timeout", true, generation);
    transport.attempts.push_back(attempt);
  }

  const auto second = coordinator.Attempt(8, 100, transport, "wake-word");
  RODAK_CHECK_EQ(second.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kWaiting);
  RODAK_CHECK_EQ(second.attempt, 2u);
  RODAK_CHECK_EQ(second.delay_ms, 200u);
  RODAK_CHECK_EQ(coordinator.retry_not_before_ms(), 300);

  const auto third = coordinator.Attempt(8, 300, transport, "wake-word");
  RODAK_CHECK_EQ(third.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kWaiting);
  RODAK_CHECK_EQ(third.attempt, 3u);
  RODAK_CHECK_EQ(third.delay_ms, 250u);
  RODAK_CHECK_EQ(coordinator.retry_not_before_ms(), 550);

  const auto terminal = coordinator.Attempt(8, 550, transport, "wake-word");
  RODAK_CHECK_EQ(terminal.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kTerminal);
  RODAK_CHECK_EQ(terminal.attempt, 3u);
  RODAK_CHECK_EQ(coordinator.state(),
                 rodakos::RealtimeVoiceReconnectState::kTerminal);
  RODAK_CHECK_EQ(
      std::count(transport.trace.begin(), transport.trace.end(), "open"), 3);
}

RODAK_TEST("voice reconnect backoff starts after a blocking attempt finishes") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({2, 10, 20});
  RODAK_CHECK(coordinator.Begin(14, 121));
  coordinator.HandleFailure(
      14,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "disconnect", true,
              121),
      0);

  FakeVoiceTransport transport;
  ScriptedAttempt attempt;
  attempt.generation = 123;
  attempt.open_succeeds = false;
  attempt.open_failure = Failure(rodakos::VoiceTransportFailureKind::kTimeout,
                                 "connect_timeout", true, 123);
  transport.attempts.push_back(attempt);

  int64_t clock_ms = 5000;
  const auto result = coordinator.Attempt(14, 10, transport, "wake-word", {},
                                          [&clock_ms]() { return clock_ms; });
  RODAK_CHECK_EQ(result.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kWaiting);
  RODAK_CHECK_EQ(result.delay_ms, 20u);
  RODAK_CHECK_EQ(coordinator.retry_not_before_ms(), 5020);
}

RODAK_TEST("voice reconnect rolls back a partially started transport") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({2, 10, 20});
  RODAK_CHECK(coordinator.Begin(18, 161));
  coordinator.HandleFailure(
      18,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "disconnect", true,
              161),
      0);

  FakeVoiceTransport transport;
  transport.start_succeeds = false;
  transport.start_failure =
      Failure(rodakos::VoiceTransportFailureKind::kResource,
              "start_failed", true, 161);

  const auto result = coordinator.Attempt(18, 10, transport, "wake-word");
  RODAK_CHECK_EQ(result.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kWaiting);
  RODAK_CHECK_EQ(result.attempt, 2u);
  RODAK_CHECK_FALSE(transport.IsAudioChannelOpen());
  CheckTrace(transport, {"close", "wait", "start", "close", "wait"});
}

RODAK_TEST("voice reconnect rolls back when cancelled after transport start") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({2, 10, 20});
  RODAK_CHECK(coordinator.Begin(21, 191));
  coordinator.HandleFailure(
      21,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "disconnect", true,
              191),
      0);

  bool current = true;
  FakeVoiceTransport transport;
  transport.on_start = [&current]() { current = false; };

  const auto result = coordinator.Attempt(
      21, 10, transport, "wake-word", [&current]() { return current; });
  RODAK_CHECK_EQ(result.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kCancelled);
  RODAK_CHECK_EQ(coordinator.interaction_generation(), 0u);
  RODAK_CHECK_FALSE(transport.IsAudioChannelOpen());
  CheckTrace(transport, {"close", "wait", "start", "close", "wait"});
}

RODAK_TEST("voice reconnect saturates an overflowing retry deadline") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({2, 10, 20});
  RODAK_CHECK(coordinator.Begin(19, 171));

  const auto result = coordinator.HandleFailure(
      19,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "disconnect", true,
              171),
      std::numeric_limits<int64_t>::max() - 5);
  RODAK_CHECK_EQ(result.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kWaiting);
  RODAK_CHECK_EQ(coordinator.retry_not_before_ms(),
                 std::numeric_limits<int64_t>::max());
  RODAK_CHECK_FALSE(
      coordinator.RetryDue(19, std::numeric_limits<int64_t>::max() - 1));
  RODAK_CHECK(
      coordinator.RetryDue(19, std::numeric_limits<int64_t>::max()));
}

RODAK_TEST(
    "voice reconnect cancel rejects waiting and stale interaction work") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({2, 10, 20});
  RODAK_CHECK(coordinator.Begin(9, 71));
  RODAK_CHECK_EQ(
      coordinator
          .HandleFailure(8,
                         Failure(rodakos::VoiceTransportFailureKind::kNetwork,
                                 "wrong_interaction", true, 71),
                         0)
          .outcome,
      rodakos::VoiceAssistantReconnectOutcome::kIgnored);
  RODAK_CHECK_EQ(
      coordinator
          .HandleFailure(9,
                         Failure(rodakos::VoiceTransportFailureKind::kNetwork,
                                 "wrong_transport", true, 70),
                         0)
          .outcome,
      rodakos::VoiceAssistantReconnectOutcome::kIgnored);

  RODAK_CHECK_EQ(
      coordinator
          .HandleFailure(9,
                         Failure(rodakos::VoiceTransportFailureKind::kNetwork,
                                 "disconnect", true, 71),
                         0)
          .outcome,
      rodakos::VoiceAssistantReconnectOutcome::kWaiting);
  RODAK_CHECK_FALSE(coordinator.Cancel(8));
  RODAK_CHECK(coordinator.Cancel(9));
  RODAK_CHECK_FALSE(coordinator.RetryDue(9, 100));

  FakeVoiceTransport transport;
  RODAK_CHECK_EQ(coordinator.Attempt(9, 100, transport, "wake-word").outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kIgnored);
  RODAK_CHECK(transport.trace.empty());
}

RODAK_TEST(
    "voice reconnect stops immediately for a nonretryable server failure") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({3, 10, 40});
  RODAK_CHECK(coordinator.Begin(15, 131));

  const auto result = coordinator.HandleFailure(
      15,
      Failure(rodakos::VoiceTransportFailureKind::kServer, "invalid_request",
              false, 131),
      100);
  RODAK_CHECK_EQ(result.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kTerminal);
  RODAK_CHECK_EQ(result.failure.code, "invalid_request");
  RODAK_CHECK_FALSE(coordinator.RetryDue(15, 1000));
}

RODAK_TEST(
    "voice reconnect guard cancels an in-flight open before wake restore") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({2, 10, 20});
  RODAK_CHECK(coordinator.Begin(10, 81));
  coordinator.HandleFailure(
      10,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "disconnect", true,
              81),
      0);

  bool current = true;
  FakeVoiceTransport transport;
  ScriptedAttempt attempt;
  attempt.generation = 83;
  transport.attempts.push_back(attempt);
  transport.on_open = [&current]() { current = false; };

  const auto cancelled = coordinator.Attempt(10, 10, transport, "wake-word",
                                             [&current]() { return current; });
  RODAK_CHECK_EQ(cancelled.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kCancelled);
  RODAK_CHECK_EQ(coordinator.interaction_generation(), 0u);
  RODAK_CHECK(std::find(transport.trace.begin(), transport.trace.end(),
                        "wake:83") == transport.trace.end());
  RODAK_CHECK(std::find(transport.trace.begin(), transport.trace.end(),
                        "input:83") == transport.trace.end());
}

RODAK_TEST(
    "voice reconnect cancels when the opened generation cannot be bound") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({2, 10, 20});
  RODAK_CHECK(coordinator.Begin(16, 141));
  coordinator.HandleFailure(
      16,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "disconnect", true,
              141),
      0);

  FakeVoiceTransport transport;
  ScriptedAttempt attempt;
  attempt.generation = 143;
  transport.attempts.push_back(attempt);

  const auto result = coordinator.Attempt(
      16, 10, transport, "wake-word", []() { return true; }, {},
      [](uint32_t) { return false; });
  RODAK_CHECK_EQ(result.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kCancelled);
  RODAK_CHECK_EQ(coordinator.interaction_generation(), 0u);
  RODAK_CHECK(std::find(transport.trace.begin(), transport.trace.end(),
                        "wake:143") == transport.trace.end());
  RODAK_CHECK(std::find(transport.trace.begin(), transport.trace.end(),
                        "input:143") == transport.trace.end());
  RODAK_CHECK_FALSE(transport.IsAudioChannelOpen());
}

RODAK_TEST(
    "voice reconnect retries a wake restore failure without starting input") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({3, 10, 40});
  RODAK_CHECK(coordinator.Begin(11, 91));
  coordinator.HandleFailure(
      11,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "disconnect", true,
              91),
      0);

  FakeVoiceTransport transport;
  ScriptedAttempt attempt;
  attempt.generation = 93;
  attempt.wake_succeeds = false;
  attempt.wake_failure = Failure(rodakos::VoiceTransportFailureKind::kSend,
                                 "wake_send_failed", true, 93);
  transport.attempts.push_back(attempt);

  const auto result = coordinator.Attempt(11, 10, transport, "wake-word");
  RODAK_CHECK_EQ(result.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kWaiting);
  RODAK_CHECK_EQ(result.attempt, 2u);
  RODAK_CHECK(std::find(transport.trace.begin(), transport.trace.end(),
                        "wake:93") != transport.trace.end());
  RODAK_CHECK(std::find(transport.trace.begin(), transport.trace.end(),
                        "input:93") == transport.trace.end());
  RODAK_CHECK_FALSE(transport.IsAudioChannelOpen());
}

RODAK_TEST(
    "voice reconnect rejects a wake failure reported for another generation") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({3, 10, 40});
  RODAK_CHECK(coordinator.Begin(17, 151));
  coordinator.HandleFailure(
      17,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "disconnect", true,
              151),
      0);

  FakeVoiceTransport transport;
  ScriptedAttempt attempt;
  attempt.generation = 153;
  attempt.wake_succeeds = false;
  attempt.wake_failure = Failure(rodakos::VoiceTransportFailureKind::kNetwork,
                                 "old_generation_failure", true, 151);
  transport.attempts.push_back(attempt);

  const auto result = coordinator.Attempt(17, 10, transport, "wake-word");
  RODAK_CHECK_EQ(result.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kTerminal);
  RODAK_CHECK_EQ(result.failure.code,
                 "reconnect_failure_generation_mismatch");
  RODAK_CHECK_EQ(result.failure.transport_generation, 153u);
  RODAK_CHECK_FALSE(transport.IsAudioChannelOpen());
}

RODAK_TEST(
    "voice reconnect treats input restore failure as terminal when directed") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({3, 10, 40});
  RODAK_CHECK(coordinator.Begin(12, 101));
  coordinator.HandleFailure(
      12,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "disconnect", true,
              101),
      0);

  FakeVoiceTransport transport;
  ScriptedAttempt attempt;
  attempt.generation = 103;
  attempt.input_start_succeeds = false;
  attempt.input_start_failure =
      Failure(rodakos::VoiceTransportFailureKind::kServer, "input_rejected",
              false, 103);
  transport.attempts.push_back(attempt);

  const auto result = coordinator.Attempt(12, 10, transport, "wake-word");
  RODAK_CHECK_EQ(result.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kTerminal);
  RODAK_CHECK_EQ(result.failure.code, "input_rejected");
  RODAK_CHECK(std::find(transport.trace.begin(), transport.trace.end(),
                        "wake:103") != transport.trace.end());
  RODAK_CHECK(std::find(transport.trace.begin(), transport.trace.end(),
                        "input:103") != transport.trace.end());
  RODAK_CHECK_FALSE(transport.IsAudioChannelOpen());
}

RODAK_TEST("voice reconnect rejects a reopened channel that reuses its old "
           "generation") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({3, 10, 40});
  RODAK_CHECK(coordinator.Begin(13, 111));
  coordinator.HandleFailure(
      13,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "disconnect", true,
              111),
      0);

  FakeVoiceTransport transport;
  ScriptedAttempt attempt;
  attempt.generation = 111;
  transport.attempts.push_back(attempt);

  const auto result = coordinator.Attempt(13, 10, transport, "wake-word");
  RODAK_CHECK_EQ(result.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kTerminal);
  RODAK_CHECK_EQ(result.failure.code, "reconnect_generation_not_advanced");
  RODAK_CHECK(std::find(transport.trace.begin(), transport.trace.end(),
                        "wake:111") == transport.trace.end());
  RODAK_CHECK_FALSE(transport.IsAudioChannelOpen());
}

RODAK_TEST("voice reconnect rejects a generation reused after restore failure") {
  rodakos::VoiceAssistantReconnectCoordinator coordinator({3, 10, 40});
  RODAK_CHECK(coordinator.Begin(20, 181));
  coordinator.HandleFailure(
      20,
      Failure(rodakos::VoiceTransportFailureKind::kNetwork, "disconnect", true,
              181),
      0);

  FakeVoiceTransport transport;
  ScriptedAttempt failed_restore;
  failed_restore.generation = 183;
  failed_restore.wake_succeeds = false;
  failed_restore.wake_failure =
      Failure(rodakos::VoiceTransportFailureKind::kSend,
              "wake_send_failed", true, 183);
  transport.attempts.push_back(failed_restore);

  ScriptedAttempt reused_generation;
  reused_generation.generation = 183;
  transport.attempts.push_back(reused_generation);

  const auto waiting = coordinator.Attempt(20, 10, transport, "wake-word");
  RODAK_CHECK_EQ(waiting.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kWaiting);
  RODAK_CHECK_EQ(waiting.attempt, 2u);

  const auto terminal = coordinator.Attempt(20, 30, transport, "wake-word");
  RODAK_CHECK_EQ(terminal.outcome,
                 rodakos::VoiceAssistantReconnectOutcome::kTerminal);
  RODAK_CHECK_EQ(terminal.failure.code,
                 "reconnect_generation_not_advanced");
  RODAK_CHECK_EQ(
      std::count(transport.trace.begin(), transport.trace.end(), "wake:183"),
      1);
  RODAK_CHECK_FALSE(transport.IsAudioChannelOpen());
}
