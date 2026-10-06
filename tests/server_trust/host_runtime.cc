#include "host_runtime.h"
#include "host_sdk.h"
#include "mdns.h"
#include "settings.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>

namespace trust_test {
std::map<std::string, std::string> strings;
std::map<std::string, bool> booleans;
std::map<std::string, int> integers;
std::map<std::string, Reply> replies;
std::vector<Request> requests;
std::vector<Discovery> discoveries;
std::string read_error_key;
std::string write_error_key;
unsigned discovery_calls = 0;

void Reset() {
    strings.clear(); booleans.clear(); integers.clear(); replies.clear(); requests.clear();
    discoveries.clear(); read_error_key.clear(); write_error_key.clear(); discovery_calls = 0;
}

rodakos::ServerTrust TestTrust() {
    rodakos::ServerTrust trust;
    trust.server_id = "2d71611acf59fb91c453de3fa25796cd9879047f92f9d4af43e2d365bec2d22b";
    trust.tls_name = "rodak-2d71611acf59fb91.local";
    std::ifstream source(TEST_CA_PATH);
    trust.ca_pem.assign(std::istreambuf_iterator<char>(source), {});
    return trust;
}

std::string BootstrapUrl(int port) {
    return "https://" + TestTrust().tls_name + ":" + std::to_string(port) +
           "/api/v1/aiot/devices/bootstrap";
}

void SeedBoundLegacy() {
    strings["board/uuid"] = "stable-client-uuid";
    strings["device_cloud/prov_url"] = "http://192.168.137.1:9080/api/v1/aiot/devices/bootstrap";
    strings["device_cloud/device_secret"] = "existing-device-secret";
    strings["device_cloud/access_token"] = "old-token";
    booleans["device_cloud/registered"] = true;
    booleans["device_cloud/activated"] = true;
    booleans["device_cloud/pending"] = false;
    booleans["device_cloud/unbind_pending"] = false;
    booleans["device_cloud/unbind_ack"] = false;
}

void RespondBound(int port, const std::string& transport) {
    const auto origin = "https://" + TestTrust().tls_name + ":" + std::to_string(port);
    replies[BootstrapUrl(port)] = {200,
        R"({"code":200,"data":{"module":"aiot","protocol":"rodak-aiot","productKey":"rymcu-bigsmart","protocolVersion":1}})"};
    replies[origin + "/api/v1/aiot/devices/auth/token"] = {200,
        R"({"code":200,"data":{"accessToken":"new-token","expiresIn":3600,"unifiedMqtt":{)"
        "\"transport\":\"" + transport + "\",\"broker_address\":\"" + TestTrust().tls_name +
        "\",\"broker_port\":8883,\"http_base_url\":\"" + origin + "\"}}}"};
}
}  // namespace trust_test

Settings::Settings(const std::string& ns, bool read_write) : ns_(ns), read_write_(read_write) {}
Settings::~Settings() = default;
bool Settings::Commit() { return true; }
SettingsStringReadStatus Settings::ReadString(const std::string& key, std::string& value, size_t max) {
    const auto name = ns_ + "/" + key;
    value.clear();
    if (trust_test::read_error_key == name) return SettingsStringReadStatus::kError;
    const auto found = trust_test::strings.find(name);
    if (found == trust_test::strings.end()) return SettingsStringReadStatus::kNotFound;
    if (found->second.size() > max) return SettingsStringReadStatus::kTooLarge;
    value = found->second;
    return SettingsStringReadStatus::kOk;
}
std::string Settings::GetString(const std::string& key, const std::string& fallback) {
    std::string value;
    return ReadString(key, value, std::numeric_limits<size_t>::max()) == SettingsStringReadStatus::kOk
        ? value : fallback;
}
SettingsStringWriteStatus Settings::WriteString(const std::string& key, const std::string& value) {
    const auto name = ns_ + "/" + key;
    if (trust_test::write_error_key == name) return SettingsStringWriteStatus::kError;
    trust_test::strings[name] = value;
    return SettingsStringWriteStatus::kOk;
}
bool Settings::SetString(const std::string& key, const std::string& value) {
    return WriteString(key, value) == SettingsStringWriteStatus::kOk;
}
int32_t Settings::GetInt(const std::string& key, int32_t fallback) {
    const auto found = trust_test::integers.find(ns_ + "/" + key);
    return found == trust_test::integers.end() ? fallback : found->second;
}
bool Settings::SetInt(const std::string& key, int32_t value) {
    const auto name = ns_ + "/" + key;
    if (trust_test::write_error_key == name) return false;
    trust_test::integers[name] = value;
    return true;
}
SettingsBoolReadStatus Settings::ReadBool(const std::string& key, bool& value) {
    const auto name = ns_ + "/" + key;
    if (trust_test::read_error_key == name) return SettingsBoolReadStatus::kError;
    const auto found = trust_test::booleans.find(name);
    if (found == trust_test::booleans.end()) return SettingsBoolReadStatus::kNotFound;
    value = found->second;
    return SettingsBoolReadStatus::kOk;
}
bool Settings::GetBool(const std::string& key, bool fallback) {
    bool value = fallback;
    return ReadBool(key, value) == SettingsBoolReadStatus::kOk ? value : fallback;
}
bool Settings::SetBool(const std::string& key, bool value) {
    const auto name = ns_ + "/" + key;
    if (trust_test::write_error_key == name) return false;
    trust_test::booleans[name] = value;
    return true;
}

struct FakeHttp { size_t request_index; trust_test::Reply reply; size_t offset = 0; };
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config) {
    trust_test::Request request;
    request.url = config->url;
    request.certificate = config->cert_pem == nullptr ? "" : config->cert_pem;
    request.common_name = config->common_name == nullptr ? "" : config->common_name;
    request.public_bundle = config->crt_bundle_attach != nullptr;
    request.skip_name = config->skip_cert_common_name_check;
    request.redirects_disabled = config->disable_auto_redirect;
    const auto index = trust_test::requests.size();
    trust_test::requests.push_back(std::move(request));
    const auto found = trust_test::replies.find(config->url);
    return new FakeHttp{index, found == trust_test::replies.end()
        ? trust_test::Reply{0, "", false} : found->second};
}
int esp_http_client_set_timeout_ms(esp_http_client_handle_t, int) { return 0; }
int esp_http_client_set_header(esp_http_client_handle_t, const char*, const char*) { return 0; }
int esp_http_client_open(esp_http_client_handle_t client, int) { return client->reply.tls_ok ? 0 : -1; }
int esp_http_client_write(esp_http_client_handle_t client, const char* body, int size) {
    trust_test::requests[client->request_index].body.append(body, size); return size;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) { return client->reply.body.size(); }
int esp_http_client_get_status_code(esp_http_client_handle_t client) { return client->reply.status; }
int esp_http_client_read_response(esp_http_client_handle_t client, char* data, int size) {
    const auto count = std::min(static_cast<size_t>(size), client->reply.body.size() - client->offset);
    std::memcpy(data, client->reply.body.data() + client->offset, count);
    client->offset += count; return static_cast<int>(count);
}
int esp_http_client_read(esp_http_client_handle_t client, char* data, int size) {
    return esp_http_client_read_response(client, data, size);
}
int esp_http_client_close(esp_http_client_handle_t) { return 0; }
int esp_http_client_cleanup(esp_http_client_handle_t client) { delete client; return 0; }

int mdns_init() { return 0; }
int mdns_query_ptr(const char*, const char*, uint32_t, size_t maximum, mdns_result_t** result) {
    ++trust_test::discovery_calls;
    mdns_result_t** next = result;
    for (size_t i = 0; i < std::min(maximum, trust_test::discoveries.size()); ++i) {
        const auto& item = trust_test::discoveries[i];
        *next = new mdns_result_t;
        (*next)->hostname = strdup(item.hostname.c_str());
        (*next)->port = item.port;
        (*next)->txt_count = 2;
        (*next)->txt = new mdns_txt_item_t[2];
        (*next)->txt[0] = {strdup("id"), strdup(item.server_id.c_str())};
        (*next)->txt[1] = {strdup("v"), strdup(item.version.c_str())};
        next = &(*next)->next;
    }
    return 0;
}
void mdns_query_results_free(mdns_result_t* item) {
    while (item != nullptr) {
        auto* next = item->next;
        free(item->hostname);
        for (size_t i = 0; i < item->txt_count; ++i) {
            free(const_cast<char*>(item->txt[i].key)); free(const_cast<char*>(item->txt[i].value));
        }
        delete[] item->txt; delete item; item = next;
    }
}
