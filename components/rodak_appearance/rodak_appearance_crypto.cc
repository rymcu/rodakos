#include "rodak_appearance_crypto.h"
#include "rodak_appearance.h"
#include "rodak_sha256.h"

#include "rodak_appearance_json.h"
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/version.h>
#include <psa/crypto.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <vector>

namespace rodakos {
namespace {
cJSON* StrictJson(const char* text, size_t length) {
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(text, length, &end, false);
    while (end != nullptr && end < text + length && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) ++end;
    if (root == nullptr || end != text + length) { cJSON_Delete(root); return nullptr; }
    return root;
}
std::string Text(const cJSON* object, const char* name) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(value) && value->valuestring != nullptr ? value->valuestring : "";
}
bool Keys(const cJSON* object) {
    if (!cJSON_IsObject(object)) return false;
    std::set<std::string> seen;
    for (const cJSON* item = object->child; item != nullptr; item = item->next) {
        if (item->string == nullptr || !seen.insert(item->string).second) return false;
    }
    return true;
}
bool Integer(const cJSON* object, const char* name, double min, double max, uint64_t& out) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
        value->valuedouble < min || value->valuedouble > max || std::floor(value->valuedouble) != value->valuedouble) return false;
    out = static_cast<uint64_t>(value->valuedouble); return true;
}
bool Rsa2048(mbedtls_pk_context& key, const std::string& pem) {
    if (pem.empty() || pem.size() > 1024 || psa_crypto_init() != PSA_SUCCESS ||
        mbedtls_pk_parse_public_key(&key, reinterpret_cast<const unsigned char*>(pem.c_str()), pem.size() + 1) != 0 ||
        mbedtls_pk_get_bitlen(&key) != 2048) return false;
#if MBEDTLS_VERSION_MAJOR >= 4
    return mbedtls_pk_can_do_psa(&key, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256), PSA_KEY_USAGE_VERIFY_HASH);
#else
    return mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA);
#endif
}
bool Base64(const std::string& text, size_t maximum, std::vector<uint8_t>& out) {
    if (text.empty() || text.size() > ((maximum + 2) / 3) * 4 || text.size() % 4 != 0 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char ch) {
            return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '+' || ch == '/' || ch == '=';
        })) return false;
    out.resize(maximum); size_t length = 0;
    if (mbedtls_base64_decode(out.data(), out.size(), &length,
        reinterpret_cast<const uint8_t*>(text.data()), text.size()) != 0 || length > maximum) return false;
    out.resize(length); return true;
}
}  // namespace
bool IsAppearanceSha256(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}
bool AppearancePublicKeyId(const std::string& pem, std::string& key_id) {
    key_id.clear(); mbedtls_pk_context key; mbedtls_pk_init(&key);
    std::array<uint8_t, 512> der = {};
    const int length = Rsa2048(key, pem) ? mbedtls_pk_write_pubkey_der(&key, der.data(), der.size()) : -1;
    mbedtls_pk_free(&key);
    Sha256 hash; std::array<unsigned char, 32> digest = {};
    if (length <= 0 || !hash.Start() || !hash.Update(der.data() + der.size() - length, static_cast<size_t>(length)) || !hash.Finish(digest)) return false;
    key_id = Sha256ToHex(digest); return true;
}
std::string AppearanceKeyFingerprint(const std::string& key_id) {
    if (!IsAppearanceSha256(key_id)) return {};
    std::string result;
    for (size_t i = 0; i < 32; i += 4) { if (i != 0) result.push_back(' '); result += key_id.substr(i, 4); }
    return result;
}
bool VerifyAppearanceManifest(const std::string& encoded, const std::string& pem,
                              const std::string& expected_key_id, const std::string& expected_device_key,
                              AppearanceSignedRelease& release, std::string& error) {
    release = {}; error = "manifest_invalid";
    cJSON* manifest = StrictJson(encoded.data(), encoded.size());
    std::vector<uint8_t> payload, signature;
    const bool envelope = Keys(manifest) && Text(manifest, "keyId") == expected_key_id &&
        Base64(Text(manifest, "signedPayload"), 4096, payload) && Base64(Text(manifest, "signature"), 256, signature) && signature.size() == 256;
    cJSON_Delete(manifest);
    if (!envelope || payload.empty() || std::find(payload.begin(), payload.end(), 0) != payload.end()) return false;
    std::string actual_key;
    if (!AppearancePublicKeyId(pem, actual_key) || actual_key != expected_key_id) { error = "publisher_key_invalid"; return false; }
    mbedtls_pk_context key; mbedtls_pk_init(&key);
    std::array<unsigned char, 32> digest = {};
    Sha256 hash;
    const bool verified = Rsa2048(key, pem) && hash.Start() && hash.Update(payload.data(), payload.size()) && hash.Finish(digest) &&
        mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest.data(), digest.size(), signature.data(), signature.size()) == 0;
    mbedtls_pk_free(&key);
    if (!verified) { error = "signature_invalid"; return false; }
    cJSON* root = StrictJson(reinterpret_cast<const char*>(payload.data()), payload.size());
    uint64_t revision = 0, size = 0, width = 0, height = 0;
    release.deployment_id = Text(root, "deploymentId"); release.release_id = Text(root, "releaseId");
    release.device_id = Text(root, "deviceId"); release.device_key = Text(root, "deviceKey"); release.product_key = Text(root, "productKey");
    release.key_id = Text(root, "keyId"); release.mode = Text(root, "mode"); release.package_sha256 = Text(root, "packageSha256");
    const bool valid = Keys(root) && Text(root, "schema") == "rodak-appearance-deployment-v1" &&
        IsAppearanceIdentifier(release.deployment_id) && IsAppearanceIdentifier(release.device_id) &&
        release.device_key == expected_device_key && release.product_key == "rymcu-bigsmart" && release.key_id == expected_key_id &&
        Integer(root, "revision", 1, 0xffffffff, revision) && Integer(root, "packageSize", 0, kAppearanceMaxPackageBytes, size) &&
        Integer(root, "width", 320, 320, width) && Integer(root, "height", 240, 240, height) &&
        ((release.mode == "custom" && IsAppearanceIdentifier(release.release_id) && size >= 16 && IsAppearanceSha256(release.package_sha256)) ||
         (release.mode == "builtin" && size == 0 && release.release_id.empty() && release.package_sha256.empty()));
    cJSON_Delete(root);
    if (!valid) { error = "signed_payload_invalid"; return false; }
    release.revision = static_cast<uint32_t>(revision); release.package_size = static_cast<size_t>(size); error.clear(); return true;
}
}  // namespace rodakos
