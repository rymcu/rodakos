#include "phone_os/trust_setup_protocol.h"

#include <cJSON.h>
#include <esp_random.h>
#include <esp_srp.h>
#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include <mbedtls/private/bignum.h>
#include <psa/crypto.h>

#include <cstring>

namespace rodakos {
namespace {

constexpr const char* kLabel = "rodak-trust-setup/v1";
constexpr size_t kGroupBytes = 384;
constexpr size_t kHashBytes = 64;
constexpr size_t kNonceBytes = 12;
constexpr size_t kTagBytes = 16;
// 只有 /trust 的明文会超过这个长度：server_trust 证书最多 1536 字节加字段与转义
constexpr size_t kMaxSealedPlaintext = 4096;

// RFC 5054 3072 位群，与 esp_srp 内置的 N_3072 相同；verifier 在这里计算以保留完整 salt。
constexpr uint8_t kGroupN[kGroupBytes] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34,
    0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1, 0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
    0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37,
    0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45, 0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6,
    0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B, 0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
    0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5, 0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6,
    0x49, 0x28, 0x66, 0x51, 0xEC, 0xE4, 0x5B, 0x3D, 0xC2, 0x00, 0x7C, 0xB8, 0xA1, 0x63, 0xBF, 0x05,
    0x98, 0xDA, 0x48, 0x36, 0x1C, 0x55, 0xD3, 0x9A, 0x69, 0x16, 0x3F, 0xA8, 0xFD, 0x24, 0xCF, 0x5F,
    0x83, 0x65, 0x5D, 0x23, 0xDC, 0xA3, 0xAD, 0x96, 0x1C, 0x62, 0xF3, 0x56, 0x20, 0x85, 0x52, 0xBB,
    0x9E, 0xD5, 0x29, 0x07, 0x70, 0x96, 0x96, 0x6D, 0x67, 0x0C, 0x35, 0x4E, 0x4A, 0xBC, 0x98, 0x04,
    0xF1, 0x74, 0x6C, 0x08, 0xCA, 0x18, 0x21, 0x7C, 0x32, 0x90, 0x5E, 0x46, 0x2E, 0x36, 0xCE, 0x3B,
    0xE3, 0x9E, 0x77, 0x2C, 0x18, 0x0E, 0x86, 0x03, 0x9B, 0x27, 0x83, 0xA2, 0xEC, 0x07, 0xA2, 0x8F,
    0xB5, 0xC5, 0x5D, 0xF0, 0x6F, 0x4C, 0x52, 0xC9, 0xDE, 0x2B, 0xCB, 0xF6, 0x95, 0x58, 0x17, 0x18,
    0x39, 0x95, 0x49, 0x7C, 0xEA, 0x95, 0x6A, 0xE5, 0x15, 0xD2, 0x26, 0x18, 0x98, 0xFA, 0x05, 0x10,
    0x15, 0x72, 0x8E, 0x5A, 0x8A, 0xAA, 0xC4, 0x2D, 0xAD, 0x33, 0x17, 0x0D, 0x04, 0x50, 0x7A, 0x33,
    0xA8, 0x55, 0x21, 0xAB, 0xDF, 0x1C, 0xBA, 0x64, 0xEC, 0xFB, 0x85, 0x04, 0x58, 0xDB, 0xEF, 0x0A,
    0x8A, 0xEA, 0x71, 0x57, 0x5D, 0x06, 0x0C, 0x7D, 0xB3, 0x97, 0x0F, 0x85, 0xA6, 0xE1, 0xE4, 0xC7,
    0xAB, 0xF5, 0xAE, 0x8C, 0xDB, 0x09, 0x33, 0xD7, 0x1E, 0x8C, 0x94, 0xE0, 0x4A, 0x25, 0x61, 0x9D,
    0xCE, 0xE3, 0xD2, 0x26, 0x1A, 0xD2, 0xEE, 0x6B, 0xF1, 0x2F, 0xFA, 0x06, 0xD9, 0x8A, 0x08, 0x64,
    0xD8, 0x76, 0x02, 0x73, 0x3E, 0xC8, 0x6A, 0x64, 0x52, 0x1F, 0x2B, 0x18, 0x17, 0x7B, 0x20, 0x0C,
    0xBB, 0xE1, 0x17, 0x57, 0x7A, 0x61, 0x5D, 0x6C, 0x77, 0x09, 0x88, 0xC0, 0xBA, 0xD9, 0x46, 0xE2,
    0x08, 0xE2, 0x4F, 0xA0, 0x74, 0xE5, 0xAB, 0x31, 0x43, 0xDB, 0x5B, 0xFC, 0xE0, 0xFD, 0x10, 0x8E,
    0x4B, 0x82, 0xD1, 0x20, 0xA9, 0x3A, 0xD2, 0xCA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

class Mpi {
public:
    Mpi() { mbedtls_mpi_init(&value); }
    ~Mpi() { mbedtls_mpi_free(&value); }
    Mpi(const Mpi&) = delete;
    Mpi& operator=(const Mpi&) = delete;
    mbedtls_mpi value;
};

bool Sha512(std::initializer_list<std::pair<const uint8_t*, size_t>> parts, uint8_t* out) {
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    if (psa_hash_setup(&operation, PSA_ALG_SHA_512) != PSA_SUCCESS) return false;
    for (const auto& part : parts) {
        if (psa_hash_update(&operation, part.first, part.second) != PSA_SUCCESS) {
            psa_hash_abort(&operation);
            return false;
        }
    }
    size_t length = 0;
    const bool ok = psa_hash_finish(&operation, out, kHashBytes, &length) == PSA_SUCCESS &&
        length == kHashBytes;
    if (!ok) psa_hash_abort(&operation);
    return ok;
}

// x = H(salt | H(I ":" P))，salt 按原样（含前导零）参与，与 esp_srp 的 calculate_x 一致。
bool ComputeVerifier(const std::string& code, const TrustSetupBytes& salt, TrustSetupBytes& out) {
    const std::string identity = std::string(kLabel) + ":" + code;
    uint8_t inner[kHashBytes];
    uint8_t x_bytes[kHashBytes];
    if (!Sha512({{reinterpret_cast<const uint8_t*>(identity.data()), identity.size()}}, inner) ||
        !Sha512({{salt.data(), salt.size()}, {inner, sizeof(inner)}}, x_bytes)) {
        return false;
    }
    Mpi n, g, x, v;
    bool ok = mbedtls_mpi_read_binary(&n.value, kGroupN, sizeof(kGroupN)) == 0 &&
        mbedtls_mpi_lset(&g.value, 5) == 0 &&
        mbedtls_mpi_read_binary(&x.value, x_bytes, sizeof(x_bytes)) == 0 &&
        mbedtls_mpi_exp_mod(&v.value, &g.value, &x.value, &n.value, nullptr) == 0;
    if (!ok) return false;
    out.assign(mbedtls_mpi_size(&v.value), 0);
    ok = mbedtls_mpi_write_binary(&v.value, out.data(), out.size()) == 0;
    mbedtls_platform_zeroize(x_bytes, sizeof(x_bytes));
    mbedtls_platform_zeroize(inner, sizeof(inner));
    return ok;
}

bool Hkdf(const uint8_t* ikm, size_t ikm_len, const char* purpose, std::array<uint8_t, 32>& out) {
    const std::string info = std::string(kLabel) + " " + purpose;
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    const bool ok =
        psa_key_derivation_setup(&operation, PSA_ALG_HKDF(PSA_ALG_SHA_256)) == PSA_SUCCESS &&
        psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT, nullptr, 0) ==
            PSA_SUCCESS &&
        psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET, ikm,
                                       ikm_len) == PSA_SUCCESS &&
        psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_INFO,
                                       reinterpret_cast<const uint8_t*>(info.data()),
                                       info.size()) == PSA_SUCCESS &&
        psa_key_derivation_output_bytes(&operation, out.data(), out.size()) == PSA_SUCCESS;
    psa_key_derivation_abort(&operation);
    return ok;
}

psa_key_id_t ImportKey(const std::array<uint8_t, 32>& key, psa_algorithm_t algorithm,
                       psa_key_type_t type, psa_key_usage_t usage) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, type);
    psa_set_key_algorithm(&attributes, algorithm);
    psa_set_key_usage_flags(&attributes, usage);
    psa_key_id_t id = 0;
    return psa_import_key(&attributes, key.data(), key.size(), &id) == PSA_SUCCESS ? id : 0;
}

void AppendLengthPrefixed(TrustSetupBytes& out, const uint8_t* data, size_t length) {
    out.push_back(static_cast<uint8_t>(length >> 8));
    out.push_back(static_cast<uint8_t>(length & 0xff));
    out.insert(out.end(), data, data + length);
}

void AppendLengthPrefixed(TrustSetupBytes& out, const std::string& text) {
    AppendLengthPrefixed(out, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

bool ConstantTimeEqual(const uint8_t* left, const uint8_t* right, size_t length) {
    uint8_t difference = 0;
    for (size_t index = 0; index < length; ++index) difference |= left[index] ^ right[index];
    return difference == 0;
}

std::string HexEncode(const TrustSetupBytes& bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 0x0f]);
    }
    return out;
}

std::string JsonString(cJSON* object) {
    char* printed = cJSON_PrintUnformatted(object);
    std::string out = printed != nullptr ? printed : "";
    cJSON_free(printed);
    return out;
}

}  // namespace

struct TrustSetupSession::SrpState {
    esp_srp_handle_t* handle = nullptr;
    ~SrpState() { esp_srp_free(handle); }
};

const char* TrustSetupAbortReason(TrustSetupAnswerResult result) {
    switch (result) {
        case TrustSetupAnswerResult::kInvalidPublicKey: return "invalid_public_key";
        case TrustSetupAnswerResult::kProofMismatch: return "proof_mismatch";
        case TrustSetupAnswerResult::kBindingMismatch: return "binding_mismatch";
        default: return "internal_error";
    }
}

std::string GenerateTrustSetupCode() {
    std::string code;
    while (code.size() < kTrustSetupCodeDigits) {
        uint8_t bytes[16];
        esp_fill_random(bytes, sizeof(bytes));
        for (const auto byte : bytes) {
            // 250 = 25 * 10：丢弃尾部避免取模偏差
            if (byte >= 250) continue;
            code.push_back(static_cast<char>('0' + byte % 10));
            if (code.size() == kTrustSetupCodeDigits) break;
        }
    }
    return code;
}

bool DeriveTrustSetupKeys(const uint8_t* session_key, size_t session_key_len,
                          TrustSetupKeys& keys) {
    if (session_key == nullptr || session_key_len != kHashBytes) return false;
    return psa_crypto_init() == PSA_SUCCESS &&
        Hkdf(session_key, session_key_len, "bind", keys.bind) &&
        Hkdf(session_key, session_key_len, "device", keys.device) &&
        Hkdf(session_key, session_key_len, "server", keys.server);
}

bool ComputeTrustSetupBinding(const TrustSetupKeys& keys, const std::string& request_id,
                              const std::string& device_key, const TrustSetupBytes& seen_spki,
                              const TrustSetupBytes& server_spki, const char* role,
                              std::array<uint8_t, 32>& mac) {
    if (seen_spki.size() != kTrustSetupSpkiBytes || server_spki.size() != kTrustSetupSpkiBytes) {
        return false;
    }
    TrustSetupBytes message;
    message.reserve(160 + request_id.size() + device_key.size());
    AppendLengthPrefixed(message, kLabel);
    AppendLengthPrefixed(message, request_id);
    AppendLengthPrefixed(message, device_key);
    AppendLengthPrefixed(message, seen_spki.data(), seen_spki.size());
    AppendLengthPrefixed(message, server_spki.data(), server_spki.size());
    AppendLengthPrefixed(message, role);
    const auto key = ImportKey(keys.bind, PSA_ALG_HMAC(PSA_ALG_SHA_256), PSA_KEY_TYPE_HMAC,
                               PSA_KEY_USAGE_SIGN_MESSAGE);
    if (key == 0) return false;
    size_t length = 0;
    const bool ok = psa_mac_compute(key, PSA_ALG_HMAC(PSA_ALG_SHA_256), message.data(),
                                    message.size(), mac.data(), mac.size(), &length) ==
            PSA_SUCCESS && length == mac.size();
    psa_destroy_key(key);
    return ok;
}

bool SealTrustSetupPayload(const std::array<uint8_t, 32>& key, const std::string& request_id,
                           const std::string& plaintext, TrustSetupSealed& sealed,
                           const TrustSetupBytes& nonce_override) {
    if (plaintext.size() > kMaxSealedPlaintext) return false;
    TrustSetupBytes nonce = nonce_override;
    if (nonce.empty()) {
        nonce.resize(kNonceBytes);
        esp_fill_random(nonce.data(), nonce.size());
    }
    if (nonce.size() != kNonceBytes) return false;
    const auto id = ImportKey(key, PSA_ALG_GCM, PSA_KEY_TYPE_AES, PSA_KEY_USAGE_ENCRYPT);
    if (id == 0) return false;
    TrustSetupBytes ciphertext(plaintext.size() + kTagBytes);
    size_t length = 0;
    const bool ok = psa_aead_encrypt(
        id, PSA_ALG_GCM, nonce.data(), nonce.size(),
        reinterpret_cast<const uint8_t*>(request_id.data()), request_id.size(),
        reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size(), ciphertext.data(),
        ciphertext.size(), &length) == PSA_SUCCESS && length == ciphertext.size();
    psa_destroy_key(id);
    if (!ok) return false;
    sealed.nonce_base64 = EncodeTrustSetupBase64(nonce.data(), nonce.size());
    sealed.ciphertext_base64 = EncodeTrustSetupBase64(ciphertext.data(), ciphertext.size());
    return !sealed.nonce_base64.empty() && !sealed.ciphertext_base64.empty();
}

bool OpenTrustSetupPayload(const std::array<uint8_t, 32>& key, const std::string& request_id,
                           const TrustSetupSealed& sealed, std::string& plaintext) {
    TrustSetupBytes nonce;
    TrustSetupBytes ciphertext;
    if (!DecodeTrustSetupBase64(sealed.nonce_base64, kNonceBytes, kNonceBytes, nonce) ||
        !DecodeTrustSetupBase64(sealed.ciphertext_base64, kTagBytes + 2,
                                kMaxSealedPlaintext + kTagBytes, ciphertext)) {
        return false;
    }
    const auto id = ImportKey(key, PSA_ALG_GCM, PSA_KEY_TYPE_AES, PSA_KEY_USAGE_DECRYPT);
    if (id == 0) return false;
    std::string out(ciphertext.size() - kTagBytes, '\0');
    size_t length = 0;
    const bool ok = psa_aead_decrypt(
        id, PSA_ALG_GCM, nonce.data(), nonce.size(),
        reinterpret_cast<const uint8_t*>(request_id.data()), request_id.size(),
        ciphertext.data(), ciphertext.size(), reinterpret_cast<uint8_t*>(out.data()), out.size(),
        &length) == PSA_SUCCESS;
    psa_destroy_key(id);
    if (!ok) return false;
    out.resize(length);
    plaintext = std::move(out);
    return true;
}

bool DecodeTrustSetupBase64(const std::string& text, size_t min_bytes, size_t max_bytes,
                            TrustSetupBytes& out) {
    if (text.empty() || text.size() % 4 != 0 || text.size() > (max_bytes + 2) / 3 * 4) {
        return false;
    }
    TrustSetupBytes decoded(text.size() / 4 * 3);
    size_t length = 0;
    if (mbedtls_base64_decode(decoded.data(), decoded.size(), &length,
                              reinterpret_cast<const uint8_t*>(text.data()), text.size()) != 0 ||
        length < min_bytes || length > max_bytes) {
        return false;
    }
    decoded.resize(length);
    // 拒绝非规范编码：重新编码后必须完全相同
    if (EncodeTrustSetupBase64(decoded.data(), decoded.size()) != text) return false;
    out = std::move(decoded);
    return true;
}

std::string EncodeTrustSetupBase64(const uint8_t* data, size_t length) {
    std::string out((length + 2) / 3 * 4 + 1, '\0');
    size_t written = 0;
    if (mbedtls_base64_encode(reinterpret_cast<uint8_t*>(out.data()), out.size(), &written, data,
                              length) != 0) {
        return {};
    }
    out.resize(written);
    return out;
}

bool ValidTrustSetupClientPublicKey(const TrustSetupBytes& A) {
    if (A.size() != kGroupBytes) return false;
    Mpi n, a, remainder;
    return mbedtls_mpi_read_binary(&n.value, kGroupN, sizeof(kGroupN)) == 0 &&
        mbedtls_mpi_read_binary(&a.value, A.data(), A.size()) == 0 &&
        mbedtls_mpi_mod_mpi(&remainder.value, &a.value, &n.value) == 0 &&
        mbedtls_mpi_cmp_int(&remainder.value, 0) != 0;
}

std::unique_ptr<TrustSetupSession> TrustSetupSession::Create(const TrustSetupBytes& seen_spki,
                                                             std::string code,
                                                             TrustSetupBytes salt) {
    if (seen_spki.size() != kTrustSetupSpkiBytes || psa_crypto_init() != PSA_SUCCESS) {
        return nullptr;
    }
    std::unique_ptr<TrustSetupSession> session(new (std::nothrow) TrustSetupSession);
    auto srp = std::unique_ptr<SrpState>(new (std::nothrow) SrpState);
    if (session == nullptr || srp == nullptr) return nullptr;
    session->code_ = code.empty() ? GenerateTrustSetupCode() : std::move(code);
    if (salt.empty()) {
        salt.resize(kTrustSetupSaltBytes);
        esp_fill_random(salt.data(), salt.size());
    }
    if (salt.size() != kTrustSetupSaltBytes || session->code_.size() != kTrustSetupCodeDigits) {
        return nullptr;
    }
    session->salt_ = std::move(salt);
    session->seen_spki_ = seen_spki;
    TrustSetupBytes verifier;
    if (!ComputeVerifier(session->code_, session->salt_, verifier)) return nullptr;

    // 不用 esp_srp_srv_pubkey：它自行生成的 salt 首字节为 0 时会被截短为 15 字节
    srp->handle = esp_srp_init(ESP_NG_3072);
    char* public_key = nullptr;
    int public_key_length = 0;
    if (srp->handle == nullptr ||
        esp_srp_set_salt_verifier(srp->handle,
                                  reinterpret_cast<const char*>(session->salt_.data()),
                                  static_cast<int>(session->salt_.size()),
                                  reinterpret_cast<const char*>(verifier.data()),
                                  static_cast<int>(verifier.size())) != ESP_OK ||
        esp_srp_srv_pubkey_from_salt_verifier(srp->handle, &public_key, &public_key_length) !=
            ESP_OK ||
        public_key == nullptr || public_key_length <= 0) {
        return nullptr;
    }
    session->B_.assign(public_key, public_key + public_key_length);
    session->srp_ = std::move(srp);
    return session;
}

TrustSetupSession::~TrustSetupSession() {
    mbedtls_platform_zeroize(code_.data(), code_.size());
    mbedtls_platform_zeroize(&keys_, sizeof(keys_));
}

std::string TrustSetupSession::RequestObjectJson() const {
    cJSON* object = cJSON_CreateObject();
    if (object == nullptr) return {};
    cJSON_AddNumberToObject(object, "version", kTrustSetupVersion);
    cJSON_AddStringToObject(object, "srpSalt",
                            EncodeTrustSetupBase64(salt_.data(), salt_.size()).c_str());
    cJSON_AddStringToObject(object, "srpB", EncodeTrustSetupBase64(B_.data(), B_.size()).c_str());
    cJSON_AddStringToObject(object, "seenSpki", HexEncode(seen_spki_).c_str());
    std::string out = JsonString(object);
    cJSON_Delete(object);
    return out;
}

TrustSetupAnswerResult TrustSetupSession::Answer(const std::string& request_id,
                                                 const std::string& device_key,
                                                 const TrustSetupChallenge& challenge,
                                                 const std::string& device_secret,
                                                 std::string& trust_body_json) {
    trust_body_json.clear();
    if (srp_ == nullptr || srp_->handle == nullptr) return TrustSetupAnswerResult::kInternalError;
    if (!ValidTrustSetupClientPublicKey(challenge.srp_A)) {
        return TrustSetupAnswerResult::kInvalidPublicKey;
    }
    if (challenge.srp_M1.size() != kHashBytes || challenge.server_bind.size() != 32) {
        return TrustSetupAnswerResult::kProofMismatch;
    }
    TrustSetupBytes A = challenge.srp_A;
    char* session_key = nullptr;
    uint16_t session_key_length = 0;
    if (esp_srp_get_session_key(srp_->handle, reinterpret_cast<char*>(A.data()),
                                static_cast<int>(A.size()), &session_key,
                                &session_key_length) != ESP_OK ||
        session_key == nullptr) {
        return TrustSetupAnswerResult::kInternalError;
    }
    TrustSetupBytes M1 = challenge.srp_M1;
    uint8_t M2[kHashBytes] = {};
    std::string username = kLabel;
    if (esp_srp_exchange_proofs(srp_->handle, username.data(),
                                static_cast<uint16_t>(username.size()),
                                reinterpret_cast<char*>(M1.data()),
                                reinterpret_cast<char*>(M2)) != ESP_OK) {
        return TrustSetupAnswerResult::kProofMismatch;
    }
    TrustSetupKeys keys;
    if (!DeriveTrustSetupKeys(reinterpret_cast<const uint8_t*>(session_key), session_key_length,
                              keys)) {
        return TrustSetupAnswerResult::kInternalError;
    }
    // 设备两处都填自己看到的 SPKI；Rodak 填的是它真实证书的 SPKI，不一致说明有中间人
    std::array<uint8_t, 32> expected_server{};
    std::array<uint8_t, 32> device_bind{};
    if (!ComputeTrustSetupBinding(keys, request_id, device_key, seen_spki_, seen_spki_, "server",
                                  expected_server) ||
        !ComputeTrustSetupBinding(keys, request_id, device_key, seen_spki_, seen_spki_, "device",
                                  device_bind)) {
        return TrustSetupAnswerResult::kInternalError;
    }
    if (!ConstantTimeEqual(expected_server.data(), challenge.server_bind.data(), 32)) {
        return TrustSetupAnswerResult::kBindingMismatch;
    }

    cJSON* credentials_plain = cJSON_CreateObject();
    if (credentials_plain == nullptr) return TrustSetupAnswerResult::kInternalError;
    cJSON_AddStringToObject(credentials_plain, "deviceSecret", device_secret.c_str());
    std::string plaintext = JsonString(credentials_plain);
    cJSON_Delete(credentials_plain);
    TrustSetupSealed credentials;
    const bool sealed = !plaintext.empty() &&
        SealTrustSetupPayload(keys.device, request_id, plaintext, credentials);
    mbedtls_platform_zeroize(plaintext.data(), plaintext.size());
    if (!sealed) return TrustSetupAnswerResult::kInternalError;

    cJSON* body = cJSON_CreateObject();
    cJSON* sealed_object = cJSON_CreateObject();
    if (body == nullptr || sealed_object == nullptr) {
        cJSON_Delete(body);
        cJSON_Delete(sealed_object);
        return TrustSetupAnswerResult::kInternalError;
    }
    cJSON_AddStringToObject(body, "srpM2", EncodeTrustSetupBase64(M2, sizeof(M2)).c_str());
    cJSON_AddStringToObject(body, "deviceBind",
                            EncodeTrustSetupBase64(device_bind.data(), device_bind.size()).c_str());
    cJSON_AddStringToObject(sealed_object, "nonce", credentials.nonce_base64.c_str());
    cJSON_AddStringToObject(sealed_object, "ciphertext", credentials.ciphertext_base64.c_str());
    cJSON_AddItemToObject(body, "credentials", sealed_object);
    trust_body_json = JsonString(body);
    cJSON_Delete(body);
    if (trust_body_json.empty()) return TrustSetupAnswerResult::kInternalError;
    keys_ = keys;
    keys_ready_ = true;
    mbedtls_platform_zeroize(&keys, sizeof(keys));
    return TrustSetupAnswerResult::kAccepted;
}

bool TrustSetupSession::OpenResponse(const std::string& request_id,
                                     const TrustSetupSealed& sealed, ServerTrust& trust,
                                     std::string& request_token, std::string& error) const {
    if (!keys_ready_) {
        error = "trust_setup_not_answered";
        return false;
    }
    std::string plaintext;
    if (!OpenTrustSetupPayload(keys_.server, request_id, sealed, plaintext)) {
        error = "trust_setup_decrypt_failed";
        return false;
    }
    cJSON* root = cJSON_ParseWithLength(plaintext.c_str(), plaintext.size());
    mbedtls_platform_zeroize(plaintext.data(), plaintext.size());
    const cJSON* trust_object = cJSON_GetObjectItemCaseSensitive(root, "serverTrust");
    const cJSON* token = cJSON_GetObjectItemCaseSensitive(root, "requestToken");
    ServerTrust parsed;
    const bool ok = cJSON_IsObject(trust_object) && cJSON_IsString(token) &&
        token->valuestring[0] != '\0' && std::strlen(token->valuestring) <= 128 &&
        ParseServerTrust(trust_object, parsed, error);
    if (ok) request_token = token->valuestring;
    cJSON_Delete(root);
    if (!ok) {
        if (error.empty()) error = "trust_setup_response_invalid";
        return false;
    }
    // 下发的身份必须就是首次连接时实际握手的那张证书
    if (parsed.server_id != HexEncode(seen_spki_)) {
        error = "trust_setup_identity_mismatch";
        return false;
    }
    trust = std::move(parsed);
    return true;
}

}  // namespace rodakos
