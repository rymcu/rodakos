#include "phone_os/voice_assistant_transport.h"

#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceTransport";
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

bool NoopVoiceAssistantTransport::SendAudio(const VoiceAudioPacket&, uint32_t) {
    return Reject("send audio");
}

bool NoopVoiceAssistantTransport::SendStartListening(VoiceListeningMode, uint32_t) {
    return Reject("start listening");
}

bool NoopVoiceAssistantTransport::SendStopListening(uint32_t) {
    return Reject("stop listening");
}

bool NoopVoiceAssistantTransport::SendWakeWordDetected(const std::string&, uint32_t) {
    return Reject("wake word");
}

bool NoopVoiceAssistantTransport::SendAbortSpeaking(VoiceAbortReason, uint32_t) {
    return Reject("abort speaking");
}

bool NoopVoiceAssistantTransport::SendMcpMessage(const std::string&, uint32_t) {
    return Reject("mcp message");
}

void NoopVoiceAssistantTransport::SetInboundHandler(VoiceInboundHandler) {
}

bool NoopVoiceAssistantTransport::Reject(const char* operation) {
    ESP_LOGW(TAG, "Cannot %s: voice transport not configured", operation);
    return false;
}

}  // namespace rodakos
