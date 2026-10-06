#pragma once

#include <cstddef>
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
struct VoiceIdentityRecord {
    VoiceIdentityConfig persistent = DefaultVoiceIdentityConfig();
    VoiceIdentityConfig active = DefaultVoiceIdentityConfig();
    // Expiry restores active, but must not reopen an older accepted revision.
    VoiceIdentityConfig last_accepted = DefaultVoiceIdentityConfig();
};

bool VoiceIdentityConfigEquals(const VoiceIdentityConfig& left, const VoiceIdentityConfig& right);
bool EncodeVoiceIdentityRecord(const VoiceIdentityRecord& record, std::string& json,
                               std::string& error);
bool DecodeVoiceIdentityRecord(const std::string& json, VoiceIdentityRecord& record,
                               std::string& error);
constexpr size_t kVoiceIdentityRecordMaxBytes = 4096;
bool NormalizeVoiceIdentityConfig(const VoiceIdentityConfig& input,
                                  VoiceIdentityConfig& output,
                                  std::string& error);
bool IsVoiceIdentityExpired(const VoiceIdentityConfig& config, int64_t now_ms);

}  // namespace rodakos
