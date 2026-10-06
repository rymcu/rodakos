#include "phone_os/server_trust_transport.h"

#include <esp_crt_bundle.h>

namespace rodakos {

bool ConfigureServerTrustHttp(const DeviceCloudConfig& cloud, const std::string& url,
                               esp_http_client_config_t& http, std::string* connect_url) {
    http.url = url.c_str();
    http.disable_auto_redirect = true;
    http.cert_pem = nullptr;
    http.cert_len = 0;
    http.common_name = nullptr;
    http.crt_bundle_attach = nullptr;
    http.use_global_ca_store = false;
    http.skip_cert_common_name_check = false;
    if (cloud.server_trust_error) return false;
    if (cloud.server_trust.empty()) {
        if (!cloud.server_connect_address.empty()) return false;
        http.crt_bundle_attach = esp_crt_bundle_attach;
        return true;
    }
    if (!IsServerTrustHttpDestination(cloud.server_trust, cloud.provisioning_url, url)) {
        return false;
    }
    if (!cloud.server_connect_address.empty()) {
        if (connect_url == nullptr) return false;
        *connect_url = ServerTrustConnectUrl(cloud.server_trust, url, cloud.server_connect_address);
        if (connect_url->empty()) return false;
        http.url = connect_url->c_str();
    }
    http.crt_bundle_attach = nullptr;
    http.use_global_ca_store = false;
    http.skip_cert_common_name_check = false;
    http.cert_pem = cloud.server_trust.ca_pem.c_str();
    http.cert_len = cloud.server_trust.ca_pem.size() + 1;
    http.common_name = cloud.server_trust.tls_name.c_str();
    return true;
}

bool ConfigureServerTrustHttpHost(const DeviceCloudConfig& cloud, const std::string& logical_url,
                                 esp_http_client_handle_t client) {
    if (client == nullptr) return false;
    if (cloud.server_trust.empty()) return cloud.server_connect_address.empty();
    if (!IsServerTrustHttpDestination(cloud.server_trust, cloud.provisioning_url, logical_url)) return false;
    const auto authority = ServerTrustUrlOrigin(logical_url).substr(8);
    return esp_http_client_set_header(client, "Host", authority.c_str()) == ESP_OK;
}

}  // namespace rodakos
