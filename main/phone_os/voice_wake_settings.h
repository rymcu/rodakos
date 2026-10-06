#pragma once

#include "phone_os/voice_identity.h"

namespace rodakos {

bool LoadVoiceWakeSettings(bool& enabled);
bool SaveVoiceWakeSettings(bool enabled);
enum class VoiceIdentitySaveStatus { kSaved, kUnchanged, kIndeterminate };

bool LoadVoiceWakeIdentitySettings(VoiceIdentityRecord& record, std::string& error);
VoiceIdentitySaveStatus SaveVoiceWakeIdentitySettings(const VoiceIdentityRecord& record,
                                                     std::string& error);

}  // namespace rodakos
