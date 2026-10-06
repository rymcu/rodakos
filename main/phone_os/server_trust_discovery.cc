#include "phone_os/server_trust.h"

#include <esp_log.h>
#include <mdns.h>

#include <algorithm>
#include <mutex>

namespace rodakos {

std::vector<std::string> DiscoverServerTrustBootstrapUrls(const ServerTrust& trust) {
    std::vector<std::string> urls;
    if (trust.empty()) return urls;
    static std::mutex initialization_mutex;
    static bool ready = false;
    {
        std::lock_guard<std::mutex> lock(initialization_mutex);
        if (!ready) ready = mdns_init() == ESP_OK;
        if (!ready) return urls;
    }
    mdns_result_t* results = nullptr;
    if (mdns_query_ptr("_rodak", "_tcp", 1500, 8, &results) != ESP_OK) return urls;
    for (auto* item = results; item != nullptr && urls.size() < 3; item = item->next) {
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
        if (std::find(urls.begin(), urls.end(), url) == urls.end()) urls.push_back(url);
    }
    mdns_query_results_free(results);
    ESP_LOGI("ServerDiscovery", "Found %u candidate endpoint(s); TLS authentication required",
             static_cast<unsigned>(urls.size()));
    return urls;
}

}  // namespace rodakos
