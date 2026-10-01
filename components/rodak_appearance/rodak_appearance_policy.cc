#include "rodak_appearance_policy.h"
#include "rodak_appearance.h"
#include "rodak_appearance_json.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace rodakos {
bool DecodeAppearanceDesired(const std::string& encoded, AppearanceDesired& desired) {
    desired = {};
    if (encoded.empty() || encoded.size() > kAppearanceMaxDesiredBytes || encoded.find('\0') != std::string::npos ||
        encoded.find("\\u0000") != std::string::npos) return false;
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(encoded.data(), encoded.size(), &end, false);
    while (end != nullptr && end < encoded.data() + encoded.size() &&
           (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) ++end;
    bool valid = cJSON_IsObject(root) && end == encoded.data() + encoded.size();
    constexpr const char* fields[] = {"deploymentId", "releaseId", "keyId", "mode", "revision"};
    unsigned seen = 0;
    for (const cJSON* item = valid ? root->child : nullptr; item != nullptr; item = item->next) {
        unsigned field = 0;
        while (field < 5 && (item->string == nullptr || std::strcmp(item->string, fields[field]) != 0)) ++field;
        if (field == 5 || (seen & (1u << field)) != 0) { valid = false; break; }
        seen |= 1u << field;
    }
    valid = valid && seen == 31;
    const auto text = [&](const char* name, std::string& value) {
        const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
        if (!cJSON_IsString(item) || item->valuestring == nullptr || std::strlen(item->valuestring) > 64) return false;
        value = item->valuestring; return true;
    };
    AppearanceDesired decoded;
    const cJSON* revision = cJSON_GetObjectItemCaseSensitive(root, "revision");
    valid = valid && text("deploymentId", decoded.deployment_id) && text("releaseId", decoded.release_id) &&
        text("keyId", decoded.key_id) && text("mode", decoded.mode) && IsAppearanceIdentifier(decoded.deployment_id) &&
        decoded.key_id.size() == 64 && std::all_of(decoded.key_id.begin(), decoded.key_id.end(), [](unsigned char ch) {
            return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
        }) && ((decoded.mode == "custom" && IsAppearanceIdentifier(decoded.release_id)) ||
               (decoded.mode == "builtin" && decoded.release_id.empty())) && cJSON_IsNumber(revision) &&
        std::isfinite(revision->valuedouble) && std::floor(revision->valuedouble) == revision->valuedouble &&
        revision->valuedouble >= 1 && revision->valuedouble <= 0xffffffffu;
    if (valid) decoded.revision = static_cast<uint32_t>(revision->valuedouble);
    cJSON_Delete(root);
    if (valid) desired = std::move(decoded);
    return valid;
}
bool BeginAppearanceTrial(AppearanceRevisionState& state) {
    if (state.trial != 0 || state.pending == 0 || state.pending <= state.active || state.pending <= state.rejected) return false;
    state.previous = state.active; state.trial = state.pending; return true;
}
bool ConfirmAppearanceTrial(AppearanceRevisionState& state, uint32_t selected_revision) {
    if (state.trial == 0 || state.trial != state.pending || state.trial != selected_revision) return false;
    state.active = state.pending; state.pending = 0; state.trial = 0; return true;
}
bool RecoverAppearanceTrial(AppearanceRevisionState& state) {
    if (state.trial == 0) return false;
    if (state.trial > state.rejected) state.rejected = state.trial;
    state.active = state.previous; state.pending = 0; state.trial = 0; return true;
}
bool ShouldAcceptAppearanceRevision(const AppearanceRevisionState& state, uint32_t revision, uint32_t latest_revision) {
    return revision != 0 && revision > state.active && revision > state.pending && revision > state.rejected && revision >= latest_revision;
}
bool ShouldResetAppearanceEpoch(const std::string& old_key, const std::string& old_origin,
                                const std::string& new_key, const std::string& new_origin) {
    return old_key != new_key || old_origin != new_origin;
}
bool ShouldRetryAppearanceAuthentication(int status, unsigned attempt) { return status == 401 && attempt == 0; }
AppearanceRangeAction ValidateAppearanceRangeResponse(int status, size_t prefix_size,
    size_t total_size, int64_t response_length, const std::string& content_range) {
    if (total_size == 0 || total_size > kAppearanceMaxPackageBytes || prefix_size >= total_size) return AppearanceRangeAction::kReject;
    if (status == 200) {
        return content_range.empty() && (response_length < 0 || static_cast<uint64_t>(response_length) == total_size)
                   ? AppearanceRangeAction::kRestart : AppearanceRangeAction::kReject;
    }
    if (status != 206 || prefix_size == 0 || response_length < 0 ||
        static_cast<uint64_t>(response_length) != total_size - prefix_size || content_range.rfind("bytes ", 0) != 0 || content_range.size() > 96) return AppearanceRangeAction::kReject;
    size_t position = 6;
    const auto number = [&](uint64_t& value) {
        value = 0; const size_t first = position;
        while (position < content_range.size() && content_range[position] >= '0' && content_range[position] <= '9') {
            value = value * 10 + static_cast<unsigned>(content_range[position++] - '0');
            if (value > kAppearanceMaxPackageBytes) return false;
        }
        return position != first;
    };
    uint64_t start = 0, end = 0, total = 0;
    if (!number(start) || position >= content_range.size() || content_range[position++] != '-' || !number(end) ||
        position >= content_range.size() || content_range[position++] != '/' || !number(total) || position != content_range.size() ||
        start != prefix_size || total != total_size || end != total_size - 1) return AppearanceRangeAction::kReject;
    return AppearanceRangeAction::kAppend;
}
uint32_t AppearanceDownloadRetryDelay(unsigned failures) {
    constexpr uint32_t delays[] = {5000, 15000, 60000};
    return failures >= 1 && failures <= 3 ? delays[failures - 1] : 0;
}
}  // namespace rodakos
