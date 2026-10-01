#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace rodakos {
inline constexpr size_t kAppearanceMaxDesiredBytes = 1024;
struct AppearanceDesired {
    std::string deployment_id;
    std::string release_id;
    std::string key_id;
    std::string mode;
    uint32_t revision = 0;
};
bool DecodeAppearanceDesired(const std::string& encoded, AppearanceDesired& desired);
struct AppearanceRevisionState {
    uint32_t active = 0;
    uint32_t pending = 0;
    uint32_t previous = 0;
    uint32_t trial = 0;
    uint32_t rejected = 0;
};
bool BeginAppearanceTrial(AppearanceRevisionState& state);
bool ConfirmAppearanceTrial(AppearanceRevisionState& state, uint32_t selected_revision);
bool RecoverAppearanceTrial(AppearanceRevisionState& state);
bool ShouldAcceptAppearanceRevision(const AppearanceRevisionState& state, uint32_t revision, uint32_t latest_revision);
bool ShouldResetAppearanceEpoch(const std::string& old_key, const std::string& old_origin,
                                const std::string& new_key, const std::string& new_origin);
bool ShouldRetryAppearanceAuthentication(int status, unsigned attempt);
enum class AppearanceRangeAction { kReject, kRestart, kAppend };
AppearanceRangeAction ValidateAppearanceRangeResponse(int status, size_t prefix_size,
    size_t total_size, int64_t response_length, const std::string& content_range);
uint32_t AppearanceDownloadRetryDelay(unsigned failures);
}  // namespace rodakos
