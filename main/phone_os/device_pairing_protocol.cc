#include "phone_os/device_pairing_protocol.h"

#include <cJSON.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

namespace rodakos {
namespace {

struct CJsonDeleter {
    void operator()(cJSON* value) const {
        cJSON_Delete(value);
    }
};

using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;

void ReadString(const cJSON* object, const char* primary_key,
                const char* alias_key, std::string& output) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, primary_key);
    if (!cJSON_IsString(value) && alias_key != nullptr) {
        value = cJSON_GetObjectItemCaseSensitive(object, alias_key);
    }
    if (cJSON_IsString(value) && value->valuestring != nullptr) {
        output = value->valuestring;
    }
}

std::string NormalizeStatus(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) == 0;
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) == 0;
    }).base(), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

}  // namespace

bool ParseDevicePairingResponse(const std::string& json,
                                DevicePairingResponseType type,
                                DevicePairingResponse& response,
                                std::string& error) {
    CJsonPtr root(cJSON_ParseWithLength(json.data(), json.size()));
    if (!cJSON_IsObject(root.get())) {
        error = "Pairing response is not a JSON object";
        return false;
    }

    const cJSON* code = cJSON_GetObjectItemCaseSensitive(root.get(), "code");
    if (code != nullptr && (!cJSON_IsNumber(code) || code->valueint != 200)) {
        const cJSON* message = cJSON_GetObjectItemCaseSensitive(root.get(), "message");
        error = cJSON_IsString(message) && message->valuestring != nullptr
                    ? message->valuestring
                    : "Pairing request was rejected by the server";
        return false;
    }

    const cJSON* data = cJSON_GetObjectItemCaseSensitive(root.get(), "data");
    if (data == nullptr) {
        data = root.get();
    }
    if (!cJSON_IsObject(data)) {
        error = "Pairing response data is not an object";
        return false;
    }

    DevicePairingResponse parsed;
    ReadString(data, "requestId", "request_id", parsed.request_id);
    ReadString(data, "requestToken", "request_token", parsed.request_token);
    ReadString(data, "pairingCode", "pairing_code", parsed.pairing_code);
    ReadString(data, "expiresAt", "expires_at", parsed.expires_at);
    ReadString(data, "status", nullptr, parsed.raw_status);
    parsed.raw_status = NormalizeStatus(parsed.raw_status);

    if (type == DevicePairingResponseType::kCreateRequest) {
        if (parsed.request_id.empty() || parsed.request_token.empty() ||
            parsed.pairing_code.empty()) {
            error = "Pairing request response is incomplete";
            return false;
        }
        if (parsed.raw_status.empty()) {
            parsed.raw_status = "pending";
        }
    } else if (parsed.raw_status.empty()) {
        error = "Pairing status response has no status";
        return false;
    }

    parsed.status = ClassifyDevicePairingStatus(parsed.raw_status);
    response = std::move(parsed);
    error.clear();
    return true;
}

}  // namespace rodakos
