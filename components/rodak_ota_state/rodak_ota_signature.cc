#include "rodak_ota_signature.h"
#include "rodak_ota_trust.h"
#include "rodak_sha256.h"
#include <psa/crypto.h>
#include <cstdio>

#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/version.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <string>

namespace rodakos {
namespace {

int HexValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return value - 'A' + 10;
}

bool DecodeHex(const std::string& value, std::array<unsigned char, 256>& output,
               size_t& output_size) {
    if (!IsValidOtaSignatureHex(value) || value.size() / 2 > output.size()) {
        return false;
    }
    output_size = value.size() / 2;
    for (size_t index = 0; index < output_size; ++index) {
        output[index] = static_cast<unsigned char>(
            (HexValue(value[index * 2]) << 4) | HexValue(value[index * 2 + 1]));
    }
    return true;
}

}  // namespace

bool VerifyOtaSignature(const OtaUpdateRecord& record, const std::string& signature_hex) {
    const std::string payload = BuildOtaSignaturePayload(record);
    std::array<unsigned char, 256> signature = {};
    size_t signature_size = 0;
    if (kOtaPublicKeyPem[0] == '\0' || psa_crypto_init() != PSA_SUCCESS ||
        payload.empty() || !DecodeHex(signature_hex, signature, signature_size)) {
        return false;
    }

    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    const int parse_result = mbedtls_pk_parse_public_key(
        &key, reinterpret_cast<const unsigned char*>(kOtaPublicKeyPem), sizeof(kOtaPublicKeyPem));
    if (parse_result != 0 || mbedtls_pk_get_bitlen(&key) != 2048) {
        mbedtls_pk_free(&key);
        return false;
    }
#if MBEDTLS_VERSION_MAJOR >= 4
    const bool rsa = mbedtls_pk_can_do_psa(&key,
        PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256), PSA_KEY_USAGE_VERIFY_HASH);
#else
    const bool rsa = mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA);
#endif
    if (!rsa) {
        mbedtls_pk_free(&key);
        return false;
    }

    std::array<unsigned char, 32> digest = {};
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    const int digest_result = md_info == nullptr
                                  ? -1
                                  : mbedtls_md(md_info,
                                               reinterpret_cast<const unsigned char*>(payload.data()),
                                               payload.size(), digest.data());
    const int verify_result = digest_result == 0
                                  ? mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest.data(),
                                                      digest.size(), signature.data(), signature_size)
                                  : -1;
    mbedtls_pk_free(&key);
    return verify_result == 0;
}

const char* OtaTrustMarker() {
    return kOtaTrustMarker;
}

bool VerifyOtaImageFile(const std::string& path, uint64_t size, const std::string& expected) {
    if (size == 0 || size > 0xd50000 || expected.size() != 64) {
        return false;
    }
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    Sha256 sha;
    bool ok = sha.Start();
    std::array<unsigned char, 1024> buffer = {};
    uint64_t total = 0;
    while (ok && total <= size) {
        const size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
        total += read;
        ok = total <= size && sha.Update(buffer.data(), read);
        if (read < buffer.size()) {
            ok = ok && std::feof(file) && !std::ferror(file);
            break;
        }
    }
    std::fclose(file);
    std::array<unsigned char, 32> digest = {};
    return ok && total == size && sha.Finish(digest) && Sha256ToHex(digest) == expected;
}

bool ReadOtaSignatureFile(const std::string& path, std::string& signature_hex) {
    signature_hex.clear();
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    std::array<char, 513> bytes = {};
    const size_t size = std::fread(bytes.data(), 1, bytes.size(), file);
    const bool ok = size == 512 && std::feof(file) && !std::ferror(file);
    std::fclose(file);
    if (!ok) {
        return false;
    }
    signature_hex.assign(bytes.data(), size);
    return IsValidOtaSignatureHex(signature_hex);
}

}  // namespace rodakos
