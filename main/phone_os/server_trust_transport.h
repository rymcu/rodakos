#pragma once

#include "phone_os/device_cloud_config.h"
#include <esp_http_client.h>

namespace rodakos {

// The cloud snapshot must outlive the HTTP client: ESP-TLS borrows its strings.
bool ConfigureServerTrustHttp(const DeviceCloudConfig& cloud, const std::string& url,
                               esp_http_client_config_t& http);

}  // namespace rodakos
