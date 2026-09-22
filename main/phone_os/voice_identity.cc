#include "phone_os/voice_identity.h"

#include <algorithm>
#include <cctype>

namespace rodakos {
namespace {
constexpr size_t kMaxNameLength = 32;
constexpr size_t kMaxWakeWordLength = 32;
constexpr size_t kMaxWakeCommandLength = 128;

std::string Trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool HasControlCharacter(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) { return ch < 0x20; });
}

}  // namespace

VoiceIdentityConfig DefaultVoiceIdentityConfig() {
    VoiceIdentityConfig config;
    config.name = "罗达克";
    config.wake_word = "你好达克";
    config.wake_command = "ni hao da ke";
    config.mode = VoiceIdentityApplyMode::kPersistent;
    config.revision = 1;
    config.expires_at_ms = 0;
    return config;
}

bool NormalizeVoiceIdentityConfig(const VoiceIdentityConfig& input,
                                  VoiceIdentityConfig& output,
                                  std::string& error) {
    output = input;
    output.name = Trim(output.name);
    output.wake_word = Trim(output.wake_word);
    output.wake_command = Trim(output.wake_command);

    if (output.name.empty() || output.name.size() > kMaxNameLength ||
        output.wake_word.empty() || output.wake_word.size() > kMaxWakeWordLength ||
        output.wake_command.empty() || output.wake_command.size() > kMaxWakeCommandLength) {
        error = "voice identity field length is invalid";
        return false;
    }
    if (HasControlCharacter(output.name) || HasControlCharacter(output.wake_word) ||
        HasControlCharacter(output.wake_command)) {
        error = "voice identity contains control characters";
        return false;
    }
    if (output.revision == 0) {
        output.revision = 1;
    }
    if (output.mode == VoiceIdentityApplyMode::kTemporary && output.expires_at_ms <= 0) {
        error = "temporary voice identity requires expires_at";
        return false;
    }
    if (output.mode == VoiceIdentityApplyMode::kPersistent) {
        output.expires_at_ms = 0;
    }
    return true;
}

bool IsVoiceIdentityExpired(const VoiceIdentityConfig& config, int64_t now_ms) {
    if (config.mode != VoiceIdentityApplyMode::kTemporary || config.expires_at_ms <= 0) {
        return false;
    }
    return config.expires_at_ms <= now_ms;
}

}  // namespace rodakos
