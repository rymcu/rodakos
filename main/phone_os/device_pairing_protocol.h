#pragma once

#include "phone_os/device_pairing_policy.h"

#include <string>

namespace rodakos {

enum class DevicePairingResponseType {
    kCreateRequest,
    kStatus,
};

struct DevicePairingResponse {
    std::string request_id;
    std::string request_token;
    std::string pairing_code;
    std::string expires_at;
    std::string raw_status;
    DevicePairingStatus status = DevicePairingStatus::kUnknown;
};

bool ParseDevicePairingResponse(const std::string& json,
                                DevicePairingResponseType type,
                                DevicePairingResponse& response,
                                std::string& error);

}  // namespace rodakos
