#include "phone_os/voice_assistant_transport.h"

#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceTransport";

bool IsAuthenticationStatus(int status_code) {
    return status_code == 401 || status_code == 403;
}
}

VoiceTransportFailure ClassifyVoiceWebsocketCloseFailure(
    int close_code, uint32_t transport_generation) {
    VoiceTransportFailure failure;
    failure.kind = VoiceTransportFailureKind::kNetwork;
    failure.code = close_code == 0
        ? "websocket_disconnected"
        : "websocket_close_" + std::to_string(close_code);
    failure.message = "Voice websocket disconnected";
    failure.transport_generation = transport_generation;

    switch (close_code) {
        case 0:
        case 1001:
        case 1006:
            failure.retryable = true;
            break;
        case 1002:
            failure.kind = VoiceTransportFailureKind::kProtocol;
            break;
        case 1011:
        case 1012:
        case 1013:
            failure.kind = VoiceTransportFailureKind::kServer;
            failure.retryable = true;
            break;
        case 4001:
            failure.kind = VoiceTransportFailureKind::kCancelled;
            break;
        default:
            break;
    }
    return failure;
}

VoiceTransportFailure ClassifyVoiceWebsocketErrorFailure(
    int http_status_code, bool pong_timeout, uint32_t transport_generation) {
    VoiceTransportFailure failure;
    failure.kind = VoiceTransportFailureKind::kNetwork;
    failure.code = "websocket_error";
    failure.message = "Websocket error";
    failure.retryable = true;
    failure.transport_generation = transport_generation;

    if (IsAuthenticationStatus(http_status_code)) {
        failure.kind = VoiceTransportFailureKind::kAuthentication;
        failure.code = "websocket_auth_" + std::to_string(http_status_code);
        failure.retryable = false;
    } else if (http_status_code == 408) {
        failure.kind = VoiceTransportFailureKind::kTimeout;
        failure.code = "websocket_http_408";
    } else if (http_status_code == 429 || http_status_code >= 500) {
        failure.kind = VoiceTransportFailureKind::kServer;
        failure.code = "websocket_http_" + std::to_string(http_status_code);
    } else if (http_status_code != 0) {
        failure.kind = VoiceTransportFailureKind::kProtocol;
        failure.code = "websocket_http_" + std::to_string(http_status_code);
        failure.retryable = false;
    } else if (pong_timeout) {
        failure.kind = VoiceTransportFailureKind::kTimeout;
        failure.code = "websocket_pong_timeout";
    }
    return failure;
}

bool NoopVoiceAssistantTransport::Start() {
    return Reject("start");
}

bool NoopVoiceAssistantTransport::OpenAudioChannel(VoiceOpenGuard) {
    return Reject("open audio channel");
}

void NoopVoiceAssistantTransport::CloseAudioChannel() {
}

void NoopVoiceAssistantTransport::WaitForAudioChannelClosed() {
}

bool NoopVoiceAssistantTransport::IsAudioChannelOpen() const {
    return false;
}

bool NoopVoiceAssistantTransport::SendAudio(const VoiceAudioPacket&,
                                            uint32_t expected_generation) {
    return Reject("send audio", expected_generation);
}

bool NoopVoiceAssistantTransport::SendStartListening(VoiceListeningMode,
                                                     uint32_t expected_generation) {
    return Reject("start listening", expected_generation);
}

bool NoopVoiceAssistantTransport::SendStopListening(uint32_t expected_generation) {
    return Reject("stop listening", expected_generation);
}

bool NoopVoiceAssistantTransport::SendWakeWordDetected(
    const std::string&, uint32_t expected_generation) {
    return Reject("wake word", expected_generation);
}

bool NoopVoiceAssistantTransport::SendAbortSpeaking(
    VoiceAbortReason, uint32_t expected_generation, uint32_t) {
    return Reject("abort speaking", expected_generation);
}

bool NoopVoiceAssistantTransport::SendVadStart(const char*, uint32_t, uint32_t,
                                               uint32_t expected_generation,
                                               uint32_t) {
    return Reject("vad start", expected_generation);
}

bool NoopVoiceAssistantTransport::SendVadEnd(const char*, uint32_t, uint32_t,
                                             uint32_t expected_generation,
                                             uint32_t) {
    return Reject("vad end", expected_generation);
}

bool NoopVoiceAssistantTransport::SendMcpMessage(const std::string&,
                                                 uint32_t expected_generation) {
    return Reject("mcp message", expected_generation);
}

void NoopVoiceAssistantTransport::SetInboundHandler(VoiceInboundHandler) {
}

bool NoopVoiceAssistantTransport::Reject(const char* operation,
                                         uint32_t transport_generation) {
    last_failure_ = {};
    last_failure_.kind = VoiceTransportFailureKind::kConfiguration;
    last_failure_.code = "transport_not_configured";
    last_failure_.message = "Voice transport not configured";
    last_failure_.transport_generation = transport_generation;
    ESP_LOGW(TAG, "Cannot %s: voice transport not configured", operation);
    return false;
}

}  // namespace rodakos
