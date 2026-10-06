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
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch < 0x20 || ch == 0x7f;
    });
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
    error.clear();
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
        error = "voice identity revision must be positive";
        return false;
    }
    if (output.mode != VoiceIdentityApplyMode::kPersistent &&
        output.mode != VoiceIdentityApplyMode::kTemporary) {
        error = "voice identity mode is invalid";
        return false;
    }
    if (output.mode == VoiceIdentityApplyMode::kTemporary &&
        (output.expires_at_ms <= 0 || output.expires_at_ms > 9'007'199'254'740'991LL)) {
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

bool VoiceIdentityConfigEquals(const VoiceIdentityConfig& left, const VoiceIdentityConfig& right) {
    return left.name == right.name && left.wake_word == right.wake_word &&
           left.wake_command == right.wake_command && left.mode == right.mode &&
           left.revision == right.revision && left.expires_at_ms == right.expires_at_ms;
}

}  // namespace rodakos
