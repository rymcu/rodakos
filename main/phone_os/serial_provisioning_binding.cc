#include "phone_os/serial_provisioning_binding.h"

#include <mbedtls/platform_util.h>
#include <psa/crypto.h>

#include <algorithm>
#include <cstdint>

namespace rodakos {

bool IsValidSerialProvisioningBindingNonce(const std::string& nonce) {
    return nonce.size() == 64 && std::all_of(nonce.begin(), nonce.end(), [](char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
               (value >= 'A' && value <= 'F');
    });
}

bool CreateSerialProvisioningBindingProof(const std::string& device_secret,
                                         const std::string& nonce,
                                         std::string& proof) {
    proof.clear();
    if (device_secret.empty() || !IsValidSerialProvisioningBindingNonce(nonce)) {
        return false;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    uint8_t secret_hash[32] = {};
    uint8_t digest[32] = {};
    size_t hash_size = 0;
    size_t digest_size = 0;
    psa_status_t result = psa_hash_compute(
        PSA_ALG_SHA_256, reinterpret_cast<const uint8_t*>(device_secret.data()),
        device_secret.size(), secret_hash, sizeof(secret_hash), &hash_size);
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_key_id_t key = 0;
    if (result == PSA_SUCCESS && hash_size == sizeof(secret_hash)) {
        result = psa_import_key(&attributes, secret_hash, sizeof(secret_hash), &key);
    } else {
        result = PSA_ERROR_GENERIC_ERROR;
    }
    psa_reset_key_attributes(&attributes);
    mbedtls_platform_zeroize(secret_hash, sizeof(secret_hash));
    if (result == PSA_SUCCESS) {
        result = psa_mac_compute(key, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                                 reinterpret_cast<const uint8_t*>(nonce.data()), nonce.size(),
                                 digest, sizeof(digest), &digest_size);
        const psa_status_t destroy_result = psa_destroy_key(key);
        if (result == PSA_SUCCESS) result = destroy_result;
    }
    if (result != PSA_SUCCESS || digest_size != sizeof(digest)) {
        mbedtls_platform_zeroize(digest, sizeof(digest));
        return false;
    }
    constexpr char hex[] = "0123456789abcdef";
    proof.reserve(sizeof(digest) * 2);
    for (const uint8_t value : digest) {
        proof.push_back(hex[value >> 4]);
        proof.push_back(hex[value & 0x0f]);
    }
    mbedtls_platform_zeroize(digest, sizeof(digest));
    return true;
}

}  // namespace rodakos
