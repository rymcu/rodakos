#pragma once

#include <string>

namespace rodakos {

bool IsValidSerialProvisioningBindingNonce(const std::string& nonce);

// Rodak persists the SHA-256 identity digest, so the shared HMAC key is that
// raw digest rather than its hex spelling or the device secret itself.
bool CreateSerialProvisioningBindingProof(const std::string& device_secret,
                                         const std::string& nonce,
                                         std::string& proof);

}  // namespace rodakos
