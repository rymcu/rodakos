#include "phone_os/server_trust.h"

#include <esp_log.h>
#include <mdns.h>
#include <lwip/sockets.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace rodakos {

namespace {

bool EnsureMdns() {
    static std::mutex initialization_mutex;
    static bool ready = false;
    std::lock_guard<std::mutex> lock(initialization_mutex);
    if (!ready) ready = mdns_init() == ESP_OK;
    return ready;
}

std::string TxtValue(const mdns_result_t* item, const char* key, int& count) {
    std::string found;
    count = 0;
    for (size_t index = 0; index < item->txt_count; ++index) {
        const auto& txt = item->txt[index];
        if (txt.key == nullptr || txt.value == nullptr || std::strcmp(txt.key, key) != 0) continue;
        found = item->txt_value_len == nullptr ? std::string(txt.value)
            : std::string(txt.value, item->txt_value_len[index]);
        ++count;
    }
    return found;
}

}  // namespace

std::vector<DiscoveredRodakServer> DiscoverRodakServers() {
    std::vector<DiscoveredRodakServer> servers;
    if (!EnsureMdns()) return servers;
    mdns_result_t* results = nullptr;
    if (mdns_query_ptr("_rodak", "_tcp", 3000, 8, &results) != ESP_OK) return servers;
    for (auto* item = results; item != nullptr && servers.size() < 6; item = item->next) {
        if (item->port == 0) continue;
        int id_count = 0;
        int version_count = 0;
        int name_count = 0;
        const auto id = TxtValue(item, "id", id_count);
        const auto version = TxtValue(item, "v", version_count);
        auto name = TxtValue(item, "name", name_count);
        if (id_count != 1 || version_count != 1 || version != "1" || id.size() != 64 ||
            id.find_first_not_of("0123456789abcdef") != std::string::npos) {
            continue;
        }
        if (name_count != 1 || name.empty() || name.size() > 63) name.clear();
        for (auto* address = item->addr; address != nullptr; address = address->next) {
            if (address->addr.type != ESP_IPADDR_TYPE_V4) continue;
            char numeric[16] = {};
            if (inet_ntop(AF_INET, &address->addr.u_addr.ip4.addr, numeric, sizeof(numeric)) == nullptr ||
                std::strncmp(numeric, "169.254.", 8) == 0) {
                continue;
            }
            const bool duplicate = std::any_of(servers.begin(), servers.end(), [&](const auto& server) {
                return server.server_id == id;
            });
            if (!duplicate) servers.push_back({id, name, numeric, item->port});
            break;
        }
    }
    mdns_query_results_free(results);
    ESP_LOGI("ServerDiscovery", "Found %u unauthenticated Rodak server(s) for first setup",
             static_cast<unsigned>(servers.size()));
    return servers;
}

std::vector<ServerRouteCandidate> DiscoverServerTrustRoutes(const ServerTrust& trust) {
    std::vector<ServerRouteCandidate> routes;
    if (trust.empty() || !EnsureMdns()) return routes;
    mdns_result_t* results = nullptr;
    if (mdns_query_ptr("_rodak", "_tcp", 1500, 8, &results) != ESP_OK) return routes;
    size_t examined = 0;
    for (auto* item = results; item != nullptr && routes.size() < 6 && examined++ < 8; item = item->next) {
        if (item->port == 0 || item->hostname == nullptr) continue;
        std::string hostname = item->hostname;
        if (hostname + ".local" != trust.tls_name && hostname != trust.tls_name) continue;
        std::string id;
        std::string version;
        int id_count = 0;
        int version_count = 0;
        for (size_t index = 0; index < item->txt_count; ++index) {
            const auto& txt = item->txt[index];
            if (txt.key == nullptr || txt.value == nullptr) continue;
            const std::string value = item->txt_value_len == nullptr ? std::string(txt.value)
                : std::string(txt.value, item->txt_value_len[index]);
            if (std::string(txt.key) == "id") { id = value; ++id_count; }
            if (std::string(txt.key) == "v") { version = value; ++version_count; }
        }
        if (id_count != 1 || version_count != 1 || id != trust.server_id || version != "1") continue;
        const std::string origin = "https://" + trust.tls_name +
            (item->port == 443 ? "" : ":" + std::to_string(item->port));
        const auto url = origin + "/api/v1/aiot/devices/bootstrap";
        size_t addresses = 0;
        for (auto* address = item->addr; address != nullptr && routes.size() < 6 && addresses++ < 8;
             address = address->next) {
            char numeric[46] = {};
            if (address->addr.type == ESP_IPADDR_TYPE_V4) {
                if (inet_ntop(AF_INET, &address->addr.u_addr.ip4.addr, numeric, sizeof(numeric)) == nullptr) continue;
                // A discovered route has no interface scope. Do not admit new
                // link-local IPv4 routes that cannot follow a network change;
                // persisted authorities still use the compatible route parser.
                if (std::strncmp(numeric, "169.254.", 8) == 0) continue;
            } else if (address->addr.type == ESP_IPADDR_TYPE_V6) {
                if (address->addr.u_addr.ip6.zone != 0 ||
                    inet_ntop(AF_INET6, address->addr.u_addr.ip6.addr, numeric, sizeof(numeric)) == nullptr) continue;
            } else continue;
            const auto canonical = NormalizeServerRouteAddress(numeric);
            if (canonical.empty()) continue;
            const bool duplicate = std::any_of(routes.begin(), routes.end(), [&](const auto& route) {
                return route.bootstrap_url == url && route.connect_address == canonical;
            });
            if (!duplicate) {
                ESP_LOGI("ServerDiscovery", "Unverified route candidate: address=%s https_port=%u",
                         canonical.c_str(), static_cast<unsigned>(item->port));
                routes.push_back({url, canonical});
            }
        }
    }
    mdns_query_results_free(results);
    ESP_LOGI("ServerDiscovery", "Found %u candidate route(s); TLS authentication required",
             static_cast<unsigned>(routes.size()));
    return routes;
}

}  // namespace rodakos
