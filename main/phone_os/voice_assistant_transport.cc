#include "phone_os/voice_assistant_transport.h"

#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceTransport";
}

bool NoopVoiceAssistantTransport::Start() {
    return Reject("start");
}

bool NoopVoiceAssistantTransport::OpenAudioChannel() {
    return Reject("open audio channel");
}

void NoopVoiceAssistantTransport::CloseAudioChannel() {
}

bool NoopVoiceAssistantTransport::IsAudioChannelOpen() const {
    return false;
}

bool NoopVoiceAssistantTransport::SendAudio(const VoiceAudioPacket&) {
    return Reject("send audio");
}

bool NoopVoiceAssistantTransport::SendStartListening(VoiceListeningMode) {
    return Reject("start listening");
}

bool NoopVoiceAssistantTransport::SendStopListening() {
    return Reject("stop listening");
}

bool NoopVoiceAssistantTransport::SendWakeWordDetected(const std::string&) {
    return Reject("wake word");
}

bool NoopVoiceAssistantTransport::SendAbortSpeaking(VoiceAbortReason) {
    return Reject("abort speaking");
}

bool NoopVoiceAssistantTransport::SendMcpMessage(const std::string&) {
    return Reject("mcp message");
}

bool NoopVoiceAssistantTransport::Reject(const char* operation) {
    last_error_ = "Voice transport not configured";
    ESP_LOGW(TAG, "Cannot %s: %s", operation, last_error_.c_str());
    return false;
}

}  // namespace rodakos
