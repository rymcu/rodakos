#pragma once

#include <string>

namespace rodakos {
class VoiceAudioFrontend;
bool HandleVoiceAecDiagnosticCommand(VoiceAudioFrontend& frontend, const std::string& command);
}  // namespace rodakos
