#pragma once

#include "phone_os/server_trust.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// 无 USB 首次接入（信任建立模式）的设备端协议，设计见 docs/lan-onboarding-design.md。
//
// 设备是 SRP6a 服务器（ESP-IDF esp_srp），Rodak 是客户端。本模块只做密码学与报文
// 编解码，不做 HTTP 或 NVS，主机测试用 tests/lan_trust_setup 中的两份向量逐字节校验。
namespace rodakos {

using TrustSetupBytes = std::vector<uint8_t>;

inline constexpr int kTrustSetupVersion = 1;
inline constexpr size_t kTrustSetupCodeDigits = 6;
inline constexpr size_t kTrustSetupSaltBytes = 16;
inline constexpr size_t kTrustSetupSpkiBytes = 32;

struct TrustSetupKeys {
    std::array<uint8_t, 32> bind{};
    std::array<uint8_t, 32> device{};
    std::array<uint8_t, 32> server{};
};

struct TrustSetupSealed {
    std::string nonce_base64;
    std::string ciphertext_base64;
};

// Rodak 在 awaiting-device 状态下给出的挑战（均已 base64 解码）。
struct TrustSetupChallenge {
    TrustSetupBytes srp_A;
    TrustSetupBytes srp_M1;
    TrustSetupBytes server_bind;
};

enum class TrustSetupAnswerResult {
    kAccepted,
    // 客户端公钥不合法；不调用 esp_srp（其拒绝分支有未定义行为）。
    kInvalidPublicKey,
    // M1 不对：桌面端输错码或冒充者。
    kProofMismatch,
    // 证书绑定 HMAC 不一致：设备连到的不是 Rodak 自己的证书（TLS 中间人）。
    kBindingMismatch,
    kInternalError,
};

const char* TrustSetupAbortReason(TrustSetupAnswerResult result);

// 用拒绝采样生成均匀的 6 位码。
std::string GenerateTrustSetupCode();

bool DeriveTrustSetupKeys(const uint8_t* session_key, size_t session_key_len,
                          TrustSetupKeys& keys);
bool ComputeTrustSetupBinding(const TrustSetupKeys& keys, const std::string& request_id,
                              const std::string& device_key, const TrustSetupBytes& seen_spki,
                              const TrustSetupBytes& server_spki, const char* role,
                              std::array<uint8_t, 32>& mac);
// AES-256-GCM，AAD 为 requestId；nonce 为空时生成随机 12 字节。
bool SealTrustSetupPayload(const std::array<uint8_t, 32>& key, const std::string& request_id,
                           const std::string& plaintext, TrustSetupSealed& sealed,
                           const TrustSetupBytes& nonce = {});
bool OpenTrustSetupPayload(const std::array<uint8_t, 32>& key, const std::string& request_id,
                           const TrustSetupSealed& sealed, std::string& plaintext);

bool DecodeTrustSetupBase64(const std::string& text, size_t min_bytes, size_t max_bytes,
                            TrustSetupBytes& out);
std::string EncodeTrustSetupBase64(const uint8_t* data, size_t length);

// 一次首次接入会话：持有 6 位码与 esp_srp 状态，只存在内存中。
class TrustSetupSession {
public:
    // code 与 salt 为空时随机生成；测试注入固定值以复现向量。
    static std::unique_ptr<TrustSetupSession> Create(const TrustSetupBytes& seen_spki,
                                                     std::string code = {},
                                                     TrustSetupBytes salt = {});
    ~TrustSetupSession();
    TrustSetupSession(const TrustSetupSession&) = delete;
    TrustSetupSession& operator=(const TrustSetupSession&) = delete;

    const std::string& code() const { return code_; }
    const TrustSetupBytes& seen_spki() const { return seen_spki_; }

    // binding/request 中的 trustSetup 对象（JSON 文本）。
    std::string RequestObjectJson() const;

    // 校验 Rodak 挑战；通过时生成 /trust 请求体（含加密的 deviceSecret）并保留密钥。
    TrustSetupAnswerResult Answer(const std::string& request_id, const std::string& device_key,
                                  const TrustSetupChallenge& challenge,
                                  const std::string& device_secret, std::string& trust_body_json);

    // 解密 /trust 响应中的 sealed，取出 server_trust 与轮换后的 requestToken。
    // server_id 必须等于首次连接时记录的证书指纹。
    bool OpenResponse(const std::string& request_id, const TrustSetupSealed& sealed,
                      ServerTrust& trust, std::string& request_token, std::string& error) const;

private:
    TrustSetupSession() = default;

    struct SrpState;
    std::unique_ptr<SrpState> srp_;
    std::string code_;
    TrustSetupBytes salt_;
    TrustSetupBytes B_;
    TrustSetupBytes seen_spki_;
    TrustSetupKeys keys_{};
    bool keys_ready_ = false;
};

// 调用 esp_srp_get_session_key 之前的预检：384 字节且 A mod N != 0。
bool ValidTrustSetupClientPublicKey(const TrustSetupBytes& A);

}  // namespace rodakos
