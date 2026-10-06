#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct cJSON;

namespace rodakos {

inline constexpr size_t kServerTrustMaxCertificateBytes = 1536;
inline constexpr size_t kServerAuthorityMaxRecordBytes = 6144;

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

// DNS-SD is only a bounded, unauthenticated source of candidate ports. The
// caller must authenticate every returned endpoint with the existing TLS pin.
std::vector<std::string> DiscoverServerTrustBootstrapUrls(const ServerTrust& trust);

}  // namespace rodakos
