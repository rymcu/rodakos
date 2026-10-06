#include "phone_os/mqtt_volume_effect.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/realtime_voice_contract.h"

#include <cJSON.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <utility>

namespace rodakos {
namespace {
constexpr size_t kMaxPayload = 256 * 1024;
constexpr size_t kMaxLedger = 64;
constexpr size_t kMaxResponse = 2048;
constexpr double kMaxSafeInteger = 9007199254740991.0;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

const cJSON* Get(const cJSON* object, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(object, key);
}
bool Text(const cJSON* value, const char* expected) {
    return cJSON_IsString(value) && value->valuestring != nullptr &&
           std::strcmp(value->valuestring, expected) == 0;
}
bool Integer(const cJSON* value, double low, double high) {
    return cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
           std::floor(value->valuedouble) == value->valuedouble &&
           value->valuedouble >= low && value->valuedouble <= high;
}
bool Unique(const cJSON* object) {
    if (!cJSON_IsObject(object)) return false;
    size_t count = 0;
    for (const cJSON* item = object->child; item != nullptr; item = item->next) {
        if (++count > 256 || item->string == nullptr) return false;
    }
    for (const cJSON* item = object->child; item != nullptr; item = item->next) {
        for (const cJSON* next = item->next; next != nullptr; next = next->next) {
            if (next->string == nullptr || std::strcmp(item->string, next->string) == 0)
                return false;
        }
    }
    return true;
}
bool Keys(const cJSON* object, std::initializer_list<const char*> allowed) {
    if (!Unique(object)) return false;
    for (const cJSON* item = object->child; item != nullptr; item = item->next) {
        if (std::none_of(allowed.begin(), allowed.end(), [item](const char* key) {
            return std::strcmp(item->string, key) == 0;
        })) return false;
    }
    return true;
}
bool Identifier(const cJSON* value, bool hash = false) {
    if (!cJSON_IsString(value) || value->valuestring == nullptr) return false;
    const size_t length = std::strlen(value->valuestring);
    if (hash ? length != 64 : (length == 0 || length > 128)) return false;
    return std::all_of(value->valuestring, value->valuestring + length, [hash](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= (hash ? 'f' : 'z')) ||
               (!hash && ((ch >= 'A' && ch <= 'Z') || ch == ':' || ch == '_' || ch == '-'));
    });
}
bool ShadowVersion(const cJSON* root, uint64_t& version) {
    const cJSON* primary = Get(root, "shadowVersion");
    const cJSON* alias = Get(root, "version");
    if (primary == nullptr && alias == nullptr) return false;
    if ((primary != nullptr && !Integer(primary, 1, kMaxSafeInteger)) ||
        (alias != nullptr && !Integer(alias, 1, kMaxSafeInteger)) ||
        (primary != nullptr && alias != nullptr && primary->valuedouble != alias->valuedouble))
        return false;
    version = static_cast<uint64_t>((primary != nullptr ? primary : alias)->valuedouble);
    return true;
}

struct Correlation {
    const char* effect_id;
    const char* parameters_hash;
    const char* dispatch_id;
    uint64_t shadow_version;
};

size_t FormatResult(char (&buffer)[kMaxResponse], const Correlation& correlation,
                    const char* error, const AudioVolumeResult* outcome = nullptr,
                    int requested = 0) {
    // All interpolated text is either a fixed literal or a validated ASCII identifier.
    const int header = std::snprintf(buffer, sizeof(buffer),
        "{\"schema\":\"rodakos.mqtt-volume-result.v1\",\"effectId\":\"%s\","
        "\"parametersHash\":\"%s\",\"dispatchId\":\"%s\",\"shadowVersion\":%llu,",
        correlation.effect_id, correlation.parameters_hash, correlation.dispatch_id,
        static_cast<unsigned long long>(correlation.shadow_version));
    if (header <= 0 || static_cast<size_t>(header) >= sizeof(buffer)) return 0;
    const size_t available = sizeof(buffer) - static_cast<size_t>(header);
    int body = 0;
    if (outcome == nullptr) {
        body = std::snprintf(buffer + header, available, "\"errorCode\":\"%s\"}", error);
    } else {
        const char* application = outcome->application == AudioVolumeApplication::kDeferred
            ? "deferred" : outcome->application == AudioVolumeApplication::kCodecApplied
                ? "codec-applied" : "unverified";
        body = std::snprintf(buffer + header, available,
            "\"receipt\":{\"schema\":\"rodakos.volume-receipt.v1\",\"effectId\":\"%s\","
            "\"parametersHash\":\"%s\",\"operation\":\"volume.set\",\"requested\":{\"volume\":%d},"
            "\"previousVolume\":%d,\"volume\":%d,\"configurationRevision\":%lu,"
            "\"status\":\"%s\",\"application\":\"%s\",\"persistence\":\"volatile\","
            "\"physicalVerified\":false%s}}",
            correlation.effect_id, correlation.parameters_hash, requested,
            outcome->previous_volume, outcome->volume,
            static_cast<unsigned long>(outcome->configuration_revision),
            outcome->accepted ? "configured" : "rejected", application,
            outcome->accepted ? "" : ",\"errorCode\":\"codec-volume-rejected\"");
    }
    return body > 0 && static_cast<size_t>(body) < available
        ? static_cast<size_t>(header + body) : 0;
}
std::string Result(const Correlation& correlation, const char* error) {
    char buffer[kMaxResponse];
    const size_t length = FormatResult(buffer, correlation, error);
    return std::string(buffer, length);
}

bool DesiredMatches(const cJSON* desired, int volume) {
    const cJSON* value = Get(desired, "volume");
    return Unique(desired) && Integer(value, 0, 100) && value->valuedouble == volume;
}
}  // namespace

MqttVolumeEffect::MqttVolumeEffect(AudioOutputService* output) : output_(output) {
    ledger_.reserve(kMaxLedger);
}

void MqttVolumeEffect::ResetAuthority() {
    std::lock_guard<std::mutex> lock(mutex_);
    ledger_.clear();
    volume_shadow_version_ = 0;
}

std::string MqttVolumeEffect::Handle(const std::string& payload, const std::string& device_key,
                                     bool& volume_handled) {
    std::lock_guard<std::mutex> lock(mutex_);
    volume_handled = true;
    if (payload.size() > kMaxPayload || !IsBoundedRealtimeVoiceControlJson(payload, 16)) return {};
    Json root(cJSON_ParseWithLengthOpts(payload.c_str(), payload.size() + 1, nullptr, true), cJSON_Delete);
    if (!Unique(root.get())) return {};
    const cJSON* meta = Get(root.get(), "_meta");
    if (meta != nullptr && !Unique(meta)) return {};
    const cJSON* correlation = Get(meta, "rodak/deviceEffect");
    const cJSON* primary = Get(root.get(), "desired");
    const cJSON* state = Get(root.get(), "state");
    const cJSON* nested = Get(state, "desired");
    const cJSON* desired = cJSON_IsObject(primary) ? primary : nested;
    const cJSON* volume = Get(desired, "volume");
    volume_handled = correlation != nullptr || volume != nullptr;
    if (correlation == nullptr) {
        if (!cJSON_IsNumber(volume) || !Unique(desired) ||
            (state != nullptr && !Unique(state))) return {};
        uint64_t version = 0;
        const bool versioned = ShadowVersion(root.get(), version);
        if ((Get(root.get(), "shadowVersion") != nullptr || Get(root.get(), "version") != nullptr) &&
            !versioned) return {};
        if (versioned && version <= volume_shadow_version_) return {};
        if (versioned) volume_shadow_version_ = version;
        if (output_ != nullptr) output_->SetVolume(std::clamp(volume->valueint, 0, 100));
        return {};
    }
    if (!Unique(correlation) || !Identifier(Get(correlation, "effectId")) ||
        !Identifier(Get(correlation, "parametersHash"), true) ||
        !Identifier(Get(correlation, "dispatchId")) ||
        !Integer(Get(correlation, "shadowVersion"), 1, kMaxSafeInteger)) return {};
    const Correlation identity{
        Get(correlation, "effectId")->valuestring,
        Get(correlation, "parametersHash")->valuestring,
        Get(correlation, "dispatchId")->valuestring,
        static_cast<uint64_t>(Get(correlation, "shadowVersion")->valuedouble)
    };
    const cJSON* requested = Get(correlation, "requested");
    const cJSON* requested_volume = Get(requested, "volume");
    uint64_t version = 0;
    if (!Keys(correlation, {"schema", "effectId", "parametersHash", "dispatchId", "shadowVersion", "operation", "requested"}) ||
        !Text(Get(correlation, "schema"), "rodak.mqtt-volume-effect.v1") ||
        !Text(Get(correlation, "operation"), "volume.set") ||
        !Keys(requested, {"volume"}) || !Integer(requested_volume, 0, 100) ||
        device_key.empty() || !Text(Get(root.get(), "deviceKey"), device_key.c_str()) ||
        !ShadowVersion(root.get(), version) || version != identity.shadow_version)
        return Result(identity, "invalid-request");
    const int amount = static_cast<int>(requested_volume->valuedouble);
    if ((primary == nullptr && nested == nullptr) ||
        (primary != nullptr && !DesiredMatches(primary, amount)) ||
        (state != nullptr && !Unique(state)) ||
        (nested != nullptr && !DesiredMatches(nested, amount)))
        return Result(identity, "invalid-request");

    const std::string request = std::to_string(device_key.size()) + ":" + device_key + ":" +
        identity.parameters_hash + ":" + identity.dispatch_id + ":" +
        std::to_string(identity.shadow_version) + ":" + std::to_string(amount);
    for (const auto& entry : ledger_) {
        if (entry.effect_id == identity.effect_id)
            return entry.request == request ? entry.response : Result(identity, "effect-conflict");
    }
    if (ledger_.size() >= kMaxLedger) return Result(identity, "effect-capacity");
    // Size both valid outcomes before the write: small devices must not reserve 2 KiB per entry.
    char response[kMaxResponse];
    AudioVolumeResult maximum{true, 100, 100, std::numeric_limits<uint32_t>::max(),
                              AudioVolumeApplication::kCodecApplied};
    const size_t configured_length = FormatResult(response, identity, nullptr, &maximum, 100);
    maximum.accepted = false;
    maximum.application = AudioVolumeApplication::kUnverified;
    const size_t rejected_length = FormatResult(response, identity, nullptr, &maximum, 100);
    if (configured_length == 0 || rejected_length == 0) return Result(identity, "outcome-unknown");
    // Allocate a terminal fallback before any side effect. Retries must never reconstruct an outcome.
    Entry entry{identity.effect_id, request, Result(identity, "outcome-unknown")};
    entry.response.reserve(std::max(configured_length, rejected_length));
    ledger_.push_back(std::move(entry));
    auto& saved = ledger_.back();
    if (version <= volume_shadow_version_) {
        saved.response = Result(identity, "stale-shadow");
        return saved.response;
    }
    volume_shadow_version_ = version;
    if (output_ == nullptr) return saved.response;
    const AudioVolumeResult outcome = output_->ApplyVolume(AudioVolumeOperation::kSet, amount);
    const size_t length = FormatResult(response, identity, nullptr, &outcome, amount);
    if (length != 0 && length <= saved.response.capacity()) saved.response.assign(response, length);
    return saved.response;
}
}  // namespace rodakos
