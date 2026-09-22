#pragma once

#include <cstdint>
#include <string>

namespace rodakos {

enum class VoiceIdentityApplyMode {
    kPersistent,
    kTemporary,
};

struct VoiceIdentityConfig {
    std::string name;
    std::string wake_word;
    std::string wake_command;
    VoiceIdentityApplyMode mode = VoiceIdentityApplyMode::kPersistent;
    uint32_t revision = 1;
    int64_t expires_at_ms = 0;
};

VoiceIdentityConfig DefaultVoiceIdentityConfig();
bool NormalizeVoiceIdentityConfig(const VoiceIdentityConfig& input,
                                  VoiceIdentityConfig& output,
                                  std::string& error);
bool IsVoiceIdentityExpired(const VoiceIdentityConfig& config, int64_t now_ms);

}  // namespace rodakos
