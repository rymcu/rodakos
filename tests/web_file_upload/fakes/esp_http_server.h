#pragma once
#include "esp_err.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using httpd_handle_t = void*;
using httpd_req_t = struct httpd_req_t;
using httpd_uri_func_t = esp_err_t (*)(httpd_req_t*);
enum httpd_method_t { HTTP_GET, HTTP_POST };
struct httpd_req_t {
    void* user_ctx = nullptr;
    int content_len = 0;
    std::string query;
    std::vector<char> body;
    size_t body_offset = 0;
    std::string response_status;
    std::string response_type;
    std::string response_body;
    std::map<std::string, std::string> response_headers;
};
struct httpd_uri_t { const char* uri; httpd_method_t method; httpd_uri_func_t handler; void* user_ctx; };
struct httpd_config_t {
    uint16_t server_port = 0;
    uint16_t ctrl_port = 0;
    bool lru_purge_enable = false;
    size_t stack_size = 0;
    bool (*uri_match_fn)(const char*, const char*, size_t) = nullptr;
};
#define HTTPD_RESP_USE_STRLEN (-1)
#define HTTPD_SOCK_ERR_TIMEOUT (-11)
inline httpd_config_t HTTPD_DEFAULT_CONFIG() { return {}; }
inline bool httpd_uri_match_wildcard(const char*, const char*, size_t) { return false; }
inline esp_err_t httpd_start(httpd_handle_t* out, const httpd_config_t*) { *out = reinterpret_cast<void*>(1); return ESP_OK; }
inline esp_err_t httpd_stop(httpd_handle_t) { return ESP_OK; }
inline esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t*) { return ESP_OK; }
inline esp_err_t httpd_req_get_url_query_str(httpd_req_t* req, char* out, size_t len) {
    if (req == nullptr || req->query.empty() || len == 0 || req->query.size() + 1 > len) return ESP_FAIL;
    std::memcpy(out, req->query.c_str(), req->query.size() + 1); return ESP_OK;
}
inline esp_err_t httpd_query_key_value(const char* query, const char* key, char* out, size_t len) {
    if (query == nullptr || key == nullptr || out == nullptr || len == 0) return ESP_FAIL;
    const std::string needle = std::string(key) + "=";
    const char* begin = std::strstr(query, needle.c_str());
    if (!begin) return ESP_FAIL;
    begin += needle.size(); const char* end = std::strchr(begin, '&');
    const size_t count = end ? static_cast<size_t>(end - begin) : std::strlen(begin);
    if (count + 1 > len) return ESP_FAIL;
    std::memcpy(out, begin, count); out[count] = '\0'; return ESP_OK;
}
inline int httpd_req_recv(httpd_req_t* req, char* out, size_t len) {
    if (req == nullptr || req->body_offset >= req->body.size()) return 0;
    const size_t count = std::min(len, req->body.size() - req->body_offset);
    std::memcpy(out, req->body.data() + req->body_offset, count);
    req->body_offset += count; return static_cast<int>(count);
}
inline esp_err_t httpd_resp_set_status(httpd_req_t* req, const char* status) { req->response_status = status ? status : ""; return ESP_OK; }
inline esp_err_t httpd_resp_set_type(httpd_req_t* req, const char* type) { req->response_type = type ? type : ""; return ESP_OK; }
inline esp_err_t httpd_resp_set_hdr(httpd_req_t* req, const char* key, const char* value) { req->response_headers[key] = value; return ESP_OK; }
inline esp_err_t httpd_resp_send(httpd_req_t* req, const char* data, ssize_t len) { if (data) req->response_body.append(data, len < 0 ? std::strlen(data) : static_cast<size_t>(len)); return ESP_OK; }
inline esp_err_t httpd_resp_sendstr(httpd_req_t* req, const char* data) { return httpd_resp_send(req, data, HTTPD_RESP_USE_STRLEN); }
inline esp_err_t httpd_resp_send_chunk(httpd_req_t* req, const char* data, size_t len) { if (data && len) req->response_body.append(data, len); return ESP_OK; }
