#pragma once
#include "host_sdk.h"
struct mdns_txt_item_t { const char* key; const char* value; };
constexpr unsigned ESP_IPADDR_TYPE_V4 = 0;
constexpr unsigned ESP_IPADDR_TYPE_V6 = 6;
struct mdns_ip_addr_t {
    struct {
        union {
            struct { uint32_t addr; } ip4;
            struct { uint32_t addr[4]; uint8_t zone; } ip6;
        } u_addr = {};
        unsigned type = ESP_IPADDR_TYPE_V4;
    } addr;
    mdns_ip_addr_t* next = nullptr;
};
struct mdns_result_t {
    mdns_result_t* next = nullptr;
    char* hostname = nullptr;
    uint16_t port = 0;
    mdns_txt_item_t* txt = nullptr;
    uint8_t* txt_value_len = nullptr;
    size_t txt_count = 0;
    mdns_ip_addr_t* addr = nullptr;
};
int mdns_init();
int mdns_query_ptr(const char*, const char*, uint32_t, size_t, mdns_result_t**);
void mdns_query_results_free(mdns_result_t*);
