#pragma once

#include "phone_os/realtime_voice_contract.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rodakos {

// Host-testable stand-in for the websocket/FreeRTOS event boundary. It drives
// the same session gate and RAV1 parser used by the firmware transport.
class RealtimeVoiceTransportHarness {
public:
    bool OpenSession(uint32_t generation, const std::string& session_id);
    void CloseSession();

    bool ReceiveOutputStart(uint32_t playback_epoch);
    bool ReceiveOutputStop(uint32_t playback_epoch);
    bool ReceiveOutputStopForGeneration(uint32_t generation, uint32_t playback_epoch);
    // Simulate websocket lifecycle callbacks. Stale generations must be
    // ignored exactly like the production transport callback boundary.
    bool ReceiveSessionReadyForGeneration(uint32_t generation,
                                          const std::string& session_id);
    bool ReceiveSessionEndForGeneration(uint32_t generation);
    bool ReceiveErrorForGeneration(uint32_t generation);
    bool ReceiveDisconnectForGeneration(uint32_t generation);
    bool ReceiveAudio(const uint8_t* data, size_t size, size_t max_payload_size,
                      std::string& error);
    bool ReceiveOutputStartForGeneration(uint32_t generation, uint32_t playback_epoch);
    bool ReceiveAudioForGeneration(uint32_t generation, const uint8_t* data, size_t size,
                                   size_t max_payload_size, std::string& error);

    bool session_open() const { return session_open_; }
    bool output_active() const { return gate_.output_active(); }
    uint32_t audio_sequence() const { return gate_.audio_sequence(); }
    const std::vector<uint8_t>& last_audio_payload() const { return last_audio_payload_; }

private:
    RealtimeVoiceSessionGate gate_;
    bool session_open_ = false;
    std::vector<uint8_t> last_audio_payload_;
};

}  // namespace rodakos
