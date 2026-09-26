#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct cJSON;

namespace rodakos {

enum class VoiceAbortReason;

inline constexpr char kRodakRealtimeVoiceSchema[] = "rodak-realtime-voice/v1";
inline constexpr char kRodakRealtimeVoiceProtocol[] = "rodak-realtime-voice";
inline constexpr int kRodakRealtimeVoiceProtocolVersion = 1;
inline constexpr char kRealtimeVoiceTransportWebSocket[] = "websocket";
inline constexpr char kRealtimeVoiceAuthModeDeviceToken[] = "device-token";

inline constexpr char kRealtimeVoiceEventSessionOpen[] = "session.open";
inline constexpr char kRealtimeVoiceEventSessionReady[] = "session.ready";
inline constexpr char kRealtimeVoiceEventInputStart[] = "input.start";
inline constexpr char kRealtimeVoiceEventInputStop[] = "input.stop";
inline constexpr char kRealtimeVoiceEventWakeDetected[] = "wake.detected";
inline constexpr char kRealtimeVoiceEventPlaybackAbort[] = "playback.abort";
inline constexpr char kRealtimeVoiceEventVad[] = "vad";
inline constexpr char kRealtimeVoiceEventOutputStart[] = "output.start";
inline constexpr char kRealtimeVoiceEventOutputStop[] = "output.stop";
inline constexpr char kRealtimeVoiceEventSessionEnd[] = "session.end";
inline constexpr char kRealtimeVoiceEventMcp[] = "mcp";
inline constexpr char kRealtimeVoiceEventError[] = "error";
inline constexpr char kRealtimeVoiceAudioMagic[] = "RAV1";
inline constexpr size_t kRealtimeVoiceAudioHeaderBytes = 12;
inline constexpr char kRealtimeVoiceVadDeviceAuthoritative[] = "device-authoritative";
inline constexpr char kRealtimeVoiceVadServerAuthoritative[] = "server-authoritative";
inline constexpr char kRealtimeVoiceVadHybridFallback[] = "hybrid-fallback";

// Generation zero is reserved as "not established" on the wire.  Reconnect
// and close paths must therefore wrap UINT32_MAX back to one rather than
// emitting zero, which the peer would reject as an invalid session generation.
inline constexpr uint32_t NextRealtimeVoiceGeneration(uint32_t generation) {
    return generation == UINT32_MAX ? 1u : generation + 1u;
}

struct RealtimeVoiceDescriptor {
    std::string endpoint;
    std::string auth_mode = "device-token";
    int protocol_version = kRodakRealtimeVoiceProtocolVersion;
    int uplink_sample_rate_hz = 16000;
    int uplink_channels = 1;
    int uplink_frame_duration_ms = 60;
    int downlink_sample_rate_hz = 24000;
    int downlink_channels = 1;
    int downlink_frame_duration_ms = 60;
    size_t max_audio_frame_bytes = 8192;
    size_t max_control_bytes = 64 * 1024;
    std::vector<std::string> features;
    std::vector<std::string> vad_strategies;
    std::string preferred_vad_strategy = kRealtimeVoiceVadServerAuthoritative;
};

// A parsed canonical RAV1 audio frame. The payload points into the caller's
// input buffer and is only valid for the duration of that buffer's lifetime.
struct RealtimeVoiceAudioFrame {
    uint32_t sequence = 0;
    const uint8_t* payload = nullptr;
    size_t payload_size = 0;
};

struct RealtimeVoiceServerError {
    std::string code;
    std::string message;
    bool retryable = false;
};

// Host-testable session gate shared by the canonical transport lifecycle.
// It deliberately contains no FreeRTOS/ESP state: the websocket transport
// owns locking and calls these methods while holding its session mutex.
// A gate accepts only monotonic playback/audio epochs for the active
// generation, so delayed frames from a previous session cannot revive output.
class RealtimeVoiceSessionGate {
public:
    // Establish exactly one session for a connection generation. Duplicate or
    // delayed session.ready events must not reset playback/audio monotonicity.
    bool Establish(uint32_t generation, const std::string& session_id);
    void Clear();

    bool Matches(uint32_t generation, const std::string& session_id) const;
    bool AcceptOutputStart(uint32_t generation, uint32_t playback_epoch);
    bool AcceptOutputStop(uint32_t generation, uint32_t playback_epoch);
    bool AcceptAudio(uint32_t generation, uint32_t sequence);

    uint32_t playback_epoch() const { return playback_epoch_; }
    uint32_t audio_sequence() const { return audio_sequence_; }
    uint32_t generation() const { return generation_; }
    bool output_active() const { return output_active_; }

private:
    uint32_t generation_ = 0;
    std::string session_id_;
    uint32_t playback_epoch_ = 0;
    uint32_t audio_sequence_ = 0;
    bool output_active_ = false;
};

// Parse the canonical realtimeVoice descriptor. Legacy websocket objects are
// intentionally not accepted here; compatibility belongs to the Rodak server.
bool ParseRealtimeVoiceDescriptor(const cJSON* value,
                                  RealtimeVoiceDescriptor& descriptor,
                                  std::string& error);

std::string BuildRealtimeVoiceSessionOpenMessage(const RealtimeVoiceDescriptor& descriptor,
                                                 bool supports_mcp,
                                                 bool supports_device_vad_epoch,
                                                 uint32_t generation);

bool IsRealtimeVoiceVadStrategy(const std::string& value);
bool IsSupportedRealtimeVoiceFrameDuration(int frame_duration_ms);

// Validate the canonical MCP envelope payload before it reaches the device
// event bus. Canonical MCP payloads are JSON objects, never scalar/array text.
bool IsRealtimeVoiceMcpPayloadObject(const cJSON* envelope);

// Decode the canonical server error without coupling the wire contract to a
// transport lifecycle. The transport maps this value to its failure type and
// attaches the active connection generation.
bool ParseRealtimeVoiceServerError(const cJSON* envelope,
                                   RealtimeVoiceServerError& server_error,
                                   std::string& error);

// Validate server-to-device payload fields for canonical control events. The
// transport still owns session/generation/epoch state checks.
bool ValidateRealtimeVoiceServerControlPayload(const cJSON* envelope,
                                               std::string& error);

// Validate and decode the 12-byte RAV1 envelope without requiring ESP-IDF.
// The helper intentionally does not enforce monotonic sequence ordering; that
// is a transport/session concern and must be checked against connection state.
bool ParseRealtimeVoiceAudioFrame(const uint8_t* data,
                                  size_t size,
                                  size_t max_payload_size,
                                  RealtimeVoiceAudioFrame& frame,
                                  std::string& error);

std::string BuildRealtimeVoiceInputMessage(const char* event,
                                           const std::string& session_id,
                                           const std::string& mode = {});

std::string BuildRealtimeVoiceWakeMessage(const std::string& session_id,
                                          const std::string& wake_word);

std::string BuildRealtimeVoicePlaybackAbortMessage(const std::string& session_id,
                                                   VoiceAbortReason reason,
                                                   uint32_t playback_epoch);

std::string BuildRealtimeVoiceVadMessage(const std::string& session_id,
                                         const char* state,
                                         const char* source,
                                         uint32_t sequence,
                                         uint32_t trigger_ms,
                                         uint32_t playback_epoch);

std::string BuildRealtimeVoiceMcpMessage(const std::string& session_id,
                                         const std::string& payload);

}  // namespace rodakos
