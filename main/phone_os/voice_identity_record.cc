#include "phone_os/voice_identity.h"

#include <cJSON.h>

#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <utility>

namespace rodakos {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
constexpr double kMaxExactInteger = 9'007'199'254'740'991.0;

bool HasFields(const cJSON* object, std::initializer_list<const char*> fields) {
    if (!cJSON_IsObject(object) || cJSON_GetArraySize(object) != static_cast<int>(fields.size()))
        return false;
    for (const char* field : fields) {
        size_t count = 0;
        for (const cJSON* child = object->child; child; child = child->next)
            if (child->string && std::strcmp(child->string, field) == 0) ++count;
        if (count != 1) return false;
    }
    return true;
}

bool IsInteger(const cJSON* value, double minimum, double maximum) {
    return cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
           value->valuedouble >= minimum && value->valuedouble <= maximum &&
           std::floor(value->valuedouble) == value->valuedouble;
}

bool ValidConfig(const VoiceIdentityConfig& config) {
    VoiceIdentityConfig normalized;
    std::string error;
    return config.revision > 0 &&
           (config.mode == VoiceIdentityApplyMode::kPersistent ||
            config.mode == VoiceIdentityApplyMode::kTemporary) &&
           config.expires_at_ms >= 0 && config.expires_at_ms <= 9'007'199'254'740'991LL &&
           NormalizeVoiceIdentityConfig(config, normalized, error) &&
           VoiceIdentityConfigEquals(config, normalized);
}

bool ValidRecord(const VoiceIdentityRecord& record) {
    if (!ValidConfig(record.persistent) || !ValidConfig(record.active) ||
        !ValidConfig(record.last_accepted) ||
        record.persistent.mode != VoiceIdentityApplyMode::kPersistent) return false;
    if (record.last_accepted.mode == VoiceIdentityApplyMode::kPersistent)
        return VoiceIdentityConfigEquals(record.persistent, record.last_accepted) &&
               VoiceIdentityConfigEquals(record.active, record.persistent);
    return record.last_accepted.revision > record.persistent.revision &&
           (VoiceIdentityConfigEquals(record.active, record.last_accepted) ||
            VoiceIdentityConfigEquals(record.active, record.persistent));
}

bool DecodeConfig(const cJSON* object, VoiceIdentityConfig& config) {
    if (!HasFields(object, {"name", "wakeWord", "wakeCommand", "mode", "revision", "expiresAtMs"}))
        return false;
    const auto* name = cJSON_GetObjectItemCaseSensitive(object, "name");
    const auto* word = cJSON_GetObjectItemCaseSensitive(object, "wakeWord");
    const auto* command = cJSON_GetObjectItemCaseSensitive(object, "wakeCommand");
    const auto* mode = cJSON_GetObjectItemCaseSensitive(object, "mode");
    const auto* revision = cJSON_GetObjectItemCaseSensitive(object, "revision");
    const auto* expiry = cJSON_GetObjectItemCaseSensitive(object, "expiresAtMs");
    if (!cJSON_IsString(name) || !cJSON_IsString(word) || !cJSON_IsString(command) ||
        !cJSON_IsString(mode) || !IsInteger(revision, 1, std::numeric_limits<uint32_t>::max()) ||
        !IsInteger(expiry, 0, kMaxExactInteger)) return false;
    if (std::strcmp(mode->valuestring, "persistent") == 0)
        config.mode = VoiceIdentityApplyMode::kPersistent;
    else if (std::strcmp(mode->valuestring, "temporary") == 0)
        config.mode = VoiceIdentityApplyMode::kTemporary;
    else return false;
    config.name = name->valuestring;
    config.wake_word = word->valuestring;
    config.wake_command = command->valuestring;
    config.revision = static_cast<uint32_t>(revision->valuedouble);
    config.expires_at_ms = static_cast<int64_t>(expiry->valuedouble);
    return ValidConfig(config);
}

cJSON* EncodeConfig(const VoiceIdentityConfig& config) {
    Json object(cJSON_CreateObject(), &cJSON_Delete);
    if (!object || !cJSON_AddStringToObject(object.get(), "name", config.name.c_str()) ||
        !cJSON_AddStringToObject(object.get(), "wakeWord", config.wake_word.c_str()) ||
        !cJSON_AddStringToObject(object.get(), "wakeCommand", config.wake_command.c_str()) ||
        !cJSON_AddStringToObject(object.get(), "mode", config.mode == VoiceIdentityApplyMode::kPersistent
                                                           ? "persistent" : "temporary") ||
        !cJSON_AddNumberToObject(object.get(), "revision", config.revision) ||
        !cJSON_AddNumberToObject(object.get(), "expiresAtMs", static_cast<double>(config.expires_at_ms)))
        return nullptr;
    return object.release();
}

bool HasInvalidStructure(const std::string& json) {
    bool in_string = false;
    int depth = 0;
    for (size_t i = 0; i < json.size(); ++i) {
        if (json[i] == '\0') return true;
        if (json[i] == '\\' && in_string) {
            if (json.compare(i, 6, "\\u0000") == 0) return true;
            ++i;
        } else if (json[i] == '"') {
            in_string = !in_string;
        } else if (!in_string) {
            if (json[i] == '[' || json[i] == ']') return true;
            if (json[i] == '{' && ++depth > 2) return true;
            if (json[i] == '}' && --depth < 0) return true;
        }
    }
    return in_string || depth != 0;
}
}  // namespace

bool EncodeVoiceIdentityRecord(const VoiceIdentityRecord& record, std::string& json,
                               std::string& error) {
    json.clear();
    error.clear();
    if (!ValidRecord(record)) {
        error = "invalid voice identity record";
        return false;
    }
    Json root(cJSON_CreateObject(), &cJSON_Delete);
    if (!root || !cJSON_AddNumberToObject(root.get(), "schema", 1)) {
        error = "voice identity allocation failed";
        return false;
    }
    const auto add = [&](const char* key, const VoiceIdentityConfig& config) {
        Json value(EncodeConfig(config), &cJSON_Delete);
        if (!value || !cJSON_AddItemToObject(root.get(), key, value.get())) return false;
        value.release();
        return true;
    };
    if (!add("persistent", record.persistent) || !add("active", record.active) ||
        !add("lastAccepted", record.last_accepted)) {
        error = "voice identity allocation failed";
        return false;
    }
    char* encoded = cJSON_PrintUnformatted(root.get());
    if (!encoded) {
        error = "voice identity allocation failed";
        return false;
    }
    json = encoded;
    cJSON_free(encoded);
    if (json.size() > kVoiceIdentityRecordMaxBytes) {
        json.clear();
        error = "voice identity record too large";
        return false;
    }
    return true;
}

bool DecodeVoiceIdentityRecord(const std::string& json, VoiceIdentityRecord& record,
                               std::string& error) {
    error = "invalid voice identity record";
    if (json.empty() || json.size() > kVoiceIdentityRecordMaxBytes || HasInvalidStructure(json)) return false;
    Json root(cJSON_ParseWithLengthOpts(json.c_str(), json.size() + 1, nullptr, true), &cJSON_Delete);
    if (!HasFields(root.get(), {"schema", "persistent", "active", "lastAccepted"}) ||
        !IsInteger(cJSON_GetObjectItemCaseSensitive(root.get(), "schema"), 1, 1)) return false;
    VoiceIdentityRecord parsed;
    if (!DecodeConfig(cJSON_GetObjectItemCaseSensitive(root.get(), "persistent"), parsed.persistent) ||
        !DecodeConfig(cJSON_GetObjectItemCaseSensitive(root.get(), "active"), parsed.active) ||
        !DecodeConfig(cJSON_GetObjectItemCaseSensitive(root.get(), "lastAccepted"), parsed.last_accepted) ||
        !ValidRecord(parsed)) return false;
    record = std::move(parsed);
    error.clear();
    return true;
}
}  // namespace rodakos
