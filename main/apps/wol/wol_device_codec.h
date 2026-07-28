#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rodakos {

constexpr uint32_t kWolDeviceSchemaVersion = 1;
constexpr size_t kWolMaxDevices = 8;
constexpr size_t kWolDeviceNameMaxBytes = 32;
constexpr size_t kWolDeviceJsonMaxBytes = 4096;

struct WolDevice {
    std::string name;
    std::string mac_address;
    std::string broadcast_address = "255.255.255.255";
    uint16_t port = 9;
};

enum class WolDeviceValidationStatus {
    kOk,
    kInvalidName,
    kInvalidMac,
    kInvalidBroadcastAddress,
    kInvalidPort,
};

enum class WolDeviceCodecStatus {
    kOk,
    kTooLarge,
    kInvalidDocument,
    kUnsupportedVersion,
    kInvalidDevice,
    kEncodeError,
};

struct WolDeviceDecodeResult {
    WolDeviceCodecStatus status = WolDeviceCodecStatus::kInvalidDocument;
    std::vector<WolDevice> devices;
    uint32_t source_version = 0;
};

struct WolDeviceEncodeResult {
    WolDeviceCodecStatus status = WolDeviceCodecStatus::kEncodeError;
    std::string json;
};

WolDeviceValidationStatus NormalizeWolDevice(WolDevice* device);
WolDeviceDecodeResult DecodeWolDevices(const std::string& json);
WolDeviceEncodeResult EncodeWolDevices(const std::vector<WolDevice>& devices);

}  // namespace rodakos
