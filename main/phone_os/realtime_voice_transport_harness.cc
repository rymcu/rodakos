#include "phone_os/realtime_voice_transport_harness.h"

namespace rodakos {

bool RealtimeVoiceTransportHarness::OpenSession(uint32_t generation,
                                                const std::string& session_id) {
    if (!gate_.Establish(generation, session_id)) {
        return false;
    }
    session_open_ = true;
    last_audio_payload_.clear();
    return true;
}

void RealtimeVoiceTransportHarness::CloseSession() {
    gate_.Clear();
    session_open_ = false;
    last_audio_payload_.clear();
}

bool RealtimeVoiceTransportHarness::ReceiveOutputStart(uint32_t playback_epoch) {
    return ReceiveOutputStartForGeneration(gate_.generation(), playback_epoch);
}

bool RealtimeVoiceTransportHarness::ReceiveOutputStop(uint32_t playback_epoch) {
    return ReceiveOutputStopForGeneration(gate_.generation(), playback_epoch);
}

bool RealtimeVoiceTransportHarness::ReceiveOutputStopForGeneration(uint32_t generation,
                                                                   uint32_t playback_epoch) {
    return session_open_ && gate_.AcceptOutputStop(generation, playback_epoch);
}

bool RealtimeVoiceTransportHarness::ReceiveSessionReadyForGeneration(
    uint32_t generation, const std::string& session_id) {
    if (!session_open_ || !gate_.Establish(generation, session_id)) {
        return false;
    }
    return true;
}

bool RealtimeVoiceTransportHarness::ReceiveSessionEndForGeneration(uint32_t generation) {
    if (!session_open_ || generation != gate_.generation()) {
        return false;
    }
    CloseSession();
    return true;
}

bool RealtimeVoiceTransportHarness::ReceiveErrorForGeneration(uint32_t generation) {
    if (!session_open_ || generation != gate_.generation()) {
        return false;
    }
    CloseSession();
    return true;
}

bool RealtimeVoiceTransportHarness::ReceiveDisconnectForGeneration(uint32_t generation) {
    if (!session_open_ || generation != gate_.generation()) {
        return false;
    }
    CloseSession();
    return true;
}

bool RealtimeVoiceTransportHarness::ReceiveAudio(const uint8_t* data,
                                                 size_t size,
                                                 size_t max_payload_size,
                                                 std::string& error) {
    return ReceiveAudioForGeneration(gate_.generation(), data, size, max_payload_size, error);
}

bool RealtimeVoiceTransportHarness::ReceiveOutputStartForGeneration(uint32_t generation,
                                                                    uint32_t playback_epoch) {
    return session_open_ && gate_.AcceptOutputStart(generation, playback_epoch);
}

bool RealtimeVoiceTransportHarness::ReceiveAudioForGeneration(uint32_t generation,
                                                              const uint8_t* data,
                                                              size_t size,
                                                              size_t max_payload_size,
                                                              std::string& error) {
    if (!session_open_ || !gate_.output_active()) {
        error = "audio requires active output";
        return false;
    }
    RealtimeVoiceAudioFrame frame;
    if (!ParseRealtimeVoiceAudioFrame(data, size, max_payload_size, frame, error)) {
        return false;
    }
    if (!gate_.AcceptAudio(generation, frame.sequence)) {
        error = "audio sequence is stale or out of order";
        return false;
    }
    last_audio_payload_.assign(frame.payload, frame.payload + frame.payload_size);
    return true;
}

}  // namespace rodakos
