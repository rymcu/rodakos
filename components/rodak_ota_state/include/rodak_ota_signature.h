#pragma once

#include "rodak_ota_state.h"

#include <cstdint>
#include <string>

namespace rodakos {

inline constexpr const char* kOtaSignatureAlgorithm = "rsa2048-sha256";
inline constexpr uint32_t kOtaManifestVersion = 2;

std::string BuildOtaSignaturePayload(const OtaUpdateRecord& record);
bool IsValidOtaSignatureHex(const std::string& value);
bool VerifyOtaSignature(const OtaUpdateRecord& record, const std::string& signature_hex);
const char* OtaTrustMarker();
bool ReadOtaSignatureFile(const std::string& path, std::string& signature_hex);
bool VerifyOtaImageFile(const std::string& path, uint64_t size, const std::string& sha256);

}  // namespace rodakos
