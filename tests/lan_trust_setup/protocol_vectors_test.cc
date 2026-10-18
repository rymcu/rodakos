#include "test_framework.h"

#include "srp_harness.h"

#include <psa/crypto.h>

#include "cJSON.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

// Rodak 用 Node crypto 生成协议层向量；这里用 PSA（固件同一套 API）独立重算，
// 两套实现一致才说明规则描述没有歧义。
using namespace lan_trust_setup;

namespace {

std::string ReadFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) throw std::runtime_error(std::string("vectors missing: ") + path);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

class Json {
public:
    explicit Json(const std::string& text) : root_(cJSON_Parse(text.c_str())) {
        if (root_ == nullptr) throw std::runtime_error("vector JSON parse failed");
    }
    ~Json() { cJSON_Delete(root_); }
    Json(const Json&) = delete;
    Json& operator=(const Json&) = delete;

    std::string Get(std::initializer_list<const char*> path) const {
        const cJSON* node = root_;
        for (const auto* key : path) {
            node = cJSON_GetObjectItemCaseSensitive(node, key);
            if (node == nullptr) throw std::runtime_error(std::string("field missing: ") + key);
        }
        if (!cJSON_IsString(node)) throw std::runtime_error("field is not a string");
        return node->valuestring;
    }

private:
    cJSON* root_;
};

const Json& Vectors() {
    static const Json vectors(ReadFile(LAN_TRUST_SETUP_PROTOCOL_VECTORS));
    return vectors;
}

void Require(psa_status_t status, const char* what) {
    if (status != PSA_SUCCESS) throw std::runtime_error(std::string("psa failure: ") + what);
}

Bytes Hkdf(const Bytes& ikm, const std::string& info) {
    Require(psa_crypto_init(), "init");
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    Bytes out(32);
    Require(psa_key_derivation_setup(&operation, PSA_ALG_HKDF(PSA_ALG_SHA_256)), "hkdf setup");
    Require(psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT, nullptr, 0),
            "hkdf salt");
    Require(psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET, ikm.data(),
                                           ikm.size()), "hkdf secret");
    Require(psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_INFO,
                                           reinterpret_cast<const uint8_t*>(info.data()),
                                           info.size()), "hkdf info");
    Require(psa_key_derivation_output_bytes(&operation, out.data(), out.size()), "hkdf output");
    psa_key_derivation_abort(&operation);
    return out;
}

psa_key_id_t ImportKey(const Bytes& key, psa_algorithm_t algorithm, psa_key_type_t type,
                       psa_key_usage_t usage) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, type);
    psa_set_key_algorithm(&attributes, algorithm);
    psa_set_key_usage_flags(&attributes, usage);
    psa_key_id_t id = 0;
    Require(psa_import_key(&attributes, key.data(), key.size(), &id), "import");
    return id;
}

void AppendLengthPrefixed(Bytes& out, const Bytes& value) {
    out.push_back(static_cast<uint8_t>(value.size() >> 8));
    out.push_back(static_cast<uint8_t>(value.size() & 0xff));
    out.insert(out.end(), value.begin(), value.end());
}

Bytes Binding(const Bytes& bind_key, const std::string& request_id, const std::string& device_key,
              const Bytes& seen, const Bytes& server, const std::string& role) {
    Bytes message;
    AppendLengthPrefixed(message, FromString("rodak-trust-setup/v1"));
    AppendLengthPrefixed(message, FromString(request_id));
    AppendLengthPrefixed(message, FromString(device_key));
    AppendLengthPrefixed(message, seen);
    AppendLengthPrefixed(message, server);
    AppendLengthPrefixed(message, FromString(role));
    const auto key = ImportKey(bind_key, PSA_ALG_HMAC(PSA_ALG_SHA_256), PSA_KEY_TYPE_HMAC,
                               PSA_KEY_USAGE_SIGN_MESSAGE);
    Bytes mac(32);
    size_t length = 0;
    const auto status = psa_mac_compute(key, PSA_ALG_HMAC(PSA_ALG_SHA_256), message.data(),
                                        message.size(), mac.data(), mac.size(), &length);
    psa_destroy_key(key);
    Require(status, "hmac");
    return mac;
}

Bytes Seal(const Bytes& key_bytes, const Bytes& nonce, const std::string& aad,
           const std::string& plaintext) {
    const auto key = ImportKey(key_bytes, PSA_ALG_GCM, PSA_KEY_TYPE_AES,
                               PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    Bytes out(plaintext.size() + 16);
    size_t length = 0;
    const auto status = psa_aead_encrypt(
        key, PSA_ALG_GCM, nonce.data(), nonce.size(), reinterpret_cast<const uint8_t*>(aad.data()),
        aad.size(), reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size(),
        out.data(), out.size(), &length);
    psa_destroy_key(key);
    Require(status, "gcm encrypt");
    out.resize(length);
    return out;
}

bool Open(const Bytes& key_bytes, const Bytes& nonce, const std::string& aad,
          const Bytes& ciphertext, std::string& plaintext) {
    const auto key = ImportKey(key_bytes, PSA_ALG_GCM, PSA_KEY_TYPE_AES,
                               PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    Bytes out(ciphertext.size());
    size_t length = 0;
    const auto status = psa_aead_decrypt(
        key, PSA_ALG_GCM, nonce.data(), nonce.size(), reinterpret_cast<const uint8_t*>(aad.data()),
        aad.size(), ciphertext.data(), ciphertext.size(), out.data(), out.size(), &length);
    psa_destroy_key(key);
    if (status != PSA_SUCCESS) return false;
    plaintext.assign(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(length));
    return true;
}

}  // namespace

RODAK_TEST("协议向量的 K 来自 esp_srp 向量 typical") {
    cJSON* srp = cJSON_Parse(ReadFile(LAN_TRUST_SETUP_VECTORS).c_str());
    RODAK_CHECK(srp != nullptr);
    const cJSON* first = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(srp, "vectors"), 0);
    const cJSON* key = cJSON_GetObjectItemCaseSensitive(first, "K");
    const std::string expected = cJSON_IsString(key) ? key->valuestring : "";
    cJSON_Delete(srp);
    RODAK_CHECK(!expected.empty());
    RODAK_CHECK(Vectors().Get({"K"}) == expected);
}

RODAK_TEST("PSA HKDF-SHA256 派生的三把密钥与 Rodak 一致") {
    const Bytes K = FromHex(Vectors().Get({"K"}));
    RODAK_CHECK(ToHex(Hkdf(K, "rodak-trust-setup/v1 bind")) == Vectors().Get({"keys", "bind"}));
    RODAK_CHECK(ToHex(Hkdf(K, "rodak-trust-setup/v1 device")) == Vectors().Get({"keys", "device"}));
    RODAK_CHECK(ToHex(Hkdf(K, "rodak-trust-setup/v1 server")) == Vectors().Get({"keys", "server"}));
}

RODAK_TEST("PSA HMAC 证书绑定与 Rodak 一致，看到攻击者证书时不一致") {
    const Bytes bind = FromHex(Vectors().Get({"keys", "bind"}));
    const auto request_id = Vectors().Get({"binding", "requestId"});
    const auto device_key = Vectors().Get({"binding", "deviceKey"});
    const Bytes server = FromHex(Vectors().Get({"binding", "serverSpki"}));
    const Bytes attacker = FromHex(Vectors().Get({"binding", "attackerSpki"}));
    RODAK_CHECK(ToHex(Binding(bind, request_id, device_key, server, server, "server")) ==
                Vectors().Get({"binding", "serverRole"}));
    RODAK_CHECK(ToHex(Binding(bind, request_id, device_key, server, server, "device")) ==
                Vectors().Get({"binding", "deviceRole"}));
    // 设备看到的是攻击者证书：Rodak 填自己的 SPKI，设备两处都填攻击者 SPKI，结果必然不同
    RODAK_CHECK(ToHex(Binding(bind, request_id, device_key, attacker, server, "server")) ==
                Vectors().Get({"binding", "serverRoleWhenDeviceSawAttacker"}));
    RODAK_CHECK(ToHex(Binding(bind, request_id, device_key, attacker, attacker, "server")) !=
                Vectors().Get({"binding", "serverRoleWhenDeviceSawAttacker"}));
}

RODAK_TEST("PSA AES-256-GCM 加解密与 Rodak 一致，AAD 不同则拒绝") {
    const Bytes nonce = FromHex(Vectors().Get({"aead", "nonce"}));
    const auto request_id = Vectors().Get({"aead", "requestId"});
    for (const auto* direction : {"device", "server"}) {
        const Bytes key = FromHex(Vectors().Get({"keys", direction}));
        const auto plaintext = Vectors().Get({"aead", direction, "plaintext"});
        const Bytes ciphertext = FromHex(Vectors().Get({"aead", direction, "ciphertext"}));
        RODAK_CHECK(Seal(key, nonce, request_id, plaintext) == ciphertext);
        std::string opened;
        RODAK_CHECK(Open(key, nonce, request_id, ciphertext, opened));
        RODAK_CHECK(opened == plaintext);
        RODAK_CHECK_FALSE(Open(key, nonce, request_id + "x", ciphertext, opened));
    }
}
