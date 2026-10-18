#include "test_framework.h"

#include "cJSON.h"
#include "esp_random.h"
#include "phone_os/trust_setup_protocol.h"
#include "srp_harness.h"

#include <fstream>
#include <sstream>
#include <string>

// 直接测试生产模块 main/phone_os/trust_setup_protocol.cc：它驱动真实 esp_srp，
// 与 Rodak 的 SRP 客户端（这里用主机参考客户端 RunClient 代替）和协议层向量互通。
namespace {

using lan_trust_setup::Bytes;
using lan_trust_setup::FromHex;
using lan_trust_setup::ToHex;
using rodakos::TrustSetupAnswerResult;
using rodakos::TrustSetupChallenge;
using rodakos::TrustSetupSession;

std::string ReadFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

struct JsonDoc {
    explicit JsonDoc(const std::string& text) : root(cJSON_Parse(text.c_str())) {}
    ~JsonDoc() { cJSON_Delete(root); }
    JsonDoc(const JsonDoc&) = delete;
    JsonDoc& operator=(const JsonDoc&) = delete;
    std::string Get(std::initializer_list<const char*> path) const {
        const cJSON* node = root;
        for (const auto* key : path) node = cJSON_GetObjectItemCaseSensitive(node, key);
        return cJSON_IsString(node) ? node->valuestring : std::string();
    }
    cJSON* root;
};

// tests/server_trust/test-ca.pem 的 SPKI SHA-256；ParseServerTrust 会核对证书公钥与它一致
const Bytes kRodakSpki =
    FromHex("2d71611acf59fb91c453de3fa25796cd9879047f92f9d4af43e2d365bec2d22b");
const Bytes kAttackerSpki =
    FromHex("0d5c9f7e2a1b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5");
const std::string kRequestId = "3f1a9c2e7b4d58a6c0e1f2a3b4c5d6e7";
const std::string kDeviceKey = "44:1b:f6:c3:b4:30";

// 模拟 Rodak：用参考 SRP 客户端得到 A、M1、K，按 Rodak 规则计算服务器绑定 HMAC。
struct RodakSide {
    TrustSetupChallenge challenge;
    rodakos::TrustSetupKeys keys;
    Bytes expected_M2;
};

RodakSide RodakAnswer(const std::string& request_json, const std::string& typed_code,
                      const Bytes& rodak_spki) {
    JsonDoc request(request_json);
    Bytes salt;
    Bytes B;
    RODAK_CHECK(rodakos::DecodeTrustSetupBase64(request.Get({"srpSalt"}), 16, 16, salt));
    RODAK_CHECK(rodakos::DecodeTrustSetupBase64(request.Get({"srpB"}), 1, 384, B));
    const Bytes seen = FromHex(request.Get({"seenSpki"}));
    const Bytes a = lan_trust_setup::Sha512(lan_trust_setup::FromString("rodak-a-" + typed_code));
    const auto client = lan_trust_setup::RunClient(lan_trust_setup::kUsername, typed_code, salt,
                                                   B, Bytes(a.begin(), a.begin() + 32));
    RodakSide side;
    RODAK_CHECK(rodakos::DeriveTrustSetupKeys(client.K.data(), client.K.size(), side.keys));
    std::array<uint8_t, 32> bind{};
    RODAK_CHECK(rodakos::ComputeTrustSetupBinding(side.keys, kRequestId, kDeviceKey, seen,
                                                  rodak_spki, "server", bind));
    side.challenge = {client.A, client.M1, Bytes(bind.begin(), bind.end())};
    side.expected_M2 = client.M2;
    return side;
}

std::string ServerTrustResponseJson() {
    cJSON* root = cJSON_CreateObject();
    cJSON* trust = cJSON_AddObjectToObject(root, "serverTrust");
    cJSON_AddNumberToObject(trust, "version", 1);
    cJSON_AddStringToObject(trust, "server_id", ToHex(kRodakSpki).c_str());
    cJSON_AddStringToObject(trust, "tls_name", "rodak-2d71611acf59fb91.local");
    cJSON_AddStringToObject(trust, "ca_pem", ReadFile(LAN_TRUST_SETUP_TEST_CA).c_str());
    cJSON_AddStringToObject(root, "requestToken", "rotated-token");
    char* printed = cJSON_PrintUnformatted(root);
    std::string out = printed;
    cJSON_free(printed);
    cJSON_Delete(root);
    return out;
}

}  // namespace

RODAK_TEST("生产模块的协议原语与 Rodak 生成的向量逐字节一致") {
    const JsonDoc vectors(ReadFile(LAN_TRUST_SETUP_PROTOCOL_VECTORS));
    const Bytes K = FromHex(vectors.Get({"K"}));
    rodakos::TrustSetupKeys keys;
    RODAK_CHECK(rodakos::DeriveTrustSetupKeys(K.data(), K.size(), keys));
    RODAK_CHECK(ToHex(Bytes(keys.bind.begin(), keys.bind.end())) == vectors.Get({"keys", "bind"}));
    RODAK_CHECK(ToHex(Bytes(keys.server.begin(), keys.server.end())) ==
                vectors.Get({"keys", "server"}));

    std::array<uint8_t, 32> mac{};
    RODAK_CHECK(rodakos::ComputeTrustSetupBinding(
        keys, vectors.Get({"binding", "requestId"}), vectors.Get({"binding", "deviceKey"}),
        FromHex(vectors.Get({"binding", "serverSpki"})),
        FromHex(vectors.Get({"binding", "serverSpki"})), "device", mac));
    RODAK_CHECK(ToHex(Bytes(mac.begin(), mac.end())) == vectors.Get({"binding", "deviceRole"}));

    rodakos::TrustSetupSealed sealed;
    RODAK_CHECK(rodakos::SealTrustSetupPayload(keys.device, vectors.Get({"aead", "requestId"}),
                                               vectors.Get({"aead", "device", "plaintext"}),
                                               sealed, FromHex(vectors.Get({"aead", "nonce"}))));
    Bytes ciphertext;
    RODAK_CHECK(rodakos::DecodeTrustSetupBase64(sealed.ciphertext_base64, 1, 4096, ciphertext));
    RODAK_CHECK(ToHex(ciphertext) == vectors.Get({"aead", "device", "ciphertext"}));
}

RODAK_TEST("首次接入全流程：码正确、证书一致时生成 /trust 请求并解出 server_trust") {
    auto session = TrustSetupSession::Create(kRodakSpki, "482913",
                                             FromHex("00a3915c7e2b48d06f1e83c4a95b7d20"));
    RODAK_CHECK(session != nullptr);
    const auto request = session->RequestObjectJson();
    // salt 首字节为 0 时仍按 16 字节发送（绕开 esp_srp 自带 salt 生成的截短问题）
    RODAK_CHECK(JsonDoc(request).Get({"srpSalt"}) == "AKORXH4rSNBvHoPEqVt9IA==");
    RODAK_CHECK(JsonDoc(request).Get({"seenSpki"}) == ToHex(kRodakSpki));

    auto rodak = RodakAnswer(request, "482913", kRodakSpki);
    std::string body;
    RODAK_CHECK(session->Answer(kRequestId, kDeviceKey, rodak.challenge, "device-secret-1", body) ==
                TrustSetupAnswerResult::kAccepted);
    const JsonDoc trust(body);
    Bytes M2;
    RODAK_CHECK(rodakos::DecodeTrustSetupBase64(trust.Get({"srpM2"}), 64, 64, M2));
    RODAK_CHECK(M2 == rodak.expected_M2);

    // Rodak 侧：校验设备绑定并解密 deviceSecret
    std::array<uint8_t, 32> expected_bind{};
    RODAK_CHECK(rodakos::ComputeTrustSetupBinding(rodak.keys, kRequestId, kDeviceKey, kRodakSpki,
                                                  kRodakSpki, "device", expected_bind));
    RODAK_CHECK(trust.Get({"deviceBind"}) ==
                rodakos::EncodeTrustSetupBase64(expected_bind.data(), expected_bind.size()));
    std::string credentials;
    RODAK_CHECK(rodakos::OpenTrustSetupPayload(
        rodak.keys.device, kRequestId,
        {trust.Get({"credentials", "nonce"}), trust.Get({"credentials", "ciphertext"})},
        credentials));
    RODAK_CHECK(credentials == "{\"deviceSecret\":\"device-secret-1\"}");

    rodakos::TrustSetupSealed response;
    RODAK_CHECK(rodakos::SealTrustSetupPayload(rodak.keys.server, kRequestId,
                                               ServerTrustResponseJson(), response));
    rodakos::ServerTrust server_trust;
    std::string token;
    std::string error;
    RODAK_CHECK(session->OpenResponse(kRequestId, response, server_trust, token, error));
    RODAK_CHECK(server_trust.server_id == ToHex(kRodakSpki));
    RODAK_CHECK(server_trust.tls_name == "rodak-2d71611acf59fb91.local");
    RODAK_CHECK(token == "rotated-token");
}

RODAK_TEST("桌面端输错码时 esp_srp 拒绝 M1，不生成请求体") {
    auto session = TrustSetupSession::Create(kRodakSpki, "482913");
    auto rodak = RodakAnswer(session->RequestObjectJson(), "482914", kRodakSpki);
    std::string body;
    RODAK_CHECK(session->Answer(kRequestId, kDeviceKey, rodak.challenge, "secret", body) ==
                TrustSetupAnswerResult::kProofMismatch);
    RODAK_CHECK(body.empty());
}

RODAK_TEST("TLS 中间人：设备看到的证书不是 Rodak 的，绑定不一致而中止") {
    // 设备握手到的是攻击者证书；Rodak 用自己真实的 SPKI 计算服务器绑定
    auto session = TrustSetupSession::Create(kAttackerSpki, "135790");
    auto rodak = RodakAnswer(session->RequestObjectJson(), "135790", kRodakSpki);
    std::string body;
    RODAK_CHECK(session->Answer(kRequestId, kDeviceKey, rodak.challenge, "secret", body) ==
                TrustSetupAnswerResult::kBindingMismatch);
    RODAK_CHECK(body.empty());
}

RODAK_TEST("下发的 server_id 与握手证书不一致时拒绝写入") {
    auto session = TrustSetupSession::Create(kAttackerSpki, "246801");
    // 攻击者知道码的极端情况下，绑定会通过，但下发的身份必须是握手证书本身
    auto rodak = RodakAnswer(session->RequestObjectJson(), "246801", kAttackerSpki);
    std::string body;
    RODAK_CHECK(session->Answer(kRequestId, kDeviceKey, rodak.challenge, "secret", body) ==
                TrustSetupAnswerResult::kAccepted);
    rodakos::TrustSetupSealed response;
    RODAK_CHECK(rodakos::SealTrustSetupPayload(rodak.keys.server, kRequestId,
                                               ServerTrustResponseJson(), response));
    rodakos::ServerTrust trust;
    std::string token;
    std::string error;
    RODAK_CHECK_FALSE(session->OpenResponse(kRequestId, response, trust, token, error));
    RODAK_CHECK(error == "trust_setup_identity_mismatch");
}

RODAK_TEST("非法客户端公钥在调用 esp_srp 之前被拒绝") {
    auto session = TrustSetupSession::Create(kRodakSpki, "112233");
    std::string body;
    for (const Bytes& A : {Bytes(384, 0), lan_trust_setup::GroupN(), Bytes(383, 1)}) {
        TrustSetupChallenge challenge{A, Bytes(64, 1), Bytes(32, 1)};
        RODAK_CHECK(session->Answer(kRequestId, kDeviceKey, challenge, "secret", body) ==
                    TrustSetupAnswerResult::kInvalidPublicKey);
    }
}

RODAK_TEST("配对码为均匀 6 位数字，严格 base64 拒绝非规范编码") {
    unsigned char bytes[64];
    for (size_t index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = static_cast<unsigned char>(index * 37 + 11);
    }
    lan_trust_setup_queue_random(bytes, sizeof(bytes));
    const auto code = rodakos::GenerateTrustSetupCode();
    RODAK_CHECK(code.size() == 6);
    for (const char digit : code) RODAK_CHECK(digit >= '0' && digit <= '9');
    while (lan_trust_setup_random_remaining() > 0) {
        unsigned char sink;
        esp_fill_random(&sink, 1);
    }

    Bytes out;
    RODAK_CHECK(rodakos::DecodeTrustSetupBase64("AAAA", 3, 3, out));
    RODAK_CHECK_FALSE(rodakos::DecodeTrustSetupBase64("AAB=", 1, 8, out));
    RODAK_CHECK_FALSE(rodakos::DecodeTrustSetupBase64("AA-A", 1, 8, out));
    RODAK_CHECK_FALSE(rodakos::DecodeTrustSetupBase64("AAAA", 4, 8, out));
}
