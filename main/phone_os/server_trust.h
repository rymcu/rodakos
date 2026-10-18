#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct cJSON;

namespace rodakos {

inline constexpr size_t kServerTrustMaxCertificateBytes = 1536;
inline constexpr size_t kServerAuthorityMaxRecordBytes = 6144;
// NVS strings are a single-page item and include their terminating NUL.
inline constexpr size_t kServerAuthorityMaxStoredRecordBytes = 3999;

struct ServerTrust {
    int version = 1;
    std::string server_id;
    std::string tls_name;
    std::string ca_pem;

    bool empty() const { return server_id.empty(); }
};

struct ServerEndpoint {
    std::string bootstrap_url;
    ServerTrust trust;
    bool requires_bound_identity = false;
    std::string connect_address;
};

struct ServerRouteCandidate {
    std::string bootstrap_url;
    std::string connect_address;
};

// A candidate is never an active credential destination. Only a complete
// authenticated exchange promotes it; failed discovery retains the old record.
struct ServerAuthority {
    ServerEndpoint active;
    ServerEndpoint pending;
};

bool ValidateServerTrust(const ServerTrust& trust, std::string& error);
bool ParseServerTrust(const cJSON* object, ServerTrust& trust, std::string& error);
bool DecodeServerAuthority(const std::string& encoded, ServerAuthority& authority,
                           std::string& error);
bool EncodeServerAuthority(const ServerAuthority& authority, std::string& encoded);
bool SameServerTrust(const ServerTrust& left, const ServerTrust& right);
std::string ServerTrustUrlOrigin(const std::string& url);
bool IsServerTrustBootstrap(const ServerTrust& trust, const std::string& url);
bool IsServerTrustHttpDestination(const ServerTrust& trust,
                                  const std::string& bootstrap_url,
                                  const std::string& url);
bool IsServerTrustVoiceDestination(const ServerTrust& trust,
                                   const std::string& bootstrap_url,
                                   const std::string& url);
std::string ServerAuthorityKey(const ServerEndpoint& endpoint);
// Numeric unicast routing only. A route never supplies a TLS name or authority.
std::string NormalizeServerRouteAddress(const std::string& address);
std::string ServerTrustConnectUrl(const ServerTrust& trust, const std::string& logical_url,
                                 const std::string& connect_address);

// DNS-SD is only a bounded, unauthenticated source of candidate addresses/ports. The
// caller must authenticate every returned endpoint with the existing TLS pin.
std::vector<ServerRouteCandidate> DiscoverServerTrustRoutes(const ServerTrust& trust);

// 首次接入用：列出局域网上全部 `_rodak._tcp`（尚无可固定的身份）。结果只是未认证的提示，
// server_id 和名称都可被伪造；身份由 SRP 之上的证书绑定 HMAC 认证。
struct DiscoveredRodakServer {
    std::string server_id;
    std::string display_name;
    std::string address;
    uint16_t port = 0;
};
std::vector<DiscoveredRodakServer> DiscoverRodakServers();

}  // namespace rodakos
