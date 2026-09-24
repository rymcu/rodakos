#include "phone_os/realtime_voice_contract.h"
#include "phone_os/realtime_voice_reconnect_policy.h"
#include "phone_os/realtime_voice_transport_harness.h"
#include "phone_os/voice_assistant_transport.h"
#include "test_framework.h"

#include <cJSON.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

RODAK_TEST("canonical realtime voice generations never emit the reserved zero") {
    RODAK_CHECK_EQ(rodakos::NextRealtimeVoiceGeneration(0), 1u);
    RODAK_CHECK_EQ(rodakos::NextRealtimeVoiceGeneration(1), 2u);
    RODAK_CHECK_EQ(rodakos::NextRealtimeVoiceGeneration(UINT32_MAX - 1), UINT32_MAX);
    RODAK_CHECK_EQ(rodakos::NextRealtimeVoiceGeneration(UINT32_MAX), 1u);
}

RODAK_TEST("canonical realtime voice session open rejects an unestablished generation") {
    rodakos::RealtimeVoiceDescriptor descriptor;
    descriptor.endpoint = "wss://voice.example.test/realtime";
    RODAK_CHECK(rodakos::BuildRealtimeVoiceSessionOpenMessage(
                    descriptor, false, false, 1)
                    .find("\"generation\":1") != std::string::npos);
    RODAK_CHECK(rodakos::BuildRealtimeVoiceSessionOpenMessage(
                    descriptor, false, false, 0)
                    .empty());
}

RODAK_TEST("canonical realtime voice reconnect policy schedules bounded exponential retries") {
    rodakos::RealtimeVoiceReconnectPolicy policy({3, 100, 250});
    RODAK_CHECK(policy.Begin(11));

    const auto first = policy.OnFailure(11, true);
    RODAK_CHECK(first.accepted);
    RODAK_CHECK(first.should_retry);
    RODAK_CHECK_EQ(first.attempt, 1u);
    RODAK_CHECK_EQ(first.delay_ms, 100u);
    RODAK_CHECK(policy.BeginRetry(11));
    RODAK_CHECK(policy.CompleteAttempt(11, true));
    RODAK_CHECK_EQ(policy.attempt(), 0u);

    const auto second = policy.OnFailure(11, true);
    RODAK_CHECK_EQ(second.delay_ms, 100u);
    RODAK_CHECK(policy.BeginRetry(11));
    RODAK_CHECK_FALSE(policy.CompleteAttempt(11, false));
    const auto third = policy.OnFailure(11, true);
    RODAK_CHECK_EQ(third.delay_ms, 200u);
    RODAK_CHECK(policy.BeginRetry(11));
    RODAK_CHECK_FALSE(policy.CompleteAttempt(11, false));
    const auto fourth = policy.OnFailure(11, true);
    RODAK_CHECK_EQ(fourth.delay_ms, 250u);
}

RODAK_TEST("canonical realtime voice reconnect policy terminal and cancel paths reject stale work") {
    rodakos::RealtimeVoiceReconnectPolicy policy({2, 50, 200});
    RODAK_CHECK(policy.Begin(21));
    const auto terminal = policy.OnFailure(21, false);
    RODAK_CHECK(terminal.accepted);
    RODAK_CHECK(terminal.terminal);
    RODAK_CHECK_FALSE(policy.OnFailure(21, true).accepted);
    RODAK_CHECK_FALSE(policy.BeginRetry(21));
    RODAK_CHECK_FALSE(policy.Cancel(21));

    RODAK_CHECK(policy.Begin(22));
    RODAK_CHECK_FALSE(policy.OnFailure(21, true).accepted);
    RODAK_CHECK(policy.Cancel(22));
    RODAK_CHECK_FALSE(policy.BeginRetry(22));
    RODAK_CHECK_FALSE(policy.OnFailure(22, true).accepted);
    RODAK_CHECK(policy.Begin(23));
}

RODAK_TEST("canonical realtime voice reconnect policy exhausts retry budget") {
    rodakos::RealtimeVoiceReconnectPolicy policy({2, 10, 100});
    RODAK_CHECK(policy.Begin(31));
    RODAK_CHECK(policy.OnFailure(31, true).should_retry);
    RODAK_CHECK(policy.BeginRetry(31));
    RODAK_CHECK_FALSE(policy.CompleteAttempt(31, false));
    RODAK_CHECK(policy.OnFailure(31, true).should_retry);
    RODAK_CHECK(policy.BeginRetry(31));
    RODAK_CHECK_FALSE(policy.CompleteAttempt(31, false));
    const auto terminal = policy.OnFailure(31, true);
    RODAK_CHECK(terminal.accepted);
    RODAK_CHECK(terminal.terminal);
    RODAK_CHECK_FALSE(terminal.should_retry);
}

RODAK_TEST("canonical realtime voice session gate rejects stale output and audio") {
    rodakos::RealtimeVoiceSessionGate gate;
    RODAK_CHECK(gate.Establish(7, "session-a"));
    RODAK_CHECK(gate.Matches(7, "session-a"));
    RODAK_CHECK_FALSE(gate.Matches(6, "session-a"));
    RODAK_CHECK_FALSE(gate.Matches(7, "session-b"));

    RODAK_CHECK_FALSE(gate.AcceptAudio(7, 1));
    RODAK_CHECK(gate.AcceptOutputStart(7, 4));
    RODAK_CHECK(gate.output_active());
    RODAK_CHECK_FALSE(gate.AcceptOutputStart(7, 4));
    RODAK_CHECK_FALSE(gate.AcceptOutputStart(6, 5));
    RODAK_CHECK(gate.AcceptAudio(7, 1));
    RODAK_CHECK_FALSE(gate.AcceptAudio(7, 1));
    RODAK_CHECK_FALSE(gate.AcceptAudio(7, 0));
    RODAK_CHECK_FALSE(gate.AcceptAudio(6, 2));
    RODAK_CHECK_FALSE(gate.AcceptOutputStop(7, 3));
    RODAK_CHECK(gate.AcceptOutputStop(7, 4));
    RODAK_CHECK_FALSE(gate.output_active());
    RODAK_CHECK_FALSE(gate.AcceptAudio(7, 2));
}

RODAK_TEST("canonical realtime voice session gate keeps sequence monotonic across output epochs") {
    rodakos::RealtimeVoiceSessionGate gate;
    RODAK_CHECK(gate.Establish(9, "session-b"));
    RODAK_CHECK(gate.AcceptOutputStart(9, 1));
    RODAK_CHECK(gate.AcceptAudio(9, 9));
    RODAK_CHECK(gate.AcceptOutputStop(9, 1));
    RODAK_CHECK(gate.AcceptOutputStart(9, 2));
    RODAK_CHECK(gate.AcceptAudio(9, 10));
    gate.Clear();
    RODAK_CHECK_FALSE(gate.Matches(9, "session-b"));
    RODAK_CHECK_FALSE(gate.AcceptOutputStart(9, 3));
}

RODAK_TEST("canonical realtime voice session gate rejects epoch-less output after negotiation") {
    rodakos::RealtimeVoiceSessionGate gate;
    RODAK_CHECK(gate.Establish(12, "session-epoch"));
    // Epoch-less servers remain compatible when they never negotiate epochs.
    RODAK_CHECK(gate.AcceptOutputStart(12, 0));
    RODAK_CHECK(gate.AcceptOutputStop(12, 0));
    RODAK_CHECK(gate.AcceptOutputStart(12, 0));
    RODAK_CHECK(gate.AcceptOutputStop(12, 0));

    RODAK_CHECK(gate.AcceptOutputStart(12, 5));
    RODAK_CHECK(gate.AcceptOutputStop(12, 5));
    // A delayed epoch-less start must not reactivate playback after the
    // server has switched to explicit playback epochs.
    RODAK_CHECK_FALSE(gate.AcceptOutputStart(12, 0));
    RODAK_CHECK(gate.AcceptOutputStart(12, 6));
}

RODAK_TEST("canonical realtime voice transport harness enforces session output and RAV1 order") {
    rodakos::RealtimeVoiceTransportHarness harness;
    std::string error;
    const uint8_t payload[] = {0x21, 0x22};
    std::vector<uint8_t> frame(rodakos::kRealtimeVoiceAudioHeaderBytes + sizeof(payload));
    std::memcpy(frame.data(), rodakos::kRealtimeVoiceAudioMagic, 4);
    frame[4] = 0;
    frame[5] = 0;
    frame[6] = 0;
    frame[7] = 1;
    frame[8] = 0;
    frame[9] = 0;
    frame[10] = 0;
    frame[11] = sizeof(payload);
    std::memcpy(frame.data() + rodakos::kRealtimeVoiceAudioHeaderBytes, payload, sizeof(payload));

    RODAK_CHECK(harness.OpenSession(4, "session-harness"));
    RODAK_CHECK_FALSE(harness.ReceiveAudio(frame.data(), frame.size(), 8192, error));
    RODAK_CHECK(harness.ReceiveOutputStart(7));
    RODAK_CHECK(harness.ReceiveAudio(frame.data(), frame.size(), 8192, error));
    RODAK_CHECK_EQ(harness.last_audio_payload().size(), sizeof(payload));
    RODAK_CHECK_FALSE(harness.ReceiveAudio(frame.data(), frame.size(), 8192, error));
    RODAK_CHECK(harness.ReceiveOutputStop(7));
    harness.CloseSession();
    RODAK_CHECK(harness.OpenSession(5, "session-harness-2"));
    RODAK_CHECK_FALSE(harness.ReceiveOutputStartForGeneration(4, 8));
    RODAK_CHECK(harness.ReceiveOutputStartForGeneration(5, 8));
    RODAK_CHECK_FALSE(harness.ReceiveAudioForGeneration(4, frame.data(), frame.size(), 8192, error));
    harness.CloseSession();
    RODAK_CHECK_FALSE(harness.session_open());
}

RODAK_TEST("canonical realtime voice session gate cannot be reset by delayed ready") {
    rodakos::RealtimeVoiceSessionGate gate;
    RODAK_CHECK_FALSE(gate.Establish(0, "session-a"));
    RODAK_CHECK_FALSE(gate.Establish(7, ""));
    RODAK_CHECK(gate.Establish(7, "session-a"));
    RODAK_CHECK(gate.AcceptOutputStart(7, 4));
    RODAK_CHECK(gate.AcceptAudio(7, 9));

    RODAK_CHECK_FALSE(gate.Establish(7, "session-a"));
    RODAK_CHECK_FALSE(gate.Establish(7, "session-b"));
    RODAK_CHECK_EQ(gate.playback_epoch(), 4u);
    RODAK_CHECK_EQ(gate.audio_sequence(), 9u);
    RODAK_CHECK_FALSE(gate.AcceptAudio(7, 1));

    gate.Clear();
    RODAK_CHECK(gate.Establish(8, "session-b"));
}

RODAK_TEST("canonical realtime voice harness ignores stale lifecycle callbacks") {
    rodakos::RealtimeVoiceTransportHarness harness;
    RODAK_CHECK(harness.OpenSession(41, "session-old"));
    RODAK_CHECK(harness.ReceiveOutputStart(3));

    // A delayed ready from the same websocket generation is a duplicate and
    // must not reset playback/audio monotonicity.
    RODAK_CHECK_FALSE(harness.ReceiveSessionReadyForGeneration(41, "session-old"));
    RODAK_CHECK_FALSE(harness.ReceiveSessionReadyForGeneration(41, "session-new"));
    RODAK_CHECK(harness.output_active());

    // Callbacks from the old client cannot tear down the current session.
    RODAK_CHECK_FALSE(harness.ReceiveDisconnectForGeneration(40));
    RODAK_CHECK_FALSE(harness.ReceiveErrorForGeneration(40));
    RODAK_CHECK_FALSE(harness.ReceiveSessionEndForGeneration(40));
    RODAK_CHECK(harness.session_open());
    RODAK_CHECK(harness.output_active());

    // The current callback closes the session and clears output state.
    RODAK_CHECK(harness.ReceiveSessionEndForGeneration(41));
    RODAK_CHECK_FALSE(harness.session_open());
    RODAK_CHECK_FALSE(harness.output_active());
    RODAK_CHECK_FALSE(harness.ReceiveDisconnectForGeneration(41));

    // A new generation can establish independently; an old callback must not
    // affect it even when it arrives after reconnect.
    RODAK_CHECK(harness.OpenSession(42, "session-new"));
    RODAK_CHECK(harness.ReceiveOutputStart(4));
    RODAK_CHECK_FALSE(harness.ReceiveErrorForGeneration(41));
    RODAK_CHECK(harness.session_open());
    RODAK_CHECK(harness.output_active());
    RODAK_CHECK(harness.ReceiveDisconnectForGeneration(42));
    RODAK_CHECK_FALSE(harness.session_open());

    RODAK_CHECK(harness.OpenSession(43, "session-error"));
    RODAK_CHECK(harness.ReceiveErrorForGeneration(43));
    RODAK_CHECK_FALSE(harness.session_open());
}

RODAK_TEST("canonical realtime voice reconnect policy composes with harness generations") {
    rodakos::RealtimeVoiceReconnectPolicy policy({2, 25, 100});
    rodakos::RealtimeVoiceTransportHarness harness;

    RODAK_CHECK(policy.Begin(51));
    RODAK_CHECK(harness.OpenSession(51, "session-before-reconnect"));
    RODAK_CHECK(harness.ReceiveDisconnectForGeneration(51));

    const auto first_failure = policy.OnFailure(51, true);
    RODAK_CHECK(first_failure.accepted);
    RODAK_CHECK(first_failure.should_retry);
    RODAK_CHECK_EQ(first_failure.generation, 51u);
    RODAK_CHECK(policy.BeginRetry(51));
    RODAK_CHECK(policy.CompleteAttempt(51, true));

    // A new policy generation and session gate generation advance together;
    // callbacks from generation 51 cannot affect generation 52.
    RODAK_CHECK(policy.Cancel(51));
    RODAK_CHECK(policy.Begin(52));
    RODAK_CHECK(harness.OpenSession(52, "session-after-reconnect"));
    RODAK_CHECK_FALSE(harness.ReceiveDisconnectForGeneration(51));
    RODAK_CHECK(harness.session_open());
    RODAK_CHECK(harness.ReceiveDisconnectForGeneration(52));
    RODAK_CHECK_FALSE(harness.session_open());
}

namespace {

bool ParseDescriptor(const char* json, rodakos::RealtimeVoiceDescriptor& descriptor,
                     std::string& error) {
    cJSON* root = cJSON_Parse(json);
    if (root == nullptr) {
        error = "invalid json";
        return false;
    }
    const bool parsed = rodakos::ParseRealtimeVoiceDescriptor(root, descriptor, error);
    cJSON_Delete(root);
    return parsed;
}

}  // namespace

RODAK_TEST("canonical realtime voice descriptor parses without legacy websocket fields") {
    rodakos::RealtimeVoiceDescriptor descriptor;
    std::string error;
    RODAK_CHECK(ParseDescriptor(
        R"({
          "schema":"rodak-realtime-voice/v1",
          "protocol":"rodak-realtime-voice",
          "protocolVersion":1,
          "transport":"websocket",
          "endpoint":"ws://127.0.0.1:9080/api/v1/aiot/devices/realtime-voice",
          "authMode":"device-token",
          "uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
          "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
          "limits":{"maxAudioFrameBytes":8192,"maxControlBytes":65536},
          "features":[],
          "capabilities":["session","audio-input","audio-output"],
          "events":["session.open","session.ready","input.start","input.stop",
                    "wake.detected","playback.abort","vad","output.start","output.stop",
                    "session.end","mcp","error"]
        })",
        descriptor, error));
    RODAK_CHECK_EQ(descriptor.protocol_version, 1);
    RODAK_CHECK_EQ(descriptor.uplink_sample_rate_hz, 16000);
    RODAK_CHECK(error.empty());
    RODAK_CHECK_EQ(descriptor.preferred_vad_strategy, "server-authoritative");
    RODAK_CHECK_EQ(descriptor.vad_strategies.size(), static_cast<size_t>(1));
}

RODAK_TEST("canonical realtime voice descriptor negotiates VAD strategies") {
    rodakos::RealtimeVoiceDescriptor descriptor;
    std::string error;
    RODAK_CHECK(ParseDescriptor(
        R"({"schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
          "protocolVersion":1,"transport":"websocket","endpoint":"wss://example.test/voice",
          "authMode":"device-token","uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
          "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
          "limits":{"maxAudioFrameBytes":8192,"maxControlBytes":65536},"features":[],
          "capabilities":["session"],"events":["session.open"],
          "vadStrategies":["device-authoritative","server-authoritative","hybrid-fallback"],
          "preferredVadStrategy":"device-authoritative"})",
        descriptor, error));
    RODAK_CHECK_EQ(descriptor.vad_strategies.size(), static_cast<size_t>(3));
    RODAK_CHECK_EQ(descriptor.preferred_vad_strategy, "device-authoritative");
    const std::string open = rodakos::BuildRealtimeVoiceSessionOpenMessage(
        descriptor, false, true, 9);
    RODAK_CHECK(open.find("vadStrategies") != std::string::npos);
    RODAK_CHECK(open.find("preferredVadStrategy") != std::string::npos);
}

RODAK_TEST("canonical realtime voice descriptor rejects unsupported VAD strategy") {
    rodakos::RealtimeVoiceDescriptor descriptor;
    std::string error;
    RODAK_CHECK_FALSE(ParseDescriptor(
        R"({"schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
          "protocolVersion":1,"transport":"websocket","endpoint":"wss://example.test/voice",
          "authMode":"device-token","uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
          "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
          "limits":{"maxAudioFrameBytes":8192,"maxControlBytes":65536},"features":[],
          "capabilities":["session"],"events":["session.open"],
          "vadStrategies":["bogus"],"preferredVadStrategy":"bogus"})",
        descriptor, error));
    RODAK_CHECK_FALSE(error.empty());
}

RODAK_TEST("canonical realtime voice descriptor rejects legacy websocket object") {
    rodakos::RealtimeVoiceDescriptor descriptor;
    std::string error;
    RODAK_CHECK_FALSE(ParseDescriptor(
        R"({"websocket":{"url":"ws://legacy","token":"secret","version":3}})",
        descriptor, error));
    RODAK_CHECK_FALSE(error.empty());
}

RODAK_TEST("canonical realtime voice control messages use event names") {
    rodakos::RealtimeVoiceDescriptor descriptor;
    std::string error;
    RODAK_CHECK(ParseDescriptor(
        R"({
          "schema":"rodak-realtime-voice/v1",
          "protocol":"rodak-realtime-voice","protocolVersion":1,"transport":"websocket",
          "endpoint":"wss://example.test/voice",
          "authMode":"device-token",
          "uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
          "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
          "limits":{"maxAudioFrameBytes":8192,"maxControlBytes":65536},
          "features":[],
          "capabilities":["session"],
          "events":["session.open"]
        })",
        descriptor, error));
    const std::string open = rodakos::BuildRealtimeVoiceSessionOpenMessage(descriptor, true, true, 7);
    RODAK_CHECK(open.find("session.open") != std::string::npos);
    RODAK_CHECK(open.find("device_vad_epoch") != std::string::npos);
    RODAK_CHECK(open.find("vadEpoch") == std::string::npos);
    RODAK_CHECK(open.find("hello") == std::string::npos);
    const std::string start = rodakos::BuildRealtimeVoiceInputMessage(
        rodakos::kRealtimeVoiceEventInputStart, "session-1", "realtime");
    RODAK_CHECK(start.find("input.start") != std::string::npos);
    RODAK_CHECK(start.find("listen") == std::string::npos);
    const std::string wake = rodakos::BuildRealtimeVoiceWakeMessage("session-1", "你好达克");
    RODAK_CHECK(wake.find("wake.detected") != std::string::npos);
}

RODAK_TEST("canonical realtime voice MCP payload must be a JSON object") {
    const std::string valid = rodakos::BuildRealtimeVoiceMcpMessage(
        "session-1", R"({"method":"tools/list","params":{}})");
    RODAK_CHECK_FALSE(valid.empty());

    cJSON* root = cJSON_Parse(valid.c_str());
    RODAK_CHECK(root != nullptr);
    RODAK_CHECK(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(root, "payload")));
    cJSON_Delete(root);

    RODAK_CHECK(rodakos::BuildRealtimeVoiceMcpMessage("session-1", "not-json").empty());
    RODAK_CHECK(rodakos::BuildRealtimeVoiceMcpMessage("session-1", R"([1,2,3])").empty());
    RODAK_CHECK(rodakos::BuildRealtimeVoiceMcpMessage("session-1", R"("text")").empty());
}

RODAK_TEST("canonical realtime voice VAD builder rejects invalid boundaries") {
    RODAK_CHECK_FALSE(rodakos::BuildRealtimeVoiceVadMessage(
                          "session-1", "start", "device", 1, 0, 0)
                          .empty());
    RODAK_CHECK(rodakos::BuildRealtimeVoiceVadMessage(
                    "session-1", "", "device", 1, 0, 0)
                    .empty());
    RODAK_CHECK(rodakos::BuildRealtimeVoiceVadMessage(
                    "session-1", "start", "", 1, 0, 0)
                    .empty());
    RODAK_CHECK(rodakos::BuildRealtimeVoiceVadMessage(
                    "session-1", "other", "device", 1, 0, 0)
                    .empty());
    RODAK_CHECK(rodakos::BuildRealtimeVoiceVadMessage(
                    "session-1", "start", "device", 0, 0, 0)
                    .empty());
}

RODAK_TEST("canonical realtime voice input wake and abort builders reject invalid fields") {
    RODAK_CHECK_FALSE(rodakos::BuildRealtimeVoiceInputMessage(
                          rodakos::kRealtimeVoiceEventInputStart, "session-1", "realtime")
                          .empty());
    RODAK_CHECK(rodakos::BuildRealtimeVoiceInputMessage(
                    "listen:start", "session-1", "realtime")
                    .empty());
    RODAK_CHECK(rodakos::BuildRealtimeVoiceInputMessage(
                    rodakos::kRealtimeVoiceEventInputStart, "session-1", "manual")
                    .empty());
    RODAK_CHECK(rodakos::BuildRealtimeVoiceWakeMessage("session-1", "wake").size() > 0);
    RODAK_CHECK(rodakos::BuildRealtimeVoiceWakeMessage("", "wake").empty());
    RODAK_CHECK(rodakos::BuildRealtimeVoicePlaybackAbortMessage(
                    "session-1", rodakos::VoiceAbortReason::kVadDetected, 1)
                    .size() > 0);
    RODAK_CHECK(rodakos::BuildRealtimeVoicePlaybackAbortMessage(
                    "", rodakos::VoiceAbortReason::kVadDetected, 1)
                    .empty());
    RODAK_CHECK(rodakos::BuildRealtimeVoicePlaybackAbortMessage(
                    "session-1", static_cast<rodakos::VoiceAbortReason>(-1), 1)
                    .empty());
}

RODAK_TEST("canonical playback abort preserves the server capture semantics for device VAD") {
    const std::vector<std::pair<rodakos::VoiceAbortReason, const char*>> cases = {
        {rodakos::VoiceAbortReason::kVadDetected, "vad_detected"},
        {rodakos::VoiceAbortReason::kWakeWordDetected, "wake-word"},
        {rodakos::VoiceAbortReason::kNone, "user"},
    };
    for (const auto& [reason, expected_reason] : cases) {
        const std::string message = rodakos::BuildRealtimeVoicePlaybackAbortMessage(
            "session-barge-in", reason, 7);
        cJSON* root = cJSON_Parse(message.c_str());
        RODAK_CHECK(root != nullptr);
        const cJSON* event = cJSON_GetObjectItemCaseSensitive(root, "event");
        const cJSON* session = cJSON_GetObjectItemCaseSensitive(root, "sessionId");
        const cJSON* wire_reason = cJSON_GetObjectItemCaseSensitive(root, "reason");
        const cJSON* epoch = cJSON_GetObjectItemCaseSensitive(root, "playbackEpoch");
        RODAK_CHECK(cJSON_IsString(event));
        RODAK_CHECK_EQ(std::string(event->valuestring), "playback.abort");
        RODAK_CHECK(cJSON_IsString(session));
        RODAK_CHECK_EQ(std::string(session->valuestring), "session-barge-in");
        RODAK_CHECK(cJSON_IsString(wire_reason));
        RODAK_CHECK_EQ(std::string(wire_reason->valuestring), expected_reason);
        RODAK_CHECK(cJSON_IsNumber(epoch));
        RODAK_CHECK_EQ(epoch->valueint, 7);
        cJSON_Delete(root);
    }
}

RODAK_TEST("canonical descriptor only accepts downlink durations supported by the session handshake") {
    const char* json = R"({
      "schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
      "protocolVersion":1,"transport":"websocket","endpoint":"wss://example.test/voice",
      "authMode":"device-token",
      "uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
      "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
      "limits":{"maxAudioFrameBytes":8192,"maxControlBytes":65536},"features":[],
      "capabilities":["session"],"events":["session.open"]})";
    const std::vector<std::pair<int, bool>> cases = {
        {5, true}, {10, true}, {20, true}, {40, true}, {60, true},
        {0, false}, {30, false}, {80, false}, {100, false}, {120, false},
    };
    for (const auto& [duration, expected] : cases) {
        cJSON* root = cJSON_Parse(json);
        RODAK_CHECK(root != nullptr);
        cJSON* downlink = cJSON_GetObjectItemCaseSensitive(root, "downlink");
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(downlink, "frameDurationMs"),
                             duration);
        rodakos::RealtimeVoiceDescriptor descriptor;
        std::string error;
        const bool accepted = rodakos::ParseRealtimeVoiceDescriptor(root, descriptor, error);
        cJSON_Delete(root);
        RODAK_CHECK_EQ(accepted, expected);
        if (accepted) {
            RODAK_CHECK_EQ(descriptor.downlink_frame_duration_ms, duration);
            RODAK_CHECK(rodakos::IsSupportedRealtimeVoiceFrameDuration(
                descriptor.downlink_frame_duration_ms));
        } else {
            RODAK_CHECK_FALSE(error.empty());
        }
    }
}

RODAK_TEST("canonical realtime voice descriptor rejects invalid audio and userinfo") {
    rodakos::RealtimeVoiceDescriptor descriptor;
    std::string error;
    RODAK_CHECK_FALSE(ParseDescriptor(
        R"({"schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice","protocolVersion":1,"transport":"websocket",
           "endpoint":"ws://user:pass@example.test/voice","authMode":"device-token",
           "uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
           "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
           "limits":{"maxAudioFrameBytes":8192,"maxControlBytes":65536},"features":[],
           "capabilities":["session"],"events":["session.open"]})",
        descriptor, error));
    RODAK_CHECK_FALSE(error.empty());
}

RODAK_TEST("canonical realtime voice descriptor requires schema and bounded metadata") {
    rodakos::RealtimeVoiceDescriptor descriptor;
    std::string error;
    const char* missing_schema = R"({
      "protocol":"rodak-realtime-voice","protocolVersion":1,"transport":"websocket",
      "endpoint":"wss://example.test/voice","authMode":"device-token",
      "uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
      "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
      "limits":{"maxAudioFrameBytes":8192,"maxControlBytes":65536},"features":[],
      "capabilities":["session"],"events":["session.open"]})";
    RODAK_CHECK_FALSE(ParseDescriptor(missing_schema, descriptor, error));

    const char* fractional_limit = R"({
      "schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
      "protocolVersion":1,"transport":"websocket","endpoint":"wss://example.test/voice",
      "authMode":"device-token",
      "uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
      "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
      "limits":{"maxAudioFrameBytes":8192.5,"maxControlBytes":65536},"features":[],
      "capabilities":["session"],"events":["session.open"]})";
    RODAK_CHECK_FALSE(ParseDescriptor(fractional_limit, descriptor, error));

    const char* too_large = R"({
      "schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
      "protocolVersion":1,"transport":"websocket","endpoint":"wss://example.test/voice",
      "authMode":"device-token",
      "uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
      "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
      "limits":{"maxAudioFrameBytes":65537,"maxControlBytes":65536},"features":[],
      "capabilities":["session"],"events":["session.open"]})";
    RODAK_CHECK_FALSE(ParseDescriptor(too_large, descriptor, error));
}

RODAK_TEST("canonical realtime voice descriptor rejects legacy fields and endpoints") {
    rodakos::RealtimeVoiceDescriptor descriptor;
    std::string error;
    const char* legacy_field = R"({
      "schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
      "protocolVersion":1,"transport":"websocket","endpoint":"wss://example.test/voice",
      "authMode":"device-token","websocketUrl":"wss://legacy.example/voice",
      "uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
      "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
      "limits":{"maxAudioFrameBytes":8192,"maxControlBytes":65536},"features":[],
      "capabilities":["session"],"events":["session.open"]})";
    RODAK_CHECK_FALSE(ParseDescriptor(legacy_field, descriptor, error));

    const char* legacy_feature = R"({
      "schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
      "protocolVersion":1,"transport":"websocket","endpoint":"wss://example.test/voice",
      "authMode":"device-token","uplink":{"codec":"opus","sampleRateHz":16000,
      "channels":1,"frameDurationMs":60},"downlink":{"codec":"opus","sampleRateHz":24000,
      "channels":1,"frameDurationMs":60},"limits":{"maxAudioFrameBytes":8192,
      "maxControlBytes":65536},"features":["XIAOZHI-wire"],"capabilities":["session"],
      "events":["session.open"]})";
    RODAK_CHECK_FALSE(ParseDescriptor(legacy_feature, descriptor, error));

    const char* http_endpoint = R"({
      "schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
      "protocolVersion":1,"transport":"websocket","endpoint":"https://example.test/voice",
      "authMode":"device-token","uplink":{"codec":"opus","sampleRateHz":16000,
      "channels":1,"frameDurationMs":60},"downlink":{"codec":"opus","sampleRateHz":24000,
      "channels":1,"frameDurationMs":60},"limits":{"maxAudioFrameBytes":8192,
      "maxControlBytes":65536},"features":[],"capabilities":["session"],
      "events":["session.open"]})";
    RODAK_CHECK_FALSE(ParseDescriptor(http_endpoint, descriptor, error));

    const char* query_endpoint = R"({
      "schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
      "protocolVersion":1,"transport":"websocket","endpoint":"wss://example.test/voice?token=secret",
      "authMode":"device-token","uplink":{"codec":"opus","sampleRateHz":16000,
      "channels":1,"frameDurationMs":60},"downlink":{"codec":"opus","sampleRateHz":24000,
      "channels":1,"frameDurationMs":60},"limits":{"maxAudioFrameBytes":8192,
      "maxControlBytes":65536},"features":[],"capabilities":["session"],
      "events":["session.open"]})";
    RODAK_CHECK_FALSE(ParseDescriptor(query_endpoint, descriptor, error));
}

RODAK_TEST("canonical realtime voice descriptor rejects unsupported uplink negotiation") {
    rodakos::RealtimeVoiceDescriptor descriptor;
    std::string error;
    const char* unsupported_uplink = R"({
      "schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
      "protocolVersion":1,"transport":"websocket","endpoint":"wss://example.test/voice",
      "authMode":"device-token",
      "uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":40},
      "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
      "limits":{"maxAudioFrameBytes":8192,"maxControlBytes":65536},"features":[],
      "capabilities":["session"],"events":["session.open"]})";
    RODAK_CHECK_FALSE(ParseDescriptor(unsupported_uplink, descriptor, error));
}

RODAK_TEST("canonical realtime voice RAV1 frame parser validates envelope and exposes payload") {
    std::vector<uint8_t> frame{
        'R', 'A', 'V', '1',
        0, 0, 0, 7,
        0, 0, 0, 3,
        0x11, 0x22, 0x33,
    };
    rodakos::RealtimeVoiceAudioFrame parsed;
    std::string error;
    RODAK_CHECK(rodakos::ParseRealtimeVoiceAudioFrame(
        frame.data(), frame.size(), 8192, parsed, error));
    RODAK_CHECK_EQ(parsed.sequence, static_cast<uint32_t>(7));
    RODAK_CHECK_EQ(parsed.payload_size, static_cast<size_t>(3));
    RODAK_CHECK_EQ(parsed.payload[0], static_cast<uint8_t>(0x11));
    RODAK_CHECK(error.empty());
}

RODAK_TEST("canonical realtime voice RAV1 frame parser rejects malformed and oversized frames") {
    std::vector<uint8_t> valid{
        'R', 'A', 'V', '1',
        0, 0, 0, 1,
        0, 0, 0, 2,
        0xaa, 0xbb,
    };
    rodakos::RealtimeVoiceAudioFrame parsed;
    std::string error;
    RODAK_CHECK_FALSE(rodakos::ParseRealtimeVoiceAudioFrame(
        nullptr, 0, 8192, parsed, error));
    RODAK_CHECK_FALSE(error.empty());

    valid[0] = 'x';
    RODAK_CHECK_FALSE(rodakos::ParseRealtimeVoiceAudioFrame(
        valid.data(), valid.size(), 8192, parsed, error));
    valid[0] = 'R';
    valid[7] = 0;
    RODAK_CHECK_FALSE(rodakos::ParseRealtimeVoiceAudioFrame(
        valid.data(), valid.size(), 8192, parsed, error));
    valid[7] = 1;
    RODAK_CHECK_FALSE(rodakos::ParseRealtimeVoiceAudioFrame(
        valid.data(), valid.size() - 1, 8192, parsed, error));
    RODAK_CHECK_FALSE(rodakos::ParseRealtimeVoiceAudioFrame(
        valid.data(), valid.size(), 1, parsed, error));
}

RODAK_TEST("canonical realtime voice MCP inbound envelope requires an object payload") {
    cJSON* object_payload = cJSON_Parse(R"({"event":"mcp","payload":{"method":"tools/list"}})");
    cJSON* array_payload = cJSON_Parse(R"({"event":"mcp","payload":[]})");
    cJSON* scalar_payload = cJSON_Parse(R"({"event":"mcp","payload":"text"})");
    RODAK_CHECK(rodakos::IsRealtimeVoiceMcpPayloadObject(object_payload));
    RODAK_CHECK_FALSE(rodakos::IsRealtimeVoiceMcpPayloadObject(array_payload));
    RODAK_CHECK_FALSE(rodakos::IsRealtimeVoiceMcpPayloadObject(scalar_payload));
    cJSON_Delete(object_payload);
    cJSON_Delete(array_payload);
    cJSON_Delete(scalar_payload);
}

RODAK_TEST("canonical realtime voice server control payloads validate field types") {
    const std::vector<std::pair<const char*, bool>> cases = {
        {R"({"event":"output.start","text":"hello"})", true},
        {R"({"event":"output.start","text":1})", false},
        {R"({"event":"output.start","text":""})", false},
        {R"({"event":"output.stop","reason":"done"})", true},
        {R"({"event":"session.end","reason":false})", false},
        {R"({"event":"session.end","reason":""})", false},
        {R"({"event":"error","code":"failed","message":"no","retryable":false})", true},
        {R"({"event":"error","code":"failed","message":"no","retryable":"false"})", false},
    };
    for (const auto& [json, expected] : cases) {
        cJSON* root = cJSON_Parse(json);
        std::string error;
        RODAK_CHECK_EQ(rodakos::ValidateRealtimeVoiceServerControlPayload(root, error), expected);
        if (!expected) RODAK_CHECK_FALSE(error.empty());
        cJSON_Delete(root);
    }
}
