#pragma once
#include "host_sdk.h"
struct mdns_txt_item_t { const char* key; const char* value; };
struct mdns_result_t {
    mdns_result_t* next = nullptr;
    char* hostname = nullptr;
    uint16_t port = 0;
    mdns_txt_item_t* txt = nullptr;
    uint8_t* txt_value_len = nullptr;
    size_t txt_count = 0;
};
int mdns_init();
int mdns_query_ptr(const char*, const char*, uint32_t, size_t, mdns_result_t**);
void mdns_query_results_free(mdns_result_t*);
