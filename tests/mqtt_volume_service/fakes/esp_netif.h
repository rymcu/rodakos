#pragma once
#include "host_sdk.h"

esp_netif_t* esp_netif_get_handle_from_ifkey(const char* ifkey);
esp_err_t esp_netif_get_ip_info(esp_netif_t* netif, esp_netif_ip_info_t* info);
