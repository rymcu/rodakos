#pragma once

#include <psa/crypto.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace rodakos {

class Sha256 {
public:
    Sha256() = default;
    ~Sha256() { Abort(); }

    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    bool Start() {
        Abort();
        if (psa_crypto_init() != PSA_SUCCESS) {
            return false;
        }
        operation_ = PSA_HASH_OPERATION_INIT;
        if (psa_hash_setup(&operation_, PSA_ALG_SHA_256) != PSA_SUCCESS) {
            Abort();
            return false;
        }
        active_ = true;
        return true;
    }

    bool Update(const void* data, size_t size) {
        if (!active_ || (data == nullptr && size != 0)) {
            return false;
        }
        if (psa_hash_update(&operation_, static_cast<const uint8_t*>(data), size) != PSA_SUCCESS) {
            Abort();
            return false;
        }
        return true;
    }

    bool Finish(std::array<unsigned char, 32>& digest) {
        if (!active_) {
            return false;
        }
        size_t digest_length = 0;
        const psa_status_t status = psa_hash_finish(
            &operation_, digest.data(), digest.size(), &digest_length);
        active_ = false;
        if (status != PSA_SUCCESS) {
            Abort();
            return false;
        }
        operation_ = PSA_HASH_OPERATION_INIT;
        return digest_length == digest.size();
    }

private:
    void Abort() {
        psa_hash_abort(&operation_);
        operation_ = PSA_HASH_OPERATION_INIT;
        active_ = false;
    }

    psa_hash_operation_t operation_ = PSA_HASH_OPERATION_INIT;
    bool active_ = false;
};

inline std::string Sha256ToHex(const std::array<unsigned char, 32>& digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result(64, '0');
    for (size_t i = 0; i < digest.size(); ++i) {
        result[i * 2] = kHex[digest[i] >> 4];
        result[i * 2 + 1] = kHex[digest[i] & 0x0f];
    }
    return result;
}

}  // namespace rodakos
