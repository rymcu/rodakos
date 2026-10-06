#include "phone_os/server_trust.h"

#include <lwip/sockets.h>
#include <cstring>

namespace rodakos {

std::string NormalizeServerRouteAddress(const std::string& address) {
    if (address.empty() || address.size() > 45 || address.find('%') != std::string::npos ||
        address.find('\0') != std::string::npos) return {};
    unsigned char bytes[16] = {};
    int family = AF_INET;
    if (inet_pton(AF_INET, address.c_str(), bytes) == 1) {
        if (bytes[0] == 0 || bytes[0] == 127 || bytes[0] >= 224) return {};
    } else {
        family = AF_INET6;
        if (inet_pton(AF_INET6, address.c_str(), bytes) != 1 || bytes[0] == 0xff ||
            (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80)) return {};
        // Scoped link-local routes need an interface lifetime contract. Do not
        // silently persist a scope-less address or IPv4-mapped bypass instead.
        const unsigned char zero[16] = {};
        if (std::memcmp(bytes, zero, 15) == 0 ||
            (std::memcmp(bytes, zero, 10) == 0 && bytes[10] == 0xff && bytes[11] == 0xff)) return {};
    }
    char normalized[46] = {};
    return inet_ntop(family, bytes, normalized, sizeof(normalized)) == nullptr ? "" : normalized;
}

std::string ServerTrustConnectUrl(const ServerTrust& trust, const std::string& logical_url,
                                 const std::string& connect_address) {
    if (trust.empty()) return connect_address.empty() ? logical_url : "";
    const auto separator = logical_url.find("://");
    if (separator == std::string::npos) return {};
    const auto scheme = logical_url.substr(0, separator);
    if (scheme != "https" && scheme != "wss" && scheme != "mqtts") return {};
    if (logical_url.find_first_of("\r\n\t @\\#") != std::string::npos ||
        logical_url.find('\0') != std::string::npos) return {};
    const auto prefix = scheme + "://" + trust.tls_name;
    if (logical_url.rfind(prefix, 0) != 0) return {};
    const auto suffix = logical_url.substr(prefix.size());
    if (!suffix.empty() && suffix.front() != ':' && suffix.front() != '/' && suffix.front() != '?') return {};
    if (!suffix.empty() && suffix.front() == ':') {
        const auto port = suffix.substr(1, suffix.find_first_of("/?") - 1);
        if (port.empty() || port.size() > 5 || port.front() == '0') return {};
        unsigned number = 0;
        for (char ch : port) {
            if (ch < '0' || ch > '9') return {};
            number = number * 10 + static_cast<unsigned>(ch - '0');
        }
        if (number == 0 || number > 65535) return {};
    }
    if (connect_address.empty()) return logical_url;
    if (NormalizeServerRouteAddress(connect_address) != connect_address) return {};
    const auto host = connect_address.find(':') == std::string::npos
        ? connect_address : "[" + connect_address + "]";
    return scheme + "://" + host + suffix;
}


}  // namespace rodakos
