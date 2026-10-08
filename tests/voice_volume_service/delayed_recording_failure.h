#pragma once

#include <cstdint>

namespace rodakos { class VoiceAssistantService; }
namespace rodakos_test {
void SubmitDelayedRecordingFailure(rodakos::VoiceAssistantService& service,
                                   uint32_t interaction_generation,
                                   uint32_t transport_generation);
}
