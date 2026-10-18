#pragma once

#include <array>
#include <cstdint>
#include <string>

// 首次接入阶段的 HTTPS 传输：设备此时还不认识服务器，只能“记录指纹”而不验证证书。
//
// 记录下来的 SPKI 会进入 SRP 之上的证书绑定 HMAC，中间人换证书必然被发现
// （docs/lan-onboarding-design.md）。这条传输只给 LanOnboardingService 用，不对其他模块开放。
namespace rodakos {

struct TrustSetupHttpResult {
    int status_code = 0;
    std::string body;
    // 本次握手中服务器叶证书 SubjectPublicKeyInfo 的 SHA-256。
    std::array<uint8_t, 32> peer_spki{};
    bool peer_spki_valid = false;
};

// base_url 为 https://<host>:<port>，path 以 / 开头。bearer_token 为空时不带 Authorization。
bool TrustSetupHttpRequest(const std::string& base_url, const std::string& path,
                           const char* method, const std::string& body,
                           const std::string& bearer_token, TrustSetupHttpResult& result,
                           std::string& error);

}  // namespace rodakos
