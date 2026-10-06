#include "phone_os/voice_volume_mcp.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/realtime_voice_contract.h"

#include <cJSON.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <initializer_list>

namespace rodakos {
namespace {
constexpr size_t kMaxPayload = 4096;
constexpr size_t kMaxLedger = 64;
constexpr size_t kMaxResponse = 3072;
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
bool Unique(const cJSON* value, int depth = 0) {
    if (depth > 8) return false;
    for (const cJSON* item = value->child; item != nullptr; item = item->next) {
        if (cJSON_IsObject(value)) {
            for (const cJSON* next = item->next; next != nullptr; next = next->next) {
                if (item->string == nullptr || next->string == nullptr ||
                    std::strcmp(item->string, next->string) == 0) return false;
            }
        }
        if (!Unique(item, depth + 1)) return false;
    }
    return true;
}
bool Keys(const cJSON* value, std::initializer_list<const char*> allowed) {
    if (!cJSON_IsObject(value)) return false;
    for (const cJSON* item = value->child; item != nullptr; item = item->next) {
        if (item->string == nullptr || std::none_of(allowed.begin(), allowed.end(),
            [item](const char* name) { return std::strcmp(name, item->string) == 0; })) return false;
    }
    return true;
}
bool Identifier(const cJSON* value, bool hash) {
    if (!cJSON_IsString(value) || value->valuestring == nullptr) return false;
    const size_t length = std::strlen(value->valuestring);
    if (hash ? length != 64 : (length == 0 || length > 128)) return false;
    return std::all_of(value->valuestring, value->valuestring + length, [hash](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= (hash ? 'f' : 'z')) ||
               (!hash && ((ch >= 'A' && ch <= 'Z') || ch == ':' || ch == '_' || ch == '-'));
    });
}
std::string Print(const cJSON* value) {
    char buffer[kMaxResponse];
    return value != nullptr && cJSON_PrintPreallocated(const_cast<cJSON*>(value), buffer,
        sizeof(buffer), false) ? std::string(buffer) : std::string();
}
std::string Error(const cJSON* id, int code, const char* message) {
    Json root(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(root.get(), "jsonrpc", "2.0");
    cJSON_AddItemToObject(root.get(), "id", id == nullptr ? cJSON_CreateNull() : cJSON_Duplicate(id, true));
    cJSON* error = cJSON_AddObjectToObject(root.get(), "error");
    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    return Print(root.get());
}
std::string Wrap(const cJSON* id, cJSON* result) {
    Json root(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(root.get(), "jsonrpc", "2.0");
    cJSON_AddItemToObject(root.get(), "id", cJSON_Duplicate(id, true));
    cJSON_AddItemToObject(root.get(), "result", result);
    return Print(root.get());
}
std::string Replay(const std::string& cached, const cJSON* id) {
    Json root(cJSON_Parse(cached.c_str()), cJSON_Delete);
    if (!root) return Error(id, -32603, "Cached response unavailable");
    cJSON_ReplaceItemInObjectCaseSensitive(root.get(), "id", cJSON_Duplicate(id, true));
    return Print(root.get());
}
const char* kTools = R"json({"tools":[
{"name":"self.audio_speaker.set_volume","description":"设置音量（0-100）。用户可能说：音量调到30","inputSchema":{"type":"object","properties":{"volume":{"type":"integer","minimum":0,"maximum":100}},"required":["volume"],"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false}},
{"name":"self.audio_speaker.volume_up","description":"调高音量，省略step时增加10。用户可能说：声音大一点、音量调高","inputSchema":{"type":"object","properties":{"step":{"type":"integer","minimum":1,"maximum":100,"default":10}},"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false}},
{"name":"self.audio_speaker.volume_down","description":"调低音量，省略step时减少10。用户可能说：声音小一点、音量调低","inputSchema":{"type":"object","properties":{"step":{"type":"integer","minimum":1,"maximum":100,"default":10}},"additionalProperties":false},"annotations":{"readOnlyHint":false,"destructiveHint":false}}
]})json";
}  // namespace

VoiceVolumeMcp::VoiceVolumeMcp(AudioOutputService& output) : output_(output) {
    ledger_.reserve(kMaxLedger);
}

bool VoiceVolumeMcp::Bind(uint32_t generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation == 0 || generation == last_generation_ ||
        (last_generation_ != 0 && generation < last_generation_ &&
         !(last_generation_ == UINT32_MAX && generation == 1))) return false;
    generation_ = generation;
    last_generation_ = generation;
    initialized_ = false;
    ledger_.clear();
    return true;
}

void VoiceVolumeMcp::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    generation_ = 0;
    initialized_ = false;
    ledger_.clear();
}

std::string VoiceVolumeMcp::Handle(const std::string& payload, uint32_t generation,
                                  const std::function<bool()>& can_execute) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto current = [&]() {
        return generation != 0 && generation == generation_ && (!can_execute || can_execute());
    };
    if (!current()) return {};
    if (payload.size() > kMaxPayload || !IsBoundedRealtimeVoiceControlJson(payload, 8))
        return Error(nullptr, -32600, "MCP request exceeds limits");
    Json root(cJSON_ParseWithLengthOpts(payload.c_str(), payload.size() + 1, nullptr, true), cJSON_Delete);
    if (!root) return Error(nullptr, -32700, "Invalid JSON");
    const cJSON* id = Get(root.get(), "id");
    const bool valid_id = (cJSON_IsString(id) && id->valuestring != nullptr &&
                          std::strlen(id->valuestring) <= 128) ||
                         Integer(id, -9007199254740991.0, 9007199254740991.0);
    if (!Keys(root.get(), {"jsonrpc", "id", "method", "params"}) || !Unique(root.get()) ||
        !Text(Get(root.get(), "jsonrpc"), "2.0") || (id != nullptr && !valid_id))
        return Error(valid_id ? id : nullptr, -32600, "Invalid JSON-RPC request");
    const cJSON* method = Get(root.get(), "method");
    if (!cJSON_IsString(method)) return Error(id, -32600, "Missing method");
    // Notifications never carry side effects; initialized is the sole supported notification.
    if (id == nullptr) return {};
    const cJSON* params = Get(root.get(), "params");
    if (Text(method, "initialize")) {
        if (params != nullptr && (!Keys(params, {"protocolVersion", "capabilities", "clientInfo"}) ||
            (Get(params, "protocolVersion") != nullptr && !Text(Get(params, "protocolVersion"), "2024-11-05"))))
            return Error(id, -32602, "Unsupported initialize parameters");
        if (!current()) return {};
        initialized_ = true;
        return Wrap(id, cJSON_Parse(R"json({"protocolVersion":"2024-11-05","capabilities":{"tools":{}},"serverInfo":{"name":"RodakOS Volume","version":"1"}})json"));
    }
    if (!initialized_) return Error(id, -32000, "MCP session is not initialized");
    if (Text(method, "tools/list")) {
        if (params != nullptr && (!Keys(params, {"cursor", "withUserTools"}) ||
            (Get(params, "cursor") != nullptr && !Text(Get(params, "cursor"), "")) ||
            (Get(params, "withUserTools") != nullptr && !cJSON_IsBool(Get(params, "withUserTools")))))
            return Error(id, -32602, "Invalid tools/list parameters");
        return Wrap(id, cJSON_Parse(kTools));
    }
    if (!Text(method, "tools/call")) return Error(id, -32601, "Method not found");
    if (!Keys(params, {"name", "arguments", "_meta"}))
        return Error(id, -32602, "Invalid tools/call parameters");
    const cJSON* name = Get(params, "name");
    const bool absolute = Text(name, "self.audio_speaker.set_volume");
    const bool up = Text(name, "self.audio_speaker.volume_up");
    const bool down = Text(name, "self.audio_speaker.volume_down");
    if (!absolute && !up && !down) return Error(id, -32602, "Unknown volume tool");
    const cJSON* args = Get(params, "arguments");
    const cJSON* value = args != nullptr ? Get(args, absolute ? "volume" : "step") : nullptr;
    if ((args != nullptr && !Keys(args, absolute ? std::initializer_list<const char*>{"volume"} : std::initializer_list<const char*>{"step"})) ||
        (absolute && !Integer(value, 0, 100)) || (!absolute && value != nullptr && !Integer(value, 1, 100)))
        return Error(id, -32602, "Invalid volume arguments");
    const int amount = value == nullptr ? 10 : static_cast<int>(value->valuedouble);
    const cJSON* meta = Get(params, "_meta");
    const cJSON* correlation = meta == nullptr ? nullptr : Get(meta, "rodak/deviceEffect");
    if ((meta != nullptr && !cJSON_IsObject(meta)) || (correlation != nullptr && (
        !Keys(correlation, {"effectId", "parametersHash"}) ||
        !Identifier(Get(correlation, "effectId"), false) ||
        !Identifier(Get(correlation, "parametersHash"), true))))
        return Error(id, -32602, "Invalid effect correlation");
    const std::string effect_id = correlation == nullptr ? "" : Get(correlation, "effectId")->valuestring;
    const std::string parameters_hash = correlation == nullptr ? "" : Get(correlation, "parametersHash")->valuestring;
    const std::string identity = Print(id);
    const std::string request = std::string(name->valuestring) + ":" +
        (value == nullptr ? "default" : std::to_string(amount)) + ":" + effect_id + ":" + parameters_hash;
    for (const auto& entry : ledger_) {
        if (entry.id == identity) {
            return entry.request == request ? entry.response : Error(id, -32001, "Request ID conflict");
        }
    }
    const auto repeated_effect = effect_id.empty() ? ledger_.end() : std::find_if(ledger_.begin(), ledger_.end(),
        [&effect_id](const Entry& entry) { return entry.effect_id == effect_id; });
    if (repeated_effect != ledger_.end() && repeated_effect->request != request)
        return Error(id, -32001, "Effect ID conflict");
    if (ledger_.size() >= kMaxLedger) return Error(id, -32003, "Session volume ledger is full");
    if (!current()) return {};
    if (repeated_effect != ledger_.end()) {
        const std::string response = Replay(repeated_effect->response, id);
        ledger_.push_back({identity, request, effect_id, response});
        return response;
    }
    // Reserve the non-replayable entry before touching the codec, including allocation failure paths.
    ledger_.push_back({identity, request, effect_id, Error(id, -32603, "Volume outcome unavailable")});
    auto& entry = ledger_.back();
    entry.response.reserve(768 + identity.size() + effect_id.size() + parameters_hash.size());
    if (!current()) return {};
    const AudioVolumeResult outcome = output_.ApplyVolume(absolute ? AudioVolumeOperation::kSet :
        up ? AudioVolumeOperation::kUp : AudioVolumeOperation::kDown, amount);
    Json result(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddBoolToObject(result.get(), "isError", !outcome.accepted);
    cJSON* content = cJSON_AddArrayToObject(result.get(), "content");
    cJSON* text = cJSON_CreateObject();
    cJSON_AddStringToObject(text, "type", "text");
    cJSON_AddStringToObject(text, "text", outcome.accepted ?
        "音量软件配置已接受，实际扬声器效果尚未验证" : "音量配置失败，硬件状态尚未验证");
    cJSON_AddItemToArray(content, text);
    {
        cJSON* receipt = cJSON_AddObjectToObject(result.get(), "structuredContent");
        cJSON_AddStringToObject(receipt, "schema", "rodakos.volume-receipt.v1");
        if (correlation != nullptr) {
            cJSON_AddStringToObject(receipt, "effectId", effect_id.c_str());
            cJSON_AddStringToObject(receipt, "parametersHash", parameters_hash.c_str());
        }
        cJSON_AddStringToObject(receipt, "operation", absolute ? "volume.set" : "volume.adjust");
        cJSON* requested = cJSON_AddObjectToObject(receipt, "requested");
        if (absolute) cJSON_AddNumberToObject(requested, "volume", amount);
        else {
            cJSON_AddStringToObject(requested, "direction", up ? "up" : "down");
            if (value != nullptr) cJSON_AddNumberToObject(requested, "step", amount);
            cJSON_AddNumberToObject(receipt, "effectiveStep", amount);
        }
        cJSON_AddNumberToObject(receipt, "previousVolume", outcome.previous_volume);
        cJSON_AddNumberToObject(receipt, "volume", outcome.volume);
        cJSON_AddNumberToObject(receipt, "configurationRevision", outcome.configuration_revision);
        cJSON_AddStringToObject(receipt, "status", outcome.accepted ? "configured" : "rejected");
        cJSON_AddStringToObject(receipt, "application", outcome.application == AudioVolumeApplication::kDeferred ?
            "deferred" : outcome.application == AudioVolumeApplication::kCodecApplied ? "codec-applied" : "unverified");
        cJSON_AddStringToObject(receipt, "persistence", "volatile");
        cJSON_AddBoolToObject(receipt, "physicalVerified", false);
        if (!outcome.accepted) cJSON_AddStringToObject(receipt, "errorCode", "codec-volume-rejected");
    }
    const std::string response = Wrap(id, result.release());
    if (!response.empty()) entry.response = response;
    return entry.response;
}
}  // namespace rodakos
