#include "phone_os/mqtt_light_effect.h"
#include "phone_os/light_service.h"
#include "phone_os/realtime_voice_contract.h"
#include <cJSON.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>

namespace rodakos {
namespace {
constexpr size_t kMaxLedger = 64;
constexpr size_t kMaxResponse = 2048;
constexpr double kMaxSafeInteger = 9007199254740991.0;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
const cJSON* Get(const cJSON* object, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(object, key);
}
bool Text(const cJSON* value, const char* expected) {
    return cJSON_IsString(value) && value->valuestring && std::strcmp(value->valuestring, expected) == 0;
}
bool Integer(const cJSON* value, double low, double high) {
    return cJSON_IsNumber(value) && std::isfinite(value->valuedouble) &&
           std::floor(value->valuedouble) == value->valuedouble &&
           value->valuedouble >= low && value->valuedouble <= high;
}
bool Unique(const cJSON* object) {
    if (!cJSON_IsObject(object)) return false;
    size_t count = 0;
    for (const cJSON* item = object->child; item; item = item->next)
        if (++count > 256 || !item->string) return false;
    for (const cJSON* item = object->child; item; item = item->next)
        for (const cJSON* next = item->next; next; next = next->next)
            if (std::strcmp(item->string, next->string) == 0) return false;
    return true;
}
bool Keys(const cJSON* object, std::initializer_list<const char*> allowed) {
    if (!Unique(object)) return false;
    for (const cJSON* item = object->child; item; item = item->next)
        if (std::none_of(allowed.begin(), allowed.end(), [item](const char* key) {
            return std::strcmp(item->string, key) == 0;
        })) return false;
    return true;
}
bool Identifier(const cJSON* value, bool hash = false) {
    if (!cJSON_IsString(value) || !value->valuestring) return false;
    const size_t size = std::strlen(value->valuestring);
    if (hash ? size != 64 : (size == 0 || size > 128)) return false;
    return std::all_of(value->valuestring, value->valuestring + size, [hash](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= (hash ? 'f' : 'z')) ||
               (!hash && ((ch >= 'A' && ch <= 'Z') || ch == ':' || ch == '_' || ch == '-'));
    });
}
bool ShadowVersion(const cJSON* root, uint64_t& version) {
    const auto* primary = Get(root, "shadowVersion");
    const auto* alias = Get(root, "version");
    if (!primary && !alias) return false;
    if ((primary && !Integer(primary, 1, kMaxSafeInteger)) ||
        (alias && !Integer(alias, 1, kMaxSafeInteger)) ||
        (primary && alias && primary->valuedouble != alias->valuedouble)) return false;
    version = static_cast<uint64_t>((primary ? primary : alias)->valuedouble);
    return true;
}
bool EffectId(const char* effect, uint64_t version) {
    const std::string prefix = "light:" + std::to_string(version) + ":";
    if (std::strncmp(effect, prefix.c_str(), prefix.size()) != 0) return false;
    const char* uuid = effect + prefix.size();
    if (std::strlen(uuid) != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (uuid[i] != '-') return false;
        } else if (!((uuid[i] >= '0' && uuid[i] <= '9') ||
                     (uuid[i] >= 'a' && uuid[i] <= 'f') ||
                     (uuid[i] >= 'A' && uuid[i] <= 'F'))) return false;
    }
    return true;
}
bool ParsePatch(const cJSON* json, LightPatch& patch) {
    if (!Keys(json, {"enabled", "brightness", "color"}) || !json->child) return false;
    if (const auto* value = Get(json, "enabled")) {
        if (!cJSON_IsBool(value)) return false;
        patch.enabled = cJSON_IsTrue(value);
    }
    if (const auto* value = Get(json, "brightness")) {
        if (!Integer(value, 0, 100)) return false;
        patch.brightness_percent = static_cast<int>(value->valuedouble);
    }
    if (const auto* color = Get(json, "color")) {
        if (!Keys(color, {"r", "g", "b"}) || !Integer(Get(color, "r"), 0, 255) ||
            !Integer(Get(color, "g"), 0, 255) || !Integer(Get(color, "b"), 0, 255)) return false;
        patch.color = RgbColor{static_cast<uint8_t>(Get(color, "r")->valueint),
                              static_cast<uint8_t>(Get(color, "g")->valueint),
                              static_cast<uint8_t>(Get(color, "b")->valueint)};
    }
    return true;
}
std::string PatchJson(const LightPatch& patch) {
    std::string result = "{";
    if (patch.enabled) result += std::string("\"enabled\":") + (*patch.enabled ? "true," : "false,");
    if (patch.brightness_percent) result += "\"brightness\":" + std::to_string(*patch.brightness_percent) + ",";
    if (patch.color) result += "\"color\":{\"r\":" + std::to_string(*patch.color->red) +
        ",\"g\":" + std::to_string(*patch.color->green) + ",\"b\":" +
        std::to_string(*patch.color->blue) + "},";
    result.back() = '}';
    return result;
}
bool DesiredMatches(const cJSON* desired, const char* id, const LightPatch& patch) {
    if (!Unique(desired)) return false;
    const auto* light = Get(desired, "light");
    if (!Unique(light) || !Text(Get(light, "id"), id)) return false;
    if (patch.enabled && (!cJSON_IsBool(Get(light, "enabled")) ||
        cJSON_IsTrue(Get(light, "enabled")) != *patch.enabled)) return false;
    if (patch.brightness_percent && (!Integer(Get(light, "brightness"), 0, 100) ||
        Get(light, "brightness")->valueint != *patch.brightness_percent)) return false;
    if (patch.color) {
        const auto* color = Get(light, "color");
        if (!Unique(color)) return false;
        for (const char* channel : {"r", "g", "b"})
            if (!Integer(Get(color, channel), 0, 255)) return false;
        if (Get(color, "r")->valueint != *patch.color->red ||
            Get(color, "g")->valueint != *patch.color->green ||
            Get(color, "b")->valueint != *patch.color->blue) return false;
    }
    return true;
}
struct Correlation { const char* effect; const char* hash; const char* dispatch; uint64_t version; };
size_t Format(char (&buffer)[kMaxResponse], const Correlation& c, const char* error,
              const char* requested = nullptr, const LightApplyResult* result = nullptr) {
    const int head = std::snprintf(buffer, sizeof(buffer),
        "{\"schema\":\"rodakos.mqtt-light-result.v1\",\"effectId\":\"%s\","
        "\"parametersHash\":\"%s\",\"dispatchId\":\"%s\",\"shadowVersion\":%llu,",
        c.effect, c.hash, c.dispatch, static_cast<unsigned long long>(c.version));
    if (head <= 0 || static_cast<size_t>(head) >= sizeof(buffer)) return 0;
    const size_t available = sizeof(buffer) - head;
    int body;
    if (!result) body = std::snprintf(buffer + head, available, "\"errorCode\":\"%s\"}", error);
    else {
        const auto& previous = result->previous;
        const auto& current = result->state;
        body = std::snprintf(buffer + head, available,
            "\"receipt\":{\"schema\":\"rodakos.light-receipt.v1\",\"effectId\":\"%s\","
            "\"parametersHash\":\"%s\",\"operation\":\"light.patch\",\"requested\":%s,"
            "\"previous\":{\"id\":\"%s\",\"enabled\":%s,\"brightness\":%u,\"color\":{\"r\":%u,\"g\":%u,\"b\":%u}},"
            "\"current\":{\"id\":\"%s\",\"enabled\":%s,\"brightness\":%u,\"color\":{\"r\":%u,\"g\":%u,\"b\":%u}},"
            "\"configurationRevision\":%lu,\"status\":\"%s\",\"application\":\"%s\","
            "\"persistence\":\"volatile\",\"physicalVerified\":false%s}}",
            c.effect, c.hash, requested,
            result->id.c_str(), previous.enabled ? "true" : "false", unsigned(previous.brightness_percent),
            unsigned(previous.color.red), unsigned(previous.color.green), unsigned(previous.color.blue),
            result->id.c_str(), current.enabled ? "true" : "false", unsigned(current.brightness_percent),
            unsigned(current.color.red), unsigned(current.color.green), unsigned(current.color.blue),
            static_cast<unsigned long>(result->configuration_revision),
            result->accepted ? "configured" : "rejected",
            result->accepted ? "driver-applied" : "unverified",
            result->accepted ? "" : ",\"errorCode\":\"light-driver-rejected\"");
    }
    return body > 0 && static_cast<size_t>(body) < available ? static_cast<size_t>(head + body) : 0;
}
std::string Error(const Correlation& c, const char* error) {
    char buffer[kMaxResponse];
    const size_t length = Format(buffer, c, error);
    return std::string(buffer, length);
}
}

MqttLightEffect::MqttLightEffect(LightService* lights) : lights_(lights) { ledger_.reserve(kMaxLedger); }
void MqttLightEffect::ResetAuthority() {
    std::lock_guard<std::mutex> lock(mutex_);
    ledger_.clear();
    watermarks_.clear();
    authority_version_ = 0;
}
std::string MqttLightEffect::Handle(const std::string& payload, const std::string& device_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (payload.size() > 256 * 1024 || !IsBoundedRealtimeVoiceControlJson(payload, 16)) return {};
    Json root(cJSON_ParseWithLengthOpts(payload.c_str(), payload.size() + 1, nullptr, true), cJSON_Delete);
    if (!Unique(root.get())) return {};
    const auto* meta = Get(root.get(), "_meta");
    if (meta && !Unique(meta)) return {};
    const auto* correlation = Get(meta, "rodak/deviceEffect");
    const auto* primary = Get(root.get(), "desired");
    const auto* state = Get(root.get(), "state");
    const auto* nested = Get(state, "desired");
    const auto* desired = cJSON_IsObject(primary) ? primary : nested;
    if (!lights_) return {};
    const auto lights = lights_->ListLights();
    if (!correlation) {
        const auto* light = Get(desired, "light");
        if (!Unique(desired) || !Unique(light) || (state && !Unique(state)) || lights.empty()) return {};
        const auto* id = Get(light, "id");
        const std::string target = id && Identifier(id) ? id->valuestring : lights.front().id;
        if (id && !Identifier(id)) return {};
        if (std::none_of(lights.begin(), lights.end(), [&](const LightState& item) { return item.id == target; })) return {};
        LightPatch patch;
        if (const auto* value = Get(light, "enabled"); cJSON_IsBool(value)) patch.enabled = cJSON_IsTrue(value);
        if (const auto* value = Get(light, "brightness"); cJSON_IsNumber(value))
            patch.brightness_percent = std::clamp(value->valueint, 0, 100);
        if (const auto* color = Get(light, "color"); cJSON_IsObject(color)) {
            if (!Unique(color)) return {};
            LightColorPatch channels;
            if (const auto* value = Get(color, "r"); cJSON_IsNumber(value)) channels.red = std::clamp(value->valueint, 0, 255);
            if (const auto* value = Get(color, "g"); cJSON_IsNumber(value)) channels.green = std::clamp(value->valueint, 0, 255);
            if (const auto* value = Get(color, "b"); cJSON_IsNumber(value)) channels.blue = std::clamp(value->valueint, 0, 255);
            if (channels.red || channels.green || channels.blue) patch.color = channels;
        }
        if (!patch.enabled && !patch.brightness_percent && !patch.color) return {};
        uint64_t version = 0;
        const bool versioned = ShadowVersion(root.get(), version);
        if ((Get(root.get(), "shadowVersion") || Get(root.get(), "version")) && !versioned) return {};
        auto mark = std::find_if(watermarks_.begin(), watermarks_.end(), [&](const Watermark& item) { return item.light_id == target; });
        if (mark == watermarks_.end()) { watermarks_.push_back({target, 0}); mark = watermarks_.end() - 1; }
        if (versioned && version <= std::max(mark->version, authority_version_)) return {};
        if (versioned) mark->version = authority_version_ = version;
        lights_->ApplyLightPatch(target, patch);
        return {};
    }
    if (!Unique(correlation) || !Identifier(Get(correlation, "effectId")) ||
        !Identifier(Get(correlation, "parametersHash"), true) || !Identifier(Get(correlation, "dispatchId")) ||
        !Integer(Get(correlation, "shadowVersion"), 1, kMaxSafeInteger)) return {};
    const Correlation c{Get(correlation, "effectId")->valuestring, Get(correlation, "parametersHash")->valuestring,
        Get(correlation, "dispatchId")->valuestring, static_cast<uint64_t>(Get(correlation, "shadowVersion")->valuedouble)};
    const auto* requested = Get(correlation, "requested");
    const auto* id = Get(requested, "lightId");
    LightPatch patch;
    uint64_t version = 0;
    if (!Keys(correlation, {"schema", "effectId", "parametersHash", "dispatchId", "shadowVersion", "operation", "requested"}) ||
        !Text(Get(correlation, "schema"), "rodak.mqtt-light-effect.v1") || !Text(Get(correlation, "operation"), "light.patch") ||
        !EffectId(c.effect, c.version) || !Keys(requested, {"lightId", "patch"}) || !Identifier(id) ||
        !ParsePatch(Get(requested, "patch"), patch) || device_key.empty() ||
        !Text(Get(root.get(), "deviceKey"), device_key.c_str()) || !ShadowVersion(root.get(), version) || version != c.version)
        return Error(c, "invalid-request");
    if ((primary == nullptr && nested == nullptr) || (primary && !DesiredMatches(primary, id->valuestring, patch)) ||
        (state && !Unique(state)) || (nested && !DesiredMatches(nested, id->valuestring, patch)) ||
        std::none_of(lights.begin(), lights.end(), [&](const LightState& item) { return item.id == id->valuestring; }))
        return Error(c, "invalid-request");
    const std::string requested_json = "{\"lightId\":\"" + std::string(id->valuestring) + "\",\"patch\":" + PatchJson(patch) + "}";
    const std::string request = device_key + ":" + c.hash + ":" + c.dispatch + ":" +
        std::to_string(version) + ":" + requested_json;
    for (const auto& entry : ledger_)
        if (entry.effect_id == c.effect) return entry.request == request ? entry.response : Error(c, "effect-conflict");
    auto mark = std::find_if(watermarks_.begin(), watermarks_.end(), [&](const Watermark& item) { return item.light_id == id->valuestring; });
    if (mark == watermarks_.end()) { watermarks_.push_back({id->valuestring, 0}); mark = watermarks_.end() - 1; }
    if (version <= std::max(mark->version, authority_version_)) return Error(c, "stale-shadow");
    // Reserve a terminal fallback and the largest outcome before touching the driver.
    LightApplyResult maximum;
    maximum.id = id->valuestring;
    maximum.previous = maximum.state = {false, 100, {255, 255, 255}};
    maximum.configuration_revision = std::numeric_limits<uint32_t>::max();
    const size_t capacity = Format(response_buffer_, c, nullptr, requested_json.c_str(), &maximum);
    if (!capacity) return Error(c, "outcome-unknown");
    Entry entry{c.effect, request, Error(c, "outcome-unknown")};
    entry.response.reserve(capacity);
    if (ledger_.size() == kMaxLedger) ledger_.erase(ledger_.begin());
    ledger_.push_back(std::move(entry));
    mark->version = authority_version_ = version;
    const auto result = lights_->ApplyLightPatch(id->valuestring, patch);
    const size_t length = Format(response_buffer_, c, nullptr, requested_json.c_str(), &result);
    auto& saved = ledger_.back();
    if (length && length <= saved.response.capacity()) saved.response.assign(response_buffer_, length);
    return saved.response;
}
}
