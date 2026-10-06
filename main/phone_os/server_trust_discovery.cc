#include "phone_os/server_trust.h"

#include <esp_log.h>
#include <mdns.h>
#include <lwip/sockets.h>

#include <algorithm>
#include <mutex>

namespace rodakos {

std::vector<ServerRouteCandidate> DiscoverServerTrustRoutes(const ServerTrust& trust) {
    std::vector<ServerRouteCandidate> routes;
    if (trust.empty()) return routes;
    static std::mutex initialization_mutex;
    static bool ready = false;
    {
        std::lock_guard<std::mutex> lock(initialization_mutex);
        if (!ready) ready = mdns_init() == ESP_OK;
        if (!ready) return routes;
    }
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
