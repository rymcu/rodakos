#pragma once

#include "phone_os/voice_identity.h"

namespace rodakos {

bool LoadVoiceWakeSettings(bool& enabled);
bool SaveVoiceWakeSettings(bool enabled);
bool LoadVoiceWakeIdentitySettings(VoiceIdentityConfig& persistent,
                                   VoiceIdentityConfig& active);
bool SaveVoiceWakeIdentitySettings(const VoiceIdentityConfig& persistent,
                                   const VoiceIdentityConfig& active);

}  // namespace rodakos
