#include "phone_os/server_trust.h"

#include <cJSON.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <new>

namespace rodakos {
namespace {

bool IsLowerHex(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

bool ReadString(const cJSON* object, const char* key, std::string& value, size_t maximum) {
    const auto* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) return false;
    value = item->valuestring;
    return !value.empty() && value.size() <= maximum;
}

bool HasUniqueFields(const cJSON* object, const std::vector<std::string>& fields) {
    if (!cJSON_IsObject(object) || cJSON_GetArraySize(object) != static_cast<int>(fields.size())) {
        return false;
    }
    for (const auto& name : fields) {
        int count = 0;
        for (const auto* field = object->child; field != nullptr; field = field->next) {
            if (field->string != nullptr && name == field->string) ++count;
        }
        if (count != 1) return false;
    }
    return true;
}

cJSON* EncodeTrust(const ServerTrust& trust) {
    auto* object = cJSON_CreateObject();
    if (object == nullptr) return nullptr;
    cJSON_AddNumberToObject(object, "version", trust.version);
    cJSON_AddStringToObject(object, "server_id", trust.server_id.c_str());
    cJSON_AddStringToObject(object, "tls_name", trust.tls_name.c_str());
    cJSON_AddStringToObject(object, "ca_pem", trust.ca_pem.c_str());
    return object;
}

bool DecodeEndpoint(const cJSON* object, ServerEndpoint& endpoint, bool has_route,
                    const ServerTrust* shared_trust, std::string& error) {
    if (cJSON_IsNull(object)) {
        endpoint = {};
        return true;
    }
    const auto* bound = cJSON_GetObjectItemCaseSensitive(object, "requires_bound_identity");
    endpoint.requires_bound_identity = cJSON_IsTrue(bound);
    endpoint.connect_address.clear();
    if (has_route) {
        const auto* route = cJSON_GetObjectItemCaseSensitive(object, "connect_address");
        if (!cJSON_IsString(route) || route->valuestring == nullptr) return false;
        endpoint.connect_address = route->valuestring;
        if (!endpoint.connect_address.empty() &&
            NormalizeServerRouteAddress(endpoint.connect_address) != endpoint.connect_address) return false;
    }
    const bool unique = shared_trust != nullptr
        ? HasUniqueFields(object, {"bootstrap_url", "requires_bound_identity", "connect_address"})
        : has_route
        ? HasUniqueFields(object, {"bootstrap_url", "trust", "requires_bound_identity", "connect_address"})
        : HasUniqueFields(object, {"bootstrap_url", "trust", "requires_bound_identity"});
    if (!unique || !cJSON_IsBool(bound) ||
        !ReadString(object, "bootstrap_url", endpoint.bootstrap_url, 256)) return false;
    if (shared_trust != nullptr) endpoint.trust = *shared_trust;
    else if (!ParseServerTrust(cJSON_GetObjectItemCaseSensitive(object, "trust"),
                               endpoint.trust, error)) return false;
    return IsServerTrustBootstrap(endpoint.trust, endpoint.bootstrap_url);
}

cJSON* EncodeEndpoint(const ServerEndpoint& endpoint) {
    if (endpoint.trust.empty()) return cJSON_CreateNull();
    auto* object = cJSON_CreateObject();
    if (object == nullptr) return nullptr;
    cJSON_AddStringToObject(object, "bootstrap_url", endpoint.bootstrap_url.c_str());
    cJSON_AddBoolToObject(object, "requires_bound_identity", endpoint.requires_bound_identity);
    cJSON_AddStringToObject(object, "connect_address", endpoint.connect_address.c_str());
    return object;
}

bool IsHttpsOriginForName(const std::string& origin, const std::string& name) {
    const auto prefix = "https://" + name;
    if (origin == prefix) return true;
    if (origin.rfind(prefix + ":", 0) != 0) return false;
    const auto port = origin.substr(prefix.size() + 1);
    if (port.empty() || port.size() > 5 || port.front() == '0') return false;
    unsigned value = 0;
    for (char ch : port) {
        if (ch < '0' || ch > '9') return false;
        value = value * 10 + static_cast<unsigned>(ch - '0');
    }
    return value > 0 && value <= 65535;
}

}  // namespace

bool ValidateServerTrust(const ServerTrust& trust, std::string& error) {
    error = "invalid_server_trust";
    if (trust.version != 1 || !IsLowerHex(trust.server_id) ||
        trust.tls_name != "rodak-" + trust.server_id.substr(0, 16) + ".local" ||
        trust.ca_pem.size() > kServerTrustMaxCertificateBytes ||
        trust.ca_pem.rfind("-----BEGIN CERTIFICATE-----\n", 0) != 0 ||
        trust.ca_pem.find('\0') != std::string::npos ||
        trust.ca_pem.find("-----BEGIN CERTIFICATE-----", 1) != std::string::npos) return false;
    auto certificate = std::unique_ptr<mbedtls_x509_crt>(new (std::nothrow) mbedtls_x509_crt);
    if (certificate == nullptr) return false;
    mbedtls_x509_crt_init(certificate.get());
    bool valid = psa_crypto_init() == PSA_SUCCESS &&
        mbedtls_x509_crt_parse(certificate.get(),
            reinterpret_cast<const unsigned char*>(trust.ca_pem.c_str()),
            trust.ca_pem.size() + 1) == 0 && certificate->next == nullptr;
    unsigned char hash[32] = {};
    size_t size = 0;
    valid = valid && certificate->pk_raw.p != nullptr &&
        psa_hash_compute(PSA_ALG_SHA_256, certificate->pk_raw.p, certificate->pk_raw.len,
                         hash, sizeof(hash), &size) == PSA_SUCCESS && size == sizeof(hash);
    std::string actual;
    constexpr char hex[] = "0123456789abcdef";
    if (valid) {
        actual.reserve(64);
        for (unsigned char byte : hash) {
            actual.push_back(hex[byte >> 4]);
            actual.push_back(hex[byte & 15]);
        }
    }
    mbedtls_x509_crt_free(certificate.get());
    if (!valid || actual != trust.server_id) return false;
    error.clear();
    return true;
}

bool ParseServerTrust(const cJSON* object, ServerTrust& trust, std::string& error) {
    ServerTrust parsed;
    const auto* version = cJSON_GetObjectItemCaseSensitive(object, "version");
    if (!HasUniqueFields(object, {"version", "server_id", "tls_name", "ca_pem"}) ||
        !cJSON_IsNumber(version) || version->valuedouble != 1.0 ||
        !ReadString(object, "server_id", parsed.server_id, 64) ||
        !ReadString(object, "tls_name", parsed.tls_name, 63) ||
        !ReadString(object, "ca_pem", parsed.ca_pem, kServerTrustMaxCertificateBytes) ||
        !ValidateServerTrust(parsed, error)) {
        error = "invalid_server_trust";
        return false;
    }
    trust = std::move(parsed);
    return true;
}

bool DecodeServerAuthority(const std::string& encoded, ServerAuthority& authority,
                           std::string& error) {
    if (encoded.empty() || encoded.size() > kServerAuthorityMaxRecordBytes ||
        encoded.find('\0') != std::string::npos || encoded.find("\\u0000") != std::string::npos) {
        error = "invalid_server_authority";
        return false;
    }
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithOpts(encoded.c_str(), &end, true);
    ServerAuthority parsed;
    const auto* version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const bool supported = cJSON_IsNumber(version) &&
        (version->valuedouble == 1.0 || version->valuedouble == 2.0 || version->valuedouble == 3.0);
    const bool compact = supported && version->valuedouble == 3.0;
    ServerTrust shared_trust;
    const bool valid = supported &&
        (compact ? HasUniqueFields(root, {"version", "trust", "active", "pending"})
                 : HasUniqueFields(root, {"version", "active", "pending"})) &&
        (!compact || ParseServerTrust(cJSON_GetObjectItemCaseSensitive(root, "trust"),
                                       shared_trust, error)) &&
        DecodeEndpoint(cJSON_GetObjectItemCaseSensitive(root, "active"), parsed.active,
                       version->valuedouble >= 2.0, compact ? &shared_trust : nullptr, error) &&
        DecodeEndpoint(cJSON_GetObjectItemCaseSensitive(root, "pending"), parsed.pending,
                       version->valuedouble >= 2.0, compact ? &shared_trust : nullptr, error) &&
        (!parsed.active.trust.empty() || !parsed.pending.trust.empty()) &&
        (parsed.active.trust.empty() || parsed.pending.trust.empty() ||
         SameServerTrust(parsed.active.trust, parsed.pending.trust));
    cJSON_Delete(root);
    if (!valid) {
        error = "invalid_server_authority";
        return false;
    }
    authority = std::move(parsed);
    error.clear();
    return true;
}

bool EncodeServerAuthority(const ServerAuthority& authority, std::string& encoded) {
    encoded.clear();
    if ((authority.active.trust.empty() && authority.pending.trust.empty()) ||
        (!authority.active.trust.empty() && !authority.pending.trust.empty() &&
         !SameServerTrust(authority.active.trust, authority.pending.trust))) return false;
    auto* root = cJSON_CreateObject();
    if (root == nullptr) return false;
    cJSON_AddNumberToObject(root, "version", 3);
    cJSON_AddItemToObject(root, "trust", EncodeTrust(authority.active.trust.empty()
                                                   ? authority.pending.trust : authority.active.trust));
    cJSON_AddItemToObject(root, "active", EncodeEndpoint(authority.active));
    cJSON_AddItemToObject(root, "pending", EncodeEndpoint(authority.pending));
    char* json = cJSON_PrintUnformatted(root);
    encoded = json == nullptr ? "" : json;
    cJSON_free(json);
    cJSON_Delete(root);
    if (encoded.size() > kServerAuthorityMaxStoredRecordBytes) {
        encoded.clear();
        return false;
    }
    ServerAuthority roundtrip;
    std::string error;
    return DecodeServerAuthority(encoded, roundtrip, error);
}

bool SameServerTrust(const ServerTrust& left, const ServerTrust& right) {
    return left.version == right.version && left.server_id == right.server_id &&
           left.tls_name == right.tls_name && left.ca_pem == right.ca_pem;
}

std::string ServerTrustUrlOrigin(const std::string& url) {
    if (url.find_first_of("\r\n\t @\\#") != std::string::npos ||
        url.find('\0') != std::string::npos) return {};
    const auto start = url.find("://");
    if (start == std::string::npos) return {};
    const auto end = url.find_first_of("/?", start + 3);
    std::string origin = url.substr(0, end);
    std::transform(origin.begin(), origin.end(), origin.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (origin.rfind("https://", 0) == 0 && origin.size() >= 4 &&
        origin.compare(origin.size() - 4, 4, ":443") == 0) origin.resize(origin.size() - 4);
    return origin;
}

bool IsServerTrustBootstrap(const ServerTrust& trust, const std::string& url) {
    const auto origin = ServerTrustUrlOrigin(url);
    return !trust.empty() && IsHttpsOriginForName(origin, trust.tls_name) &&
           url == origin + "/api/v1/aiot/devices/bootstrap";
}

bool IsServerTrustHttpDestination(const ServerTrust& trust,
                                  const std::string& bootstrap_url, const std::string& url) {
    return IsServerTrustBootstrap(trust, bootstrap_url) &&
           ServerTrustUrlOrigin(url) == ServerTrustUrlOrigin(bootstrap_url);
}

bool IsServerTrustVoiceDestination(const ServerTrust& trust,
                                   const std::string& bootstrap_url, const std::string& url) {
    return url.rfind("wss://", 0) == 0 &&
        IsServerTrustHttpDestination(trust, bootstrap_url, "https://" + url.substr(6));
}

std::string ServerAuthorityKey(const ServerEndpoint& endpoint) {
    if (endpoint.trust.empty()) return {};
    const auto logical_key = endpoint.trust.server_id + "|" + endpoint.bootstrap_url;
    return endpoint.connect_address.empty() ? logical_key : logical_key + "|" + endpoint.connect_address;
}


}  // namespace rodakos
