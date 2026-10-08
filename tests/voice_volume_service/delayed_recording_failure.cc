#include "delayed_recording_failure.h"
#include "phone_os/voice_assistant_service.h"

namespace rodakos_test {
// Only this host TU bypasses access checks to deliver a captured fault at its real endpoint.
// State is established by public Start/reconnect operations, never by private-field mutation.
void SubmitDelayedRecordingFailure(rodakos::VoiceAssistantService& service,
                                   uint32_t interaction_generation,
                                   uint32_t transport_generation) {
    service.FinishInteraction(rodakos::VoiceAssistantPhase::kError,
                              "Voice capture stopped unexpectedly", interaction_generation,
                              rodakos::CloudDiagnosticCode::kVoiceUnavailable, transport_generation);
}
}
