#include "apps/wol/wol_device_codec.h"

#include "phone_os/wake_on_lan_packet.h"

#include <cJSON.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace rodakos {
namespace {

bool IsValidDeviceName(const std::string& name) {
    if (name.empty() || name.size() > kWolDeviceNameMaxBytes) {
        return false;
    }
    for (unsigned char character : name) {
        if (character < 0x20 || character == 0x7f) {
            return false;
        }
    }
    return true;
}

bool ReadString(cJSON* object, const char* key, std::string* value) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr || value == nullptr) {
        return false;
    }
    *value = item->valuestring;
    return true;
}

}  // namespace

WolDeviceValidationStatus NormalizeWolDevice(WolDevice* device) {
    if (device == nullptr || !IsValidDeviceName(device->name)) {
        return WolDeviceValidationStatus::kInvalidName;
    }

    std::array<uint8_t, kWakeOnLanMacBytes> mac{};
    if (!ParseWakeOnLanMac(device->mac_address, &mac)) {
        return WolDeviceValidationStatus::kInvalidMac;
    }
    device->mac_address = FormatWakeOnLanMac(mac);

    std::string normalized_address;
    if (!NormalizeWakeOnLanIpv4(device->broadcast_address, &normalized_address)) {
        return WolDeviceValidationStatus::kInvalidBroadcastAddress;
    }
    device->broadcast_address = std::move(normalized_address);

    return device->port == 0 ? WolDeviceValidationStatus::kInvalidPort
                             : WolDeviceValidationStatus::kOk;
}

WolDeviceDecodeResult DecodeWolDevices(const std::string& json) {
    WolDeviceDecodeResult result;
    if (json.size() > kWolDeviceJsonMaxBytes) {
        result.status = WolDeviceCodecStatus::kTooLarge;
        return result;
    }

    cJSON* root = cJSON_ParseWithLength(json.data(), json.size());
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return result;
    }

    cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || !std::isfinite(version->valuedouble) ||
        version->valuedouble < 0 || std::floor(version->valuedouble) != version->valuedouble) {
        cJSON_Delete(root);
        return result;
    }
    result.source_version = static_cast<uint32_t>(version->valuedouble);
    if (result.source_version != kWolDeviceSchemaVersion) {
        result.status = WolDeviceCodecStatus::kUnsupportedVersion;
        cJSON_Delete(root);
        return result;
    }

    cJSON* devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
    if (!cJSON_IsArray(devices) || cJSON_GetArraySize(devices) > static_cast<int>(kWolMaxDevices)) {
        cJSON_Delete(root);
        return result;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, devices) {
        if (!cJSON_IsObject(item)) {
            result.status = WolDeviceCodecStatus::kInvalidDevice;
            cJSON_Delete(root);
            return result;
        }
        WolDevice device;
        cJSON* port = cJSON_GetObjectItemCaseSensitive(item, "port");
        if (!ReadString(item, "name", &device.name) ||
            !ReadString(item, "mac", &device.mac_address) ||
            !ReadString(item, "broadcast", &device.broadcast_address) ||
            !cJSON_IsNumber(port) || port->valuedouble < 1 || port->valuedouble > 65535 ||
            std::floor(port->valuedouble) != port->valuedouble) {
            result.status = WolDeviceCodecStatus::kInvalidDevice;
            cJSON_Delete(root);
            return result;
        }
        device.port = static_cast<uint16_t>(port->valuedouble);
        if (NormalizeWolDevice(&device) != WolDeviceValidationStatus::kOk) {
            result.status = WolDeviceCodecStatus::kInvalidDevice;
            cJSON_Delete(root);
            return result;
        }
        result.devices.push_back(std::move(device));
    }

    result.status = WolDeviceCodecStatus::kOk;
    cJSON_Delete(root);
    return result;
}

WolDeviceEncodeResult EncodeWolDevices(const std::vector<WolDevice>& devices) {
    WolDeviceEncodeResult result;
    if (devices.size() > kWolMaxDevices) {
        result.status = WolDeviceCodecStatus::kInvalidDevice;
        return result;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* array = cJSON_CreateArray();
    if (root == nullptr || array == nullptr ||
        cJSON_AddNumberToObject(root, "version", kWolDeviceSchemaVersion) == nullptr ||
        !cJSON_AddItemToObject(root, "devices", array)) {
        cJSON_Delete(array);
        cJSON_Delete(root);
        return result;
    }

    for (const WolDevice& source : devices) {
        WolDevice device = source;
        if (NormalizeWolDevice(&device) != WolDeviceValidationStatus::kOk) {
            result.status = WolDeviceCodecStatus::kInvalidDevice;
            cJSON_Delete(root);
            return result;
        }
        cJSON* item = cJSON_CreateObject();
        if (item == nullptr || cJSON_AddStringToObject(item, "name", device.name.c_str()) == nullptr ||
            cJSON_AddStringToObject(item, "mac", device.mac_address.c_str()) == nullptr ||
            cJSON_AddStringToObject(item, "broadcast", device.broadcast_address.c_str()) == nullptr ||
            cJSON_AddNumberToObject(item, "port", device.port) == nullptr ||
            !cJSON_AddItemToArray(array, item)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            return result;
        }
    }

    char* encoded = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (encoded == nullptr) {
        return result;
    }
    result.json = encoded;
    cJSON_free(encoded);
    if (result.json.size() > kWolDeviceJsonMaxBytes) {
        result.json.clear();
        result.status = WolDeviceCodecStatus::kTooLarge;
        return result;
    }
    result.status = WolDeviceCodecStatus::kOk;
    return result;
}

}  // namespace rodakos
