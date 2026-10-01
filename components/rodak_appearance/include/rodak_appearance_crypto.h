#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rodakos {
struct AppearanceSignedRelease {
    std::string deployment_id;
    std::string release_id;
    std::string device_id;
    std::string device_key;
    std::string product_key;
    std::string key_id;
    std::string mode;
    uint32_t revision = 0;
    size_t package_size = 0;
    std::string package_sha256;
};
bool AppearancePublicKeyId(const std::string& pem, std::string& key_id);
std::string AppearanceKeyFingerprint(const std::string& key_id);
bool VerifyAppearanceManifest(const std::string& encoded, const std::string& pem,
                              const std::string& expected_key_id,
                              const std::string& expected_device_key,
                              AppearanceSignedRelease& release, std::string& error);
bool IsAppearanceSha256(const std::string& value);
}  // namespace rodakos
