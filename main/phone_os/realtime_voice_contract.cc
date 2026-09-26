#include "phone_os/realtime_voice_contract.h"
#include "phone_os/voice_assistant_transport.h"

#include <cJSON.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace rodakos {

bool RealtimeVoiceSessionGate::Establish(uint32_t generation,
                                         const std::string& session_id) {
    if (generation == 0 || session_id.empty() || generation_ != 0 ||
        !session_id_.empty()) {
        return false;
    }
    generation_ = generation;
    session_id_ = session_id;
    playback_epoch_ = 0;
    audio_sequence_ = 0;
    output_active_ = false;
    return true;
}

void RealtimeVoiceSessionGate::Clear() {
    generation_ = 0;
    session_id_.clear();
    playback_epoch_ = 0;
    audio_sequence_ = 0;
    output_active_ = false;
}

bool RealtimeVoiceSessionGate::Matches(uint32_t generation,
                                       const std::string& session_id) const {
    return generation != 0 && generation_ == generation && !session_id_.empty() &&
           session_id_ == session_id;
}

bool RealtimeVoiceSessionGate::AcceptOutputStart(uint32_t generation,
                                                 uint32_t playback_epoch) {
    if (generation == 0 || generation_ != generation) return false;
    // Once the server has negotiated an explicit playback epoch, a later
    // output.start without an epoch is ambiguous and may be a delayed event
    // from an older utterance.  Servers that omit epochs entirely remain
    // compatible because playback_epoch_ stays zero in that mode.
    if (playback_epoch == 0 && this->playback_epoch_ != 0) return false;
    if (playback_epoch != 0 && playback_epoch <= playback_epoch_) return false;
    if (playback_epoch != 0) playback_epoch_ = playback_epoch;
    output_active_ = true;
    return true;
}

bool RealtimeVoiceSessionGate::AcceptOutputStop(uint32_t generation,
                                                uint32_t playback_epoch) {
    if (generation == 0 || generation_ != generation || !output_active_) return false;
    if (playback_epoch != 0 && playback_epoch != playback_epoch_) return false;
    output_active_ = false;
    return true;
}

bool RealtimeVoiceSessionGate::AcceptAudio(uint32_t generation, uint32_t sequence) {
    if (generation == 0 || generation_ != generation || !output_active_ || sequence == 0 ||
        sequence <= audio_sequence_) {
        return false;
    }
    audio_sequence_ = sequence;
    return true;
}
namespace {

std::string JsonToString(cJSON* root) {
    char* json = cJSON_PrintUnformatted(root);
    if (json == nullptr) {
        return "{}";
    }
    std::string result(json);
    cJSON_free(json);
    return result;
}

const cJSON* ObjectItem(const cJSON* object, const char* name) {
    return object != nullptr && cJSON_IsObject(object)
               ? cJSON_GetObjectItemCaseSensitive(object, name)
               : nullptr;
}

bool ReadRequiredString(const cJSON* object, const char* name, std::string& output) {
    const cJSON* value = ObjectItem(object, name);
    if (!cJSON_IsString(value) || value->valuestring == nullptr || value->valuestring[0] == '\0') {
        return false;
    }
    output = value->valuestring;
    return true;
}

bool ReadInteger(const cJSON* object, const char* name, int& output, bool required) {
    const cJSON* value = ObjectItem(object, name);
    if (value == nullptr) {
        return !required;
    }
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
        value->valuedouble != std::floor(value->valuedouble) ||
        value->valuedouble < static_cast<double>(INT_MIN) ||
        value->valuedouble > static_cast<double>(INT_MAX)) {
        return false;
    }
    output = static_cast<int>(value->valuedouble);
    return true;
}

bool ReadSize(const cJSON* object, const char* name, size_t& output, bool required) {
    const cJSON* value = ObjectItem(object, name);
    if (value == nullptr) {
        return !required;
    }
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
        value->valuedouble != std::floor(value->valuedouble) || value->valuedouble < 1 ||
        value->valuedouble > static_cast<double>(std::numeric_limits<size_t>::max())) {
        return false;
    }
    output = static_cast<size_t>(value->valuedouble);
    return true;
}

std::string LowerAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

std::string NormalizeFieldName(const std::string& value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const char character : LowerAscii(value)) {
        if (character != '_' && character != '-' && character != '.') {
            normalized.push_back(character);
        }
    }
    return normalized;
}

bool IsLegacyFieldName(const char* value) {
    if (value == nullptr) {
        return false;
    }
    const std::string lower = LowerAscii(value);
    const std::string normalized = NormalizeFieldName(value);
    return normalized.find("xiaozhi") != std::string::npos ||
           lower == "websocket" || lower == "websocketurl" ||
           lower == "websocket_url" || lower == "wsurl" ||
           lower == "hello" || lower == "listen" ||
           lower == "listen:start" || lower == "listen:stop" ||
           lower == "listen:detect" || lower == "tts" ||
           lower == "goodbye" || lower == "type" ||
           lower == "session_id" || lower == "audio_params" ||
           lower == "auth_mode" || lower == "protocol_version" ||
           lower == "sample_rate" || lower == "frame_duration" ||
           normalized == "sessionid" || normalized == "audioparams" ||
           normalized == "listenstart" || normalized == "listenstop" ||
           normalized == "listendetect";
}

bool IsLegacyMarker(const std::string& value) {
    const std::string lower = LowerAscii(value);
    return lower.find("xiaozhi") != std::string::npos ||
           lower.find("/xiaozhi") != std::string::npos ||
           lower.find("websocket") != std::string::npos ||
           lower == "hello" || lower == "listen" ||
           lower.rfind("listen:", 0) == 0 || lower == "tts" ||
           lower.rfind("tts:", 0) == 0 || lower == "goodbye";
}

bool ContainsLegacyFields(const cJSON* value) {
    if (value == nullptr) {
        return false;
    }
    if (cJSON_IsArray(value)) {
        cJSON* child = nullptr;
        cJSON_ArrayForEach(child, value) {
            if (ContainsLegacyFields(child)) {
                return true;
            }
        }
        return false;
    }
    if (!cJSON_IsObject(value)) {
        return false;
    }
    cJSON* child = nullptr;
    cJSON_ArrayForEach(child, value) {
        if (IsLegacyFieldName(child->string) || ContainsLegacyFields(child)) {
            return true;
        }
    }
    return false;
}

bool IsValidEndpoint(const std::string& endpoint) {
    const bool secure = endpoint.rfind("wss://", 0) == 0;
    const size_t scheme_length = secure ? 6 : 5;
    if (!secure && endpoint.rfind("ws://", 0) != 0) {
        return false;
    }
    if (endpoint.size() <= scheme_length) {
        return false;
    }
    for (const unsigned char character : endpoint) {
        if (character <= 0x20 || character == '\\' || character == '@' ||
            character == '?' || character == '#') {
            return false;
        }
    }

    const size_t path_start = endpoint.find('/', scheme_length);
    if (path_start == std::string::npos || path_start == scheme_length) {
        return false;
    }
    const std::string authority = endpoint.substr(scheme_length, path_start - scheme_length);
    if (authority.empty()) {
        return false;
    }

    const auto valid_port = [](const std::string& port) {
        if (port.empty() || port.size() > 5 ||
            !std::all_of(port.begin(), port.end(), [](char character) {
                return character >= '0' && character <= '9';
            })) {
            return false;
        }
        int value = 0;
        for (const char character : port) {
            value = value * 10 + (character - '0');
        }
        return value >= 1 && value <= 65535;
    };

    if (authority.front() == '[') {
        const size_t closing = authority.find(']');
        if (closing <= 1 || closing == std::string::npos) {
            return false;
        }
        const std::string literal = authority.substr(1, closing - 1);
        if (literal.find(':') == std::string::npos ||
            !std::all_of(literal.begin(), literal.end(), [](char character) {
                return std::isxdigit(static_cast<unsigned char>(character)) != 0 ||
                       character == ':' || character == '.';
            })) {
            return false;
        }
        if (closing + 1 < authority.size() &&
            (authority[closing + 1] != ':' ||
             !valid_port(authority.substr(closing + 2)))) {
            return false;
        }
    } else {
        if (authority.find(':') != authority.rfind(':')) {
            return false;
        }
        const size_t colon = authority.find(':');
        if (colon == 0) {
            return false;
        }
        const std::string host = authority.substr(0, colon);
        if (host.empty() ||
            !std::all_of(host.begin(), host.end(), [](char character) {
                return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
                       character == '.' || character == '-';
            })) {
            return false;
        }
        if (colon != std::string::npos && !valid_port(authority.substr(colon + 1))) {
            return false;
        }
    }

    const std::string lower = LowerAscii(endpoint);
    return lower.find("/xiaozhi") == std::string::npos &&
           lower.find("websocket") == std::string::npos;
}

bool ValidateStringArray(const cJSON* object, const char* name, bool required,
                         const char* const* allowed, size_t allowed_count,
                         std::vector<std::string>* output, bool reject_legacy_markers) {
    const cJSON* value = ObjectItem(object, name);
    if (value == nullptr) {
        return !required;
    }
    if (!cJSON_IsArray(value)) {
        return false;
    }
    if (cJSON_GetArraySize(value) > 32) {
        return false;
    }
    if (required && allowed != nullptr && cJSON_GetArraySize(value) == 0) {
        return false;
    }
    if (output != nullptr) {
        output->clear();
    }
    cJSON* child = nullptr;
    cJSON_ArrayForEach(child, value) {
        if (!cJSON_IsString(child) || child->valuestring == nullptr ||
            child->valuestring[0] == '\0' || std::strlen(child->valuestring) > 96 ||
            (reject_legacy_markers && IsLegacyMarker(child->valuestring))) {
            return false;
        }
        const std::string item = child->valuestring;
        if (allowed != nullptr &&
            std::find_if(allowed, allowed + allowed_count,
                         [&item](const char* candidate) { return item == candidate; }) ==
                allowed + allowed_count) {
            return false;
        }
        if (output != nullptr &&
            std::find(output->begin(), output->end(), item) != output->end()) {
            return false;
        }
        if (output != nullptr) {
            output->push_back(item);
        }
    }
    return true;
}

bool IsValidAudio(int sample_rate, int channels, int frame_duration_ms) {
    return (sample_rate == 8000 || sample_rate == 12000 || sample_rate == 16000 ||
            sample_rate == 24000 || sample_rate == 48000) &&
           channels == 1 &&
           IsSupportedRealtimeVoiceFrameDuration(frame_duration_ms);
}

bool IsKnownVadStrategy(const std::string& value) {
    return value == kRealtimeVoiceVadDeviceAuthoritative ||
           value == kRealtimeVoiceVadServerAuthoritative ||
           value == kRealtimeVoiceVadHybridFallback;
}

void AddAudio(cJSON* object, const char* name, int sample_rate, int channels,
              int frame_duration_ms) {
    cJSON* audio = cJSON_CreateObject();
    cJSON_AddStringToObject(audio, "codec", "opus");
    cJSON_AddNumberToObject(audio, "sampleRateHz", sample_rate);
    cJSON_AddNumberToObject(audio, "channels", channels);
    cJSON_AddNumberToObject(audio, "frameDurationMs", frame_duration_ms);
    cJSON_AddItemToObject(object, name, audio);
}

void AddEnvelope(cJSON* root, const char* event, const std::string& session_id) {
    cJSON_AddStringToObject(root, "event", event);
    if (std::strcmp(event, kRealtimeVoiceEventSessionOpen) != 0) {
        cJSON_AddStringToObject(root, "sessionId", session_id.c_str());
    }
}

}  // namespace

bool ParseRealtimeVoiceDescriptor(const cJSON* value,
                                  RealtimeVoiceDescriptor& descriptor,
                                  std::string& error) {
    if (!cJSON_IsObject(value)) {
        error = "realtimeVoice descriptor must be an object";
        return false;
    }

    if (ContainsLegacyFields(value)) {
        error = "legacy realtime voice fields are not accepted";
        return false;
    }

    RealtimeVoiceDescriptor parsed;
    std::string schema;
    std::string protocol;
    std::string transport;
    if (!ReadRequiredString(value, "schema", schema) ||
        !ReadRequiredString(value, "protocol", protocol) ||
        !ReadRequiredString(value, "transport", transport) ||
        !ReadRequiredString(value, "endpoint", parsed.endpoint) ||
        !ReadRequiredString(value, "authMode", parsed.auth_mode) ||
        !ReadInteger(value, "protocolVersion", parsed.protocol_version, true)) {
        error = "incomplete realtime voice descriptor";
        return false;
    }

    if (schema != kRodakRealtimeVoiceSchema ||
        protocol != kRodakRealtimeVoiceProtocol ||
        parsed.protocol_version != kRodakRealtimeVoiceProtocolVersion ||
        transport != kRealtimeVoiceTransportWebSocket ||
        parsed.auth_mode != kRealtimeVoiceAuthModeDeviceToken ||
        !IsValidEndpoint(parsed.endpoint)) {
        error = "unsupported realtime voice descriptor";
        return false;
    }

    const cJSON* uplink = ObjectItem(value, "uplink");
    const cJSON* downlink = ObjectItem(value, "downlink");
    std::string uplink_codec;
    std::string downlink_codec;
    if (!ReadRequiredString(uplink, "codec", uplink_codec) ||
        !ReadRequiredString(downlink, "codec", downlink_codec) ||
        !ReadInteger(uplink, "sampleRateHz", parsed.uplink_sample_rate_hz, true) ||
        !ReadInteger(uplink, "channels", parsed.uplink_channels, true) ||
        !ReadInteger(uplink, "frameDurationMs", parsed.uplink_frame_duration_ms, true) ||
        !ReadInteger(downlink, "sampleRateHz", parsed.downlink_sample_rate_hz, true) ||
        !ReadInteger(downlink, "channels", parsed.downlink_channels, true) ||
        !ReadInteger(downlink, "frameDurationMs", parsed.downlink_frame_duration_ms, true)) {
        error = "invalid realtime voice audio descriptor";
        return false;
    }
    if (uplink_codec != "opus" || downlink_codec != "opus" ||
        !IsValidAudio(parsed.uplink_sample_rate_hz, parsed.uplink_channels,
                      parsed.uplink_frame_duration_ms) ||
        !IsValidAudio(parsed.downlink_sample_rate_hz, parsed.downlink_channels,
                      parsed.downlink_frame_duration_ms)) {
        error = "invalid realtime voice audio descriptor";
        return false;
    }
    if (parsed.uplink_sample_rate_hz != 16000 ||
        parsed.uplink_frame_duration_ms != 60) {
        error = "unsupported realtime voice uplink format";
        return false;
    }

    const cJSON* limits = ObjectItem(value, "limits");
    if (!ReadSize(limits, "maxAudioFrameBytes", parsed.max_audio_frame_bytes, true) ||
        !ReadSize(limits, "maxControlBytes", parsed.max_control_bytes, true)) {
        error = "invalid realtime voice limits";
        return false;
    }
    if (parsed.max_audio_frame_bytes > 64 * 1024 ||
        parsed.max_control_bytes > 256 * 1024) {
        error = "realtime voice limits are too large";
        return false;
    }

    static constexpr const char* kCapabilities[] = {
        "session", "audio-input", "audio-output", "speech-boundaries",
        "interrupt", "assistant-events", "mcp"};
    static constexpr const char* kEvents[] = {
        kRealtimeVoiceEventSessionOpen, kRealtimeVoiceEventSessionReady,
        kRealtimeVoiceEventInputStart, kRealtimeVoiceEventInputStop,
        kRealtimeVoiceEventWakeDetected, kRealtimeVoiceEventPlaybackAbort,
        kRealtimeVoiceEventVad, kRealtimeVoiceEventOutputStart,
        kRealtimeVoiceEventOutputStop, kRealtimeVoiceEventSessionEnd,
        kRealtimeVoiceEventMcp, kRealtimeVoiceEventError};
    if (!ValidateStringArray(value, "features", true, nullptr, 0,
                             &parsed.features, true) ||
        !ValidateStringArray(value, "capabilities", true, kCapabilities,
                             sizeof(kCapabilities) / sizeof(kCapabilities[0]),
                             nullptr, true) ||
        !ValidateStringArray(value, "events", true, kEvents,
                             sizeof(kEvents) / sizeof(kEvents[0]), nullptr, true)) {
        error = "invalid realtime voice capability descriptor";
        return false;
    }

    const cJSON* vad_strategies = ObjectItem(value, "vadStrategies");
    if (vad_strategies == nullptr) {
        // Descriptors issued before VAD negotiation are safe only when the
        // server remains authoritative. Device VAD must never be inferred.
        parsed.vad_strategies = {kRealtimeVoiceVadServerAuthoritative};
        parsed.preferred_vad_strategy = kRealtimeVoiceVadServerAuthoritative;
    } else {
        static constexpr const char* kVadStrategies[] = {
            kRealtimeVoiceVadDeviceAuthoritative,
            kRealtimeVoiceVadServerAuthoritative,
            kRealtimeVoiceVadHybridFallback};
        if (!ValidateStringArray(value, "vadStrategies", true, kVadStrategies,
                                 sizeof(kVadStrategies) / sizeof(kVadStrategies[0]),
                                 &parsed.vad_strategies, false)) {
            error = "invalid realtime voice VAD strategies";
            return false;
        }
        const cJSON* preferred = ObjectItem(value, "preferredVadStrategy");
        if (preferred == nullptr) {
            parsed.preferred_vad_strategy = parsed.vad_strategies.front();
        } else if (!cJSON_IsString(preferred) || preferred->valuestring == nullptr ||
                   !IsKnownVadStrategy(preferred->valuestring) ||
                   std::find(parsed.vad_strategies.begin(), parsed.vad_strategies.end(),
                             preferred->valuestring) == parsed.vad_strategies.end()) {
            error = "invalid preferred realtime voice VAD strategy";
            return false;
        } else {
            parsed.preferred_vad_strategy = preferred->valuestring;
        }
    }

    descriptor = parsed;
    return true;
}

bool IsRealtimeVoiceVadStrategy(const std::string& value) {
    return IsKnownVadStrategy(value);
}

bool IsSupportedRealtimeVoiceFrameDuration(int frame_duration_ms) {
    return frame_duration_ms == 5 || frame_duration_ms == 10 || frame_duration_ms == 20 ||
           frame_duration_ms == 40 || frame_duration_ms == 60;
}

bool IsRealtimeVoiceMcpPayloadObject(const cJSON* envelope) {
    if (envelope == nullptr || !cJSON_IsObject(envelope)) {
        return false;
    }
    return cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(envelope, "payload"));
}

bool ParseRealtimeVoiceServerError(const cJSON* envelope,
                                   RealtimeVoiceServerError& server_error,
                                   std::string& error) {
    server_error = {};
    error.clear();
    if (envelope == nullptr || !cJSON_IsObject(envelope)) {
        error = "canonical server error envelope must be an object";
        return false;
    }

    const cJSON* event = cJSON_GetObjectItemCaseSensitive(envelope, "event");
    if (!cJSON_IsString(event) || event->valuestring == nullptr ||
        std::strcmp(event->valuestring, kRealtimeVoiceEventError) != 0) {
        error = "canonical server error event is invalid";
        return false;
    }

    RealtimeVoiceServerError parsed;
    const cJSON* retryable = cJSON_GetObjectItemCaseSensitive(envelope, "retryable");
    if (!ReadRequiredString(envelope, "code", parsed.code) ||
        !ReadRequiredString(envelope, "message", parsed.message) ||
        !cJSON_IsBool(retryable)) {
        error = "canonical server error payload is invalid";
        return false;
    }
    parsed.retryable = cJSON_IsTrue(retryable);
    server_error = std::move(parsed);
    return true;
}

bool ValidateRealtimeVoiceServerControlPayload(const cJSON* envelope,
                                               std::string& error) {
    error.clear();
    if (envelope == nullptr || !cJSON_IsObject(envelope)) {
        error = "canonical control envelope must be an object";
        return false;
    }
    const cJSON* event = cJSON_GetObjectItemCaseSensitive(envelope, "event");
    if (!cJSON_IsString(event) || event->valuestring == nullptr) {
        error = "canonical control event is missing";
        return false;
    }
    const std::string event_name = event->valuestring;
    auto optional_string = [&](const char* key) {
        const cJSON* value = cJSON_GetObjectItemCaseSensitive(envelope, key);
        return value == nullptr || (cJSON_IsString(value) && value->valuestring != nullptr &&
                                    value->valuestring[0] != '\0');
    };
    if (event_name == kRealtimeVoiceEventOutputStart && !optional_string("text")) {
        error = "canonical output.start text is invalid";
        return false;
    }
    if ((event_name == kRealtimeVoiceEventOutputStop ||
         event_name == kRealtimeVoiceEventSessionEnd) && !optional_string("reason")) {
        error = "canonical control reason is invalid";
        return false;
    }
    if (event_name == kRealtimeVoiceEventMcp && !IsRealtimeVoiceMcpPayloadObject(envelope)) {
        error = "canonical mcp payload must be an object";
        return false;
    }
    if (event_name == kRealtimeVoiceEventError) {
        RealtimeVoiceServerError server_error;
        if (!ParseRealtimeVoiceServerError(envelope, server_error, error)) {
            return false;
        }
    }
    return true;
}

bool ParseRealtimeVoiceAudioFrame(const uint8_t* data,
                                  size_t size,
                                  size_t max_payload_size,
                                  RealtimeVoiceAudioFrame& frame,
                                  std::string& error) {
    frame = {};
    error.clear();
    if (data == nullptr || size < kRealtimeVoiceAudioHeaderBytes) {
        error = "RAV1 frame is shorter than its header";
        return false;
    }
    if (std::memcmp(data, kRealtimeVoiceAudioMagic, 4) != 0) {
        error = "RAV1 frame has an invalid magic";
        return false;
    }
    const uint32_t sequence = (static_cast<uint32_t>(data[4]) << 24) |
                              (static_cast<uint32_t>(data[5]) << 16) |
                              (static_cast<uint32_t>(data[6]) << 8) |
                              static_cast<uint32_t>(data[7]);
    const uint32_t payload_size = (static_cast<uint32_t>(data[8]) << 24) |
                                  (static_cast<uint32_t>(data[9]) << 16) |
                                  (static_cast<uint32_t>(data[10]) << 8) |
                                  static_cast<uint32_t>(data[11]);
    if (sequence == 0) {
        error = "RAV1 frame sequence must be non-zero";
        return false;
    }
    if (payload_size == 0 || payload_size != size - kRealtimeVoiceAudioHeaderBytes) {
        error = "RAV1 frame payload length does not match the envelope";
        return false;
    }
    if (static_cast<size_t>(payload_size) > max_payload_size) {
        error = "RAV1 frame payload exceeds the negotiated limit";
        return false;
    }
    frame.sequence = sequence;
    frame.payload = data + kRealtimeVoiceAudioHeaderBytes;
    frame.payload_size = payload_size;
    return true;
}

std::string BuildRealtimeVoiceSessionOpenMessage(const RealtimeVoiceDescriptor& descriptor,
                                                 bool supports_mcp,
                                                 bool supports_device_vad_epoch,
                                                 uint32_t generation) {
    // Generation zero is reserved for the not-established state.  Emitting a
    // session.open frame with zero would let a peer associate the handshake
    // with an invalid/stale transport lifecycle, so fail closed at the
    // contract boundary instead of relying on every caller to pre-validate.
    if (generation == 0) {
        return {};
    }
    cJSON* root = cJSON_CreateObject();
    AddEnvelope(root, kRealtimeVoiceEventSessionOpen, "");
    cJSON_AddStringToObject(root, "protocol", kRodakRealtimeVoiceProtocol);
    cJSON_AddNumberToObject(root, "protocolVersion", descriptor.protocol_version);
    cJSON_AddNumberToObject(root, "generation", generation);
    const std::vector<std::string> fallback_vad_strategies = {
        kRealtimeVoiceVadServerAuthoritative};
    const std::vector<std::string>& vad_strategy_list = descriptor.vad_strategies.empty()
                                                              ? fallback_vad_strategies
                                                              : descriptor.vad_strategies;
    const std::string& preferred_vad_strategy =
        IsKnownVadStrategy(descriptor.preferred_vad_strategy) &&
                std::find(vad_strategy_list.begin(), vad_strategy_list.end(),
                          descriptor.preferred_vad_strategy) != vad_strategy_list.end()
            ? descriptor.preferred_vad_strategy
            : vad_strategy_list.front();
    cJSON* vad_strategies = cJSON_CreateArray();
    for (const std::string& strategy : vad_strategy_list) {
        cJSON_AddItemToArray(vad_strategies, cJSON_CreateString(strategy.c_str()));
    }
    cJSON_AddItemToObject(root, "vadStrategies", vad_strategies);
    cJSON_AddStringToObject(root, "preferredVadStrategy",
                            preferred_vad_strategy.c_str());
    cJSON* features = cJSON_CreateObject();
    cJSON_AddBoolToObject(features, "mcp", supports_mcp);
    // canonical gateway 会把该能力转发给现有 runtime adapter；统一使用
    // device_vad_epoch，避免设备 VAD 被静默降级为纯服务端 VAD。
    cJSON_AddNumberToObject(features, "device_vad_epoch",
                            supports_device_vad_epoch ? 1 : 0);
    cJSON_AddItemToObject(root, "features", features);
    AddAudio(root, "uplink", descriptor.uplink_sample_rate_hz, descriptor.uplink_channels,
             descriptor.uplink_frame_duration_ms);
    AddAudio(root, "downlink", descriptor.downlink_sample_rate_hz, descriptor.downlink_channels,
             descriptor.downlink_frame_duration_ms);
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return message;
}

std::string BuildRealtimeVoiceInputMessage(const char* event,
                                           const std::string& session_id,
                                           const std::string& mode) {
    if (event == nullptr || session_id.empty() ||
        (std::strcmp(event, kRealtimeVoiceEventInputStart) != 0 &&
         std::strcmp(event, kRealtimeVoiceEventInputStop) != 0) ||
        (!mode.empty() && mode != "auto-stop" && mode != "manual-stop" &&
         mode != "realtime")) {
        return {};
    }
    cJSON* root = cJSON_CreateObject();
    AddEnvelope(root, event != nullptr ? event : "", session_id);
    if (!mode.empty()) {
        cJSON_AddStringToObject(root, "mode", mode.c_str());
    }
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return message;
}

std::string BuildRealtimeVoiceWakeMessage(const std::string& session_id,
                                          const std::string& wake_word) {
    if (session_id.empty() || wake_word.empty()) {
        return {};
    }
    cJSON* root = cJSON_CreateObject();
    AddEnvelope(root, kRealtimeVoiceEventWakeDetected, session_id);
    cJSON_AddStringToObject(root, "text", wake_word.c_str());
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return message;
}

std::string BuildRealtimeVoicePlaybackAbortMessage(const std::string& session_id,
                                                   VoiceAbortReason reason,
                                                   uint32_t playback_epoch) {
    if (session_id.empty()) {
        return {};
    }
    const char* wire_reason = nullptr;
    switch (reason) {
        case VoiceAbortReason::kNone:
            wire_reason = "user";
            break;
        case VoiceAbortReason::kWakeWordDetected:
            wire_reason = "wake-word";
            break;
        case VoiceAbortReason::kVadDetected:
            // Rodak uses this reason to preserve the active capture during barge-in.
            wire_reason = "vad_detected";
            break;
        default:
            return {};
    }
    cJSON* root = cJSON_CreateObject();
    AddEnvelope(root, kRealtimeVoiceEventPlaybackAbort, session_id);
    cJSON_AddStringToObject(root, "reason", wire_reason);
    if (playback_epoch != 0) {
        cJSON_AddNumberToObject(root, "playbackEpoch", playback_epoch);
    }
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return message;
}

std::string BuildRealtimeVoiceVadMessage(const std::string& session_id,
                                         const char* state,
                                         const char* source,
                                         uint32_t sequence,
                                         uint32_t trigger_ms,
                                         uint32_t playback_epoch) {
    if (session_id.empty() || state == nullptr || source == nullptr || state[0] == '\0' ||
        source[0] == '\0' ||
        (std::strcmp(state, "start") != 0 && std::strcmp(state, "end") != 0) ||
        sequence == 0) {
        return {};
    }
    cJSON* root = cJSON_CreateObject();
    AddEnvelope(root, kRealtimeVoiceEventVad, session_id);
    cJSON_AddStringToObject(root, "state", state != nullptr ? state : "");
    cJSON_AddStringToObject(root, "source", source != nullptr ? source : "device");
    cJSON_AddNumberToObject(root, "sequence", sequence);
    cJSON_AddNumberToObject(root, "triggerMs", trigger_ms);
    if (playback_epoch != 0) {
        cJSON_AddNumberToObject(root, "playbackEpoch", playback_epoch);
    }
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return message;
}

std::string BuildRealtimeVoiceMcpMessage(const std::string& session_id,
                                          const std::string& payload) {
    cJSON* payload_json = cJSON_Parse(payload.c_str());
    if (!cJSON_IsObject(payload_json)) {
        cJSON_Delete(payload_json);
        return {};
    }

    cJSON* root = cJSON_CreateObject();
    AddEnvelope(root, kRealtimeVoiceEventMcp, session_id);
    cJSON_AddItemToObject(root, "payload", payload_json);
    std::string message = JsonToString(root);
    cJSON_Delete(root);
    return message;
}

}  // namespace rodakos
