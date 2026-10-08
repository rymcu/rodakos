#include "test_framework.h"
#include "host_runtime.h"
#include "phone_os/device_cloud_config.h"
#include "phone_os/server_trust_transport.h"

#include <cJSON.h>

using rodakos::DeviceCloudConfig;
using rodakos::DeviceCloudConfigService;
using rodakos::ProvisioningUrlSaveResult;

namespace {
std::string LegacyAuthorityRecord(const rodakos::ServerAuthority& authority, int version) {
    auto* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", version);
    for (const auto& [name, endpoint] : std::vector<std::pair<const char*, rodakos::ServerEndpoint>>{
             {"active", authority.active}, {"pending", authority.pending}}) {
        if (endpoint.trust.empty()) { cJSON_AddNullToObject(root, name); continue; }
        auto* item = cJSON_AddObjectToObject(root, name);
        cJSON_AddStringToObject(item, "bootstrap_url", endpoint.bootstrap_url.c_str());
        cJSON_AddBoolToObject(item, "requires_bound_identity", endpoint.requires_bound_identity);
        if (version == 2) cJSON_AddStringToObject(item, "connect_address", endpoint.connect_address.c_str());
        auto* trust = cJSON_AddObjectToObject(item, "trust");
        cJSON_AddNumberToObject(trust, "version", endpoint.trust.version);
        cJSON_AddStringToObject(trust, "server_id", endpoint.trust.server_id.c_str());
        cJSON_AddStringToObject(trust, "tls_name", endpoint.trust.tls_name.c_str());
        cJSON_AddStringToObject(trust, "ca_pem", endpoint.trust.ca_pem.c_str());
    }
    char* json = cJSON_PrintUnformatted(root);
    const std::string encoded = json;
    cJSON_free(json); cJSON_Delete(root);
    return encoded;
}

void Install(DeviceCloudConfigService& service) {
    const auto trust = trust_test::TestTrust();
    std::string proof;
    RODAK_CHECK_EQ(service.SaveSerialProvisioning(trust_test::BootstrapUrl(),
        std::string(64, 'a'), proof, &trust), ProvisioningUrlSaveResult::kSaved);
    RODAK_CHECK_EQ(proof.size(), 64U);
}

void Activate(DeviceCloudConfigService& service) {
    Install(service);
    trust_test::RespondBound();
    DeviceCloudConfig config;
    RODAK_CHECK(service.Refresh(config));
    RODAK_CHECK(config.has_mqtt_config);
    RODAK_CHECK_FALSE(config.server_trust_pending);
}

void RespondVoice() {
    trust_test::RespondBound();
    const auto url = rodakos::ServerTrustUrlOrigin(trust_test::BootstrapUrl()) + "/api/v1/aiot/devices/auth/token";
    auto* root = cJSON_Parse(trust_test::replies[url].body.c_str());
    auto* voice = cJSON_Parse(R"({"schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
        "protocolVersion":1,"transport":"websocket","authMode":"device-token",
        "uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
        "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
        "limits":{"maxAudioFrameBytes":8192,"maxControlBytes":65536},"features":[],
        "capabilities":["session","audio-input","audio-output"],
        "events":["session.open","session.ready","input.start","input.stop","wake.detected",
                  "playback.abort","vad","output.start","output.stop","session.end","mcp","error"]})");
    const auto endpoint = "wss://" + trust_test::TestTrust().tls_name + ":9443/voice";
    cJSON_AddStringToObject(voice, "endpoint", endpoint.c_str());
    cJSON_AddItemToObject(cJSON_GetObjectItemCaseSensitive(root, "data"), "realtimeVoice", voice);
    char* json = cJSON_PrintUnformatted(root);
    trust_test::replies[url].body = json;
    cJSON_free(json); cJSON_Delete(root);
}

void RespondRotatedToken(const std::string& token) {
    trust_test::RespondBound();
    const auto url = rodakos::ServerTrustUrlOrigin(trust_test::BootstrapUrl()) +
                     "/api/v1/aiot/devices/auth/token";
    auto* root = cJSON_Parse(trust_test::replies[url].body.c_str());
    auto* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON_ReplaceItemInObjectCaseSensitive(data, "accessToken", cJSON_CreateString(token.c_str()));
    char* json = cJSON_PrintUnformatted(root);
    trust_test::replies[url].body = json;
    cJSON_free(json); cJSON_Delete(root);
}
}  // namespace

RODAK_TEST("USB trust derives the stable identity from certificate SPKI") {
    auto trust = trust_test::TestTrust();
    std::string error;
    RODAK_CHECK(rodakos::ValidateServerTrust(trust, error));
    trust.server_id[63] = trust.server_id[63] == 'a' ? 'b' : 'a';
    RODAK_CHECK_FALSE(rodakos::ValidateServerTrust(trust, error));
    trust = trust_test::TestTrust();
    trust.tls_name = "attacker.local";
    RODAK_CHECK_FALSE(rodakos::ValidateServerTrust(trust, error));
    trust = trust_test::TestTrust();
    trust.ca_pem += trust.ca_pem;
    RODAK_CHECK_FALSE(rodakos::ValidateServerTrust(trust, error));
}

RODAK_TEST("Pinned HTTP allows same origin ticket queries and rejects authority confusion") {
    DeviceCloudConfig cloud;
    cloud.server_trust = trust_test::TestTrust();
    cloud.provisioning_url = trust_test::BootstrapUrl();
    const auto origin = rodakos::ServerTrustUrlOrigin(cloud.provisioning_url);
    esp_http_client_config_t http;
    http.crt_bundle_attach = esp_crt_bundle_attach;
    http.use_global_ca_store = true;
    http.skip_cert_common_name_check = true;
    RODAK_CHECK(rodakos::ConfigureServerTrustHttp(cloud, origin + "/artifact?ticket=opaque", http));
    RODAK_CHECK(http.crt_bundle_attach == nullptr);
    RODAK_CHECK_FALSE(http.use_global_ca_store);
    RODAK_CHECK_FALSE(http.skip_cert_common_name_check);
    RODAK_CHECK(http.disable_auto_redirect);
    RODAK_CHECK_EQ(std::string(http.cert_pem), cloud.server_trust.ca_pem);
    RODAK_CHECK_EQ(std::string(http.common_name), cloud.server_trust.tls_name);
    for (const auto& url : std::vector<std::string>{"http://" + cloud.server_trust.tls_name + ":9443/secret",
         "https://evil.local:9443/secret", "https://" + cloud.server_trust.tls_name + ":9555/secret",
         "https://" + cloud.server_trust.tls_name + ":9443@evil.local/secret",
         origin + "/#fragment", origin + "/\rheader"}) {
        RODAK_CHECK_FALSE(rodakos::ConfigureServerTrustHttp(cloud, url, http));
    }
    cloud.server_trust_error = true;
    RODAK_CHECK_FALSE(rodakos::ConfigureServerTrustHttp(cloud, origin + "/token", http));
}

RODAK_TEST("HTTP helper clears stale borrowed TLS pointers even for legacy snapshots") {
    DeviceCloudConfig cloud;
    esp_http_client_config_t http;
    http.cert_pem = "stale"; http.cert_len = 5; http.common_name = "stale";
    http.use_global_ca_store = true; http.skip_cert_common_name_check = true;
    RODAK_CHECK(rodakos::ConfigureServerTrustHttp(cloud, "http://legacy.local", http));
    RODAK_CHECK(http.cert_pem == nullptr); RODAK_CHECK(http.common_name == nullptr);
    RODAK_CHECK_EQ(http.cert_len, 0U); RODAK_CHECK_FALSE(http.use_global_ca_store);
    RODAK_CHECK_FALSE(http.skip_cert_common_name_check); RODAK_CHECK(http.crt_bundle_attach != nullptr);
}

RODAK_TEST("Authority record roundtrips active and candidate without admitting a changed pin") {
    rodakos::ServerAuthority authority;
    authority.active = {trust_test::BootstrapUrl(), trust_test::TestTrust(), true, {}};
    authority.pending = {trust_test::BootstrapUrl(9555), trust_test::TestTrust(), true, {}};
    std::string encoded;
    RODAK_CHECK(rodakos::EncodeServerAuthority(authority, encoded));
    rodakos::ServerAuthority decoded;
    std::string error;
    RODAK_CHECK(rodakos::DecodeServerAuthority(encoded, decoded, error));
    RODAK_CHECK_EQ(decoded.active.bootstrap_url, authority.active.bootstrap_url);
    RODAK_CHECK_EQ(decoded.pending.bootstrap_url, authority.pending.bootstrap_url);
    RODAK_CHECK(decoded.pending.requires_bound_identity);
    RODAK_CHECK_FALSE(rodakos::DecodeServerAuthority(encoded + "garbage", decoded, error));
    RODAK_CHECK_FALSE(rodakos::DecodeServerAuthority("{}", decoded, error));
}

RODAK_TEST("Existing binding USB upgrade stays pending and leaves old identity untouched") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    Install(service);
    DeviceCloudConfig config;
    service.Load(config);
    RODAK_CHECK(config.server_trust_pending);
    RODAK_CHECK(config.server_requires_bound_identity);
    RODAK_CHECK(config.aiot_registered && config.aiot_activated);
    RODAK_CHECK_FALSE(config.has_mqtt_config);
    RODAK_CHECK_FALSE(config.has_realtime_voice_config);
    RODAK_CHECK_EQ(config.aiot_device_secret, "existing-device-secret");
    RODAK_CHECK_EQ(trust_test::strings["device_cloud/prov_url"],
        "http://192.168.137.1:9080/api/v1/aiot/devices/bootstrap");
}

RODAK_TEST("TLS rejection does not expose a secret or mutate active authority") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    Install(service);
    const auto original = trust_test::strings;
    DeviceCloudConfig config;
    RODAK_CHECK_FALSE(service.Refresh(config));
    RODAK_CHECK_EQ(trust_test::strings, original);
    for (const auto& request : trust_test::requests) {
        RODAK_CHECK(request.body.empty());
        RODAK_CHECK_EQ(request.certificate, trust_test::TestTrust().ca_pem);
        RODAK_CHECK_FALSE(request.public_bundle);
        RODAK_CHECK_FALSE(request.skip_name);
    }
}

RODAK_TEST("Successful TLS token authentication promotes existing identity without pairing") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    Activate(service);
    DeviceCloudConfig config;
    RODAK_CHECK(service.Load(config));
    RODAK_CHECK_FALSE(config.server_trust_pending);
    RODAK_CHECK_EQ(config.aiot_device_secret, "existing-device-secret");
    RODAK_CHECK_EQ(config.mqtt_broker_port, 8883);
    RODAK_CHECK_EQ(config.mqtt_broker_address, trust_test::TestTrust().tls_name);
    RODAK_CHECK_EQ(trust_test::requests.size(), 2U);
    RODAK_CHECK(trust_test::requests.back().body.find("existing-device-secret") != std::string::npos);
    RODAK_CHECK(trust_test::requests.back().url.find("/auth/token") != std::string::npos);
}

RODAK_TEST("Pinned token rejection preserves identity and never creates a replacement pairing") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    Install(service); trust_test::RespondBound();
    trust_test::replies["https://" + trust_test::TestTrust().tls_name +
        ":9443/api/v1/aiot/devices/auth/token"] = {401, R"({"code":401})"};
    const auto before = trust_test::strings;
    DeviceCloudConfig config;
    RODAK_CHECK_FALSE(service.Refresh(config));
    RODAK_CHECK_EQ(trust_test::strings, before);
    RODAK_CHECK(config.aiot_registered && config.aiot_activated);
    for (const auto& request : trust_test::requests) {
        RODAK_CHECK(request.url.find("/binding/requests") == std::string::npos);
    }
}

RODAK_TEST("Pinned token descriptor cannot downgrade MQTT or exfiltrate via a different host") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    Install(service); trust_test::RespondBound(9443, "mqtt");
    const auto before = trust_test::strings;
    DeviceCloudConfig config;
    RODAK_CHECK_FALSE(service.Refresh(config));
    RODAK_CHECK_EQ(trust_test::strings, before);
    trust_test::RespondBound();
    auto& body = trust_test::replies["https://" + trust_test::TestTrust().tls_name +
        ":9443/api/v1/aiot/devices/auth/token"].body;
    body.replace(body.find(trust_test::TestTrust().tls_name), trust_test::TestTrust().tls_name.size(), "evil.local");
    RODAK_CHECK_FALSE(service.Refresh(config));
    RODAK_CHECK_EQ(trust_test::strings, before);
}

RODAK_TEST("DNS-SD candidates are bounded and a valid server survives an impersonator first") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    Activate(service);
    DeviceCloudConfig old;
    service.Load(old);
    trust_test::requests.clear(); trust_test::replies.clear();
    const auto trust = trust_test::TestTrust();
    trust_test::discoveries = {{trust.tls_name, trust.server_id, "1", 9444},
                               {trust.tls_name, trust.server_id, "1", 9555}};
    trust_test::RespondBoundAt("192.168.137.1", 9555);
    DeviceCloudConfig migrated;
    RODAK_CHECK(service.Refresh(migrated));
    RODAK_CHECK_EQ(migrated.provisioning_url, trust_test::BootstrapUrl(9555));
    RODAK_CHECK_FALSE(service.IsVoiceConfigCurrent(old));
    RODAK_CHECK(service.IsVoiceConfigCurrent(migrated));
    RODAK_CHECK_EQ(trust_test::strings["device_cloud/device_secret"], "existing-device-secret");
    for (const auto& request : trust_test::requests) {
        if (request.url.find(":9444/") != std::string::npos) RODAK_CHECK(request.body.empty());
    }
}

RODAK_TEST("DNS-SD ignores unrelated records and never trusts a TXT identity without TLS") {
    trust_test::Reset();
    const auto trust = trust_test::TestTrust();
    trust_test::discoveries = {{"evil.local", trust.server_id, "1", 1},
        {trust.tls_name, std::string(64, 'a'), "1", 2},
        {trust.tls_name, trust.server_id, "2", 3}};
    for (uint16_t port = 1000; port < 1010; ++port) {
        trust_test::discoveries.push_back({trust.tls_name, trust.server_id, "1", port});
    }
    const auto routes = rodakos::DiscoverServerTrustRoutes(trust);
    RODAK_CHECK_EQ(routes.size(), 5U);
    RODAK_CHECK_EQ(routes.front().bootstrap_url, trust_test::BootstrapUrl(1000));
    RODAK_CHECK_EQ(routes.front().connect_address, "192.168.137.1");
}

RODAK_TEST("NVS pin latch rejects missing authority and corrupted identity without plaintext fallback") {
    for (const auto& error_key : {"device_cloud/server_auth", "device_cloud/device_secret",
                                  "device_cloud/registered", "device_cloud/activated"}) {
        trust_test::Reset(); trust_test::SeedBoundLegacy();
        DeviceCloudConfigService service;
        Activate(service);
        trust_test::requests.clear();
        trust_test::read_error_key = error_key;
        DeviceCloudConfig config;
        RODAK_CHECK_FALSE(service.Load(config));
        RODAK_CHECK(config.server_trust_error);
        RODAK_CHECK_FALSE(service.Refresh(config));
        RODAK_CHECK(trust_test::requests.empty());
    }
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    Activate(service);
    trust_test::strings.erase("device_cloud/server_auth");
    trust_test::requests.clear();
    DeviceCloudConfig config;
    RODAK_CHECK_FALSE(service.Refresh(config));
    RODAK_CHECK(trust_test::requests.empty());
}

RODAK_TEST("A cache write failure cannot promote the candidate authority") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    Install(service); trust_test::RespondBound();
    const auto pending = trust_test::strings["device_cloud/server_auth"];
    trust_test::write_error_key = "unified_mqtt/password";
    DeviceCloudConfig config;
    RODAK_CHECK_FALSE(service.Refresh(config));
    RODAK_CHECK_EQ(trust_test::strings["device_cloud/server_auth"], pending);
    RODAK_CHECK_EQ(trust_test::strings["device_cloud/device_secret"], "existing-device-secret");
}

RODAK_TEST("USB cannot downgrade an already pinned server to a numeric HTTP endpoint") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    Activate(service);
    const auto authority = trust_test::strings["device_cloud/server_auth"];
    std::string proof;
    RODAK_CHECK_EQ(service.SaveSerialProvisioning("http://192.168.137.1:9080", "", proof),
                  ProvisioningUrlSaveResult::kFailedRolledBack);
    RODAK_CHECK_EQ(trust_test::strings["device_cloud/server_auth"], authority);
}

RODAK_TEST("Repeated USB refresh and reboot preserve the same pinned device identity") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService first;
    Activate(first);
    const auto original_secret = trust_test::strings["device_cloud/device_secret"];
    DeviceCloudConfigService after_reboot;
    DeviceCloudConfig config;
    RODAK_CHECK(after_reboot.Load(config));
    Install(after_reboot);
    RODAK_CHECK_FALSE(after_reboot.Load(config));
    RODAK_CHECK(config.server_trust_pending);
    RODAK_CHECK(after_reboot.Refresh(config));
    RODAK_CHECK_EQ(trust_test::strings["device_cloud/device_secret"], original_secret);
    RODAK_CHECK_FALSE(config.has_pairing_request);
}

RODAK_TEST("All failed discovered endpoints leave the last authenticated record unchanged") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    Activate(service);
    const auto before = trust_test::strings;
    trust_test::replies.clear();
    const auto trust = trust_test::TestTrust();
    trust_test::discoveries = {{trust.tls_name, trust.server_id, "1", 9555}};
    trust_test::RespondBoundAt("192.168.137.1", 9555);
    trust_test::replies["https://192.168.137.1"
        ":9555/api/v1/aiot/devices/auth/token"] = {403, R"({"code":403})"};
    DeviceCloudConfig config;
    RODAK_CHECK_FALSE(service.Refresh(config));
    RODAK_CHECK_EQ(trust_test::strings, before);
    RODAK_CHECK_EQ(config.provisioning_url, trust_test::BootstrapUrl());
}

RODAK_TEST("Interrupted first trust record persistence cannot reactivate legacy plaintext") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    trust_test::write_error_key = "device_cloud/server_auth";
    const auto trust = trust_test::TestTrust();
    std::string proof;
    RODAK_CHECK_EQ(service.SaveSerialProvisioning(trust_test::BootstrapUrl(),
        std::string(64, 'a'), proof, &trust), ProvisioningUrlSaveResult::kStateUncertain);
    RODAK_CHECK(proof.empty());
    DeviceCloudConfigService after_reboot;
    DeviceCloudConfig config;
    RODAK_CHECK_FALSE(after_reboot.Refresh(config));
    RODAK_CHECK(config.server_trust_error);
    RODAK_CHECK(trust_test::requests.empty());
    RODAK_CHECK_EQ(trust_test::strings["device_cloud/device_secret"], "existing-device-secret");
}

RODAK_TEST("Voice destinations use WSS on the pinned logical origin") {
    const auto trust = trust_test::TestTrust();
    const auto bootstrap = trust_test::BootstrapUrl();
    const auto base = trust.tls_name + ":9443/api/v1/realtime/voice";
    RODAK_CHECK(rodakos::IsServerTrustVoiceDestination(trust, bootstrap, "wss://" + base));
    RODAK_CHECK_FALSE(rodakos::IsServerTrustVoiceDestination(trust, bootstrap, "ws://" + base));
    RODAK_CHECK_FALSE(rodakos::IsServerTrustVoiceDestination(trust, bootstrap,
        "wss://" + trust.tls_name + ":9555/api/v1/realtime/voice"));
    RODAK_CHECK_FALSE(rodakos::IsServerTrustVoiceDestination(trust, bootstrap,
        "wss://evil.local:9443/api/v1/realtime/voice"));
}

RODAK_TEST("Unreadable or missing transaction barriers cannot enable a pinned cache") {
    for (const auto& key : {"device_cloud/pending", "device_cloud/unbind_pending",
                            "device_cloud/unbind_ack"}) {
        for (bool missing : {false, true}) {
            trust_test::Reset(); trust_test::SeedBoundLegacy();
            DeviceCloudConfigService service;
            Activate(service);
            trust_test::requests.clear();
            if (missing) trust_test::booleans.erase(key);
            else trust_test::read_error_key = key;
            DeviceCloudConfig config;
            RODAK_CHECK_FALSE(service.Load(config));
            RODAK_CHECK(config.server_trust_error);
            RODAK_CHECK_FALSE(config.has_mqtt_config);
            RODAK_CHECK_FALSE(service.PrepareVoiceConfig(config));
            RODAK_CHECK_FALSE(service.Refresh(config));
            RODAK_CHECK(trust_test::requests.empty());
        }
    }
}

RODAK_TEST("Numeric dial routes retain the logical origin and fixed TLS name") {
    DeviceCloudConfig cloud;
    cloud.server_trust = trust_test::TestTrust();
    cloud.provisioning_url = trust_test::BootstrapUrl();
    cloud.server_connect_address = "192.168.137.9";
    const auto logical = rodakos::ServerTrustUrlOrigin(cloud.provisioning_url) + "/artifact?ticket=opaque";
    esp_http_client_config_t http = {};
    std::string dial;
    RODAK_CHECK(rodakos::ConfigureServerTrustHttp(cloud, logical, http, &dial));
    RODAK_CHECK_EQ(dial, "https://192.168.137.9:9443/artifact?ticket=opaque");
    RODAK_CHECK_EQ(std::string(http.common_name), cloud.server_trust.tls_name);
    RODAK_CHECK_EQ(std::string(http.cert_pem), cloud.server_trust.ca_pem);
    RODAK_CHECK(http.disable_auto_redirect);
    RODAK_CHECK_FALSE(rodakos::ConfigureServerTrustHttp(cloud, dial, http, &dial));
    RODAK_CHECK_EQ(rodakos::ServerTrustConnectUrl(cloud.server_trust,
        "wss://" + cloud.server_trust.tls_name + ":9443/voice", cloud.server_connect_address),
        "wss://192.168.137.9:9443/voice");
    RODAK_CHECK_EQ(rodakos::ServerTrustConnectUrl(cloud.server_trust,
        "mqtts://" + cloud.server_trust.tls_name + ":8883", "2001:db8::5"),
        "mqtts://[2001:db8::5]:8883");
    for (const auto& bad : {"evil.local", "127.0.0.1", "0.0.0.0", "224.0.0.1",
                            "fe80::1", "fe80::1%3", "::1", "::ffff:127.0.0.1"}) {
        RODAK_CHECK(rodakos::NormalizeServerRouteAddress(bad).empty());
        RODAK_CHECK(rodakos::ServerTrustConnectUrl(cloud.server_trust, logical, bad).empty());
    }
    RODAK_CHECK(rodakos::ServerTrustConnectUrl(cloud.server_trust,
        "https://" + cloud.server_trust.tls_name + ".evil.local/token", cloud.server_connect_address).empty());
}

RODAK_TEST("Authority v1 and v2 remain readable while v3 persists numeric routing hints") {
    rodakos::ServerAuthority authority;
    authority.active = {trust_test::BootstrapUrl(), trust_test::TestTrust(), true, "192.168.137.9"};
    std::string encoded;
    RODAK_CHECK(rodakos::EncodeServerAuthority(authority, encoded));
    rodakos::ServerAuthority decoded;
    std::string error;
    RODAK_CHECK(rodakos::DecodeServerAuthority(encoded, decoded, error));
    RODAK_CHECK_EQ(decoded.active.connect_address, "192.168.137.9");
    for (int version : {1, 2}) {
        RODAK_CHECK(rodakos::DecodeServerAuthority(LegacyAuthorityRecord(authority, version), decoded, error));
        RODAK_CHECK_EQ(decoded.active.connect_address, version == 1 ? "" : "192.168.137.9");
    }
    authority.active.connect_address = "attacker.local";
    RODAK_CHECK_FALSE(rodakos::EncodeServerAuthority(authority, encoded));
}

RODAK_TEST("Same port A candidates skip failed TLS and persist the second route across reboot") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    Activate(service);
    DeviceCloudConfig old; service.Load(old);
    trust_test::requests.clear(); trust_test::replies.clear();
    const auto trust = trust_test::TestTrust();
    trust_test::discoveries = {{trust.tls_name, trust.server_id, "1", 9443,
                               {"192.168.137.8", "192.168.137.9"}}};
    trust_test::RespondBoundAt("192.168.137.9");
    DeviceCloudConfig config;
    RODAK_CHECK(service.Refresh(config));
    RODAK_CHECK_EQ(config.provisioning_url, old.provisioning_url);
    RODAK_CHECK_EQ(config.server_connect_address, "192.168.137.9");
    RODAK_CHECK(config.cloud_generation > old.cloud_generation);
    RODAK_CHECK_FALSE(service.IsVoiceConfigCurrent(old));
    RODAK_CHECK_EQ(trust_test::strings["device_cloud/device_secret"], "existing-device-secret");
    for (const auto& request : trust_test::requests) {
        RODAK_CHECK_EQ(request.host_header, trust.tls_name + ":9443");
        RODAK_CHECK_EQ(request.common_name, trust.tls_name);
        if (request.url.find("192.168.137.8") != std::string::npos) {
            RODAK_CHECK_FALSE(request.tls_verified); RODAK_CHECK(request.body.empty());
        }
        if (!request.body.empty()) {
            RODAK_CHECK(request.tls_verified);
            RODAK_CHECK(request.url.find("192.168.137.9") != std::string::npos);
        }
    }
    DeviceCloudConfigService rebooted;
    RODAK_CHECK(rebooted.Load(config));
    RODAK_CHECK_EQ(config.server_connect_address, "192.168.137.9");
    RODAK_CHECK_EQ(config.mqtt_broker_address, trust.tls_name);
    trust_test::requests.clear(); trust_test::discoveries.clear();
    RODAK_CHECK(rebooted.Refresh(config));
    RODAK_CHECK_EQ(trust_test::requests.size(), 2U);
    RODAK_CHECK(trust_test::requests.front().url.find("192.168.137.9") != std::string::npos);
    trust_test::strings["unified_mqtt/server_key"] = rodakos::ServerAuthorityKey({config.provisioning_url, trust, true, {}});
    RODAK_CHECK_FALSE(rebooted.Load(config));
    RODAK_CHECK_FALSE(config.has_mqtt_config);
}

RODAK_TEST("A successful bootstrap cannot authorize a token connection with failed TLS") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service; Activate(service);
    trust_test::requests.clear(); trust_test::replies.clear();
    const auto trust = trust_test::TestTrust();
    trust_test::discoveries = {{trust.tls_name, trust.server_id, "1", 9443,
                               {"192.168.137.8", "192.168.137.9"}}};
    trust_test::RespondBoundAt("192.168.137.8");
    trust_test::RespondBoundAt("192.168.137.9");
    trust_test::replies["https://192.168.137.8:9443/api/v1/aiot/devices/auth/token"].tls_ok = false;
    DeviceCloudConfig config;
    RODAK_CHECK(service.Refresh(config));
    RODAK_CHECK_EQ(config.server_connect_address, "192.168.137.9");
    for (const auto& request : trust_test::requests) {
        if (request.url.find("192.168.137.8") != std::string::npos) RODAK_CHECK(request.body.empty());
    }
}

RODAK_TEST("Authenticated credential rejection stops address search without re-pairing") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service; Activate(service);
    const auto before = trust_test::strings;
    trust_test::requests.clear(); trust_test::replies.clear();
    const auto trust = trust_test::TestTrust();
    trust_test::discoveries = {{trust.tls_name, trust.server_id, "1", 9443,
                               {"192.168.137.8", "192.168.137.9"}}};
    trust_test::RespondBoundAt("192.168.137.8");
    trust_test::RespondBoundAt("192.168.137.9");
    trust_test::replies["https://192.168.137.8:9443/api/v1/aiot/devices/auth/token"] = {401, R"({"code":401})"};
    DeviceCloudConfig config;
    RODAK_CHECK_FALSE(service.Refresh(config));
    RODAK_CHECK_EQ(trust_test::strings, before);
    for (const auto& request : trust_test::requests) {
        RODAK_CHECK(request.url.find("192.168.137.9") == std::string::npos);
        RODAK_CHECK(request.url.find("binding/request") == std::string::npos);
    }
}

RODAK_TEST("USB cannot supersede in-flight route verification and later invalidates its snapshot") {
    for (bool during_connect : {false, true}) {
        trust_test::Reset(); trust_test::SeedBoundLegacy();
        DeviceCloudConfigService service; Activate(service);
        trust_test::requests.clear(); trust_test::replies.clear();
        const auto trust = trust_test::TestTrust();
        trust_test::discoveries = {{trust.tls_name, trust.server_id, "1", 9443, {"192.168.137.9"}}};
        trust_test::RespondBoundAt("192.168.137.9");
        bool superseded = false;
        const auto supersede = [&]() {
            if (superseded) return;
            superseded = true;
            std::string proof;
            RODAK_CHECK_EQ(service.SaveSerialProvisioning(trust_test::BootstrapUrl(9555), "", proof, &trust),
                          ProvisioningUrlSaveResult::kFailedRolledBack);
        };
        if (during_connect) {
            trust_test::on_http_open = [&](const auto& url) {
                if (url.find("192.168.137.9") != std::string::npos) supersede();
            };
        } else trust_test::on_discovery = supersede;
        DeviceCloudConfig config;
        RODAK_CHECK(service.Refresh(config));
        RODAK_CHECK(superseded);
        RODAK_CHECK_EQ(config.provisioning_url, trust_test::BootstrapUrl());
        RODAK_CHECK_EQ(config.server_connect_address, "192.168.137.9");
        const auto verified = config;
        std::string proof;
        RODAK_CHECK_EQ(service.SaveSerialProvisioning(trust_test::BootstrapUrl(9555), "", proof, &trust),
                      ProvisioningUrlSaveResult::kSaved);
        RODAK_CHECK_FALSE(service.IsVoiceConfigCurrent(verified));
        service.Load(config);
        RODAK_CHECK_EQ(config.provisioning_url, trust_test::BootstrapUrl(9555));
        RODAK_CHECK(config.server_trust_pending);
        RODAK_CHECK(config.server_connect_address.empty());
    }
}

RODAK_TEST("Discovery deduplicates address-port pairs and bounds invalid or scoped records") {
    trust_test::Reset();
    const auto trust = trust_test::TestTrust();
    trust_test::discoveries = {{trust.tls_name, trust.server_id, "1", 9443,
        {"127.0.0.1", "224.0.0.1", "fe80::1", "192.168.137.8", "192.168.137.8", "2001:db8::5"}},
        {trust.tls_name, trust.server_id, "1", 9443, {"192.168.137.8", "192.168.137.9"}},
        {trust.tls_name, trust.server_id, "1", 9555, {"192.168.137.8"}}};
    const auto routes = rodakos::DiscoverServerTrustRoutes(trust);
    RODAK_CHECK_EQ(routes.size(), 4U);
    RODAK_CHECK_EQ(routes[0].connect_address, "192.168.137.8");
    RODAK_CHECK_EQ(routes[1].connect_address, "2001:db8::5");
    RODAK_CHECK_EQ(routes[3].bootstrap_url, trust_test::BootstrapUrl(9555));
    trust_test::discoveries = {{trust.tls_name, trust.server_id, "1", 9443,
        {"192.168.137.1", "192.168.137.2", "192.168.137.3", "192.168.137.4",
         "192.168.137.5", "192.168.137.6", "192.168.137.7", "192.168.137.8"}}};
    const auto bounded = rodakos::DiscoverServerTrustRoutes(trust);
    RODAK_CHECK_EQ(bounded.size(), 6U);
    RODAK_CHECK_EQ(bounded.back().connect_address, "192.168.137.6");
    trust_test::discoveries.front().addresses.assign(8, "127.0.0.1");
    trust_test::discoveries.front().addresses.push_back("192.168.137.9");
    RODAK_CHECK(rodakos::DiscoverServerTrustRoutes(trust).empty());
}

RODAK_TEST("Discovery rejects link-local IPv4 without an interface scope") {
    trust_test::Reset();
    const auto trust = trust_test::TestTrust();
    trust_test::discoveries = {{trust.tls_name, trust.server_id, "1", 9443,
                                {"169.254.1.2", "192.168.137.9"}}};
    const auto routes = rodakos::DiscoverServerTrustRoutes(trust);
    RODAK_CHECK_EQ(routes.size(), 1U);
    RODAK_CHECK_EQ(routes.front().connect_address, "192.168.137.9");
}

RODAK_TEST("Persisted link-local authority remains readable while discovery rejects it") {
    const auto trust = trust_test::TestTrust();
    rodakos::ServerAuthority authority;
    authority.active = {trust_test::BootstrapUrl(), trust, true, "169.254.1.2"};

    std::string encoded;
    RODAK_CHECK(rodakos::EncodeServerAuthority(authority, encoded));
    rodakos::ServerAuthority decoded;
    std::string error;
    RODAK_CHECK(rodakos::DecodeServerAuthority(encoded, decoded, error));
    RODAK_CHECK_EQ(decoded.active.connect_address, "169.254.1.2");
}

RODAK_TEST("Compact authority keeps one trust and rejects conflicting or ambiguous identities") {
    rodakos::ServerAuthority authority;
    authority.active = {trust_test::BootstrapUrl(), trust_test::TestTrust(), true, "192.168.137.9"};
    authority.pending = {trust_test::BootstrapUrl(9555), trust_test::TestTrust(), true, {}};
    std::string encoded;
    RODAK_CHECK(rodakos::EncodeServerAuthority(authority, encoded));
    RODAK_CHECK(encoded.size() + authority.active.trust.ca_pem.size() < LegacyAuthorityRecord(authority, 2).size());
    RODAK_CHECK(encoded.size() <= rodakos::kServerAuthorityMaxStoredRecordBytes);
    const std::string valid = encoded;
    for (int mutation = 0; mutation < 8; ++mutation) {
        auto* root = cJSON_Parse(valid.c_str());
        auto* pending = cJSON_GetObjectItemCaseSensitive(root, "pending");
        if (mutation == 0) cJSON_AddNumberToObject(root, "version", 3);
        if (mutation == 1) cJSON_DeleteItemFromObjectCaseSensitive(root, "trust");
        if (mutation == 2) cJSON_AddItemToObject(pending, "trust", cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(root, "trust"), true));
        if (mutation == 3) cJSON_DeleteItemFromObjectCaseSensitive(pending, "requires_bound_identity");
        if (mutation == 4) cJSON_AddStringToObject(pending, "connect_address", "192.168.137.8");
        if (mutation == 5) {
            cJSON_ReplaceItemInObjectCaseSensitive(root, "active", cJSON_CreateNull());
            cJSON_ReplaceItemInObjectCaseSensitive(root, "pending", cJSON_CreateNull());
        }
        if (mutation == 6) cJSON_AddBoolToObject(root, "unknown", true);
        if (mutation == 7) cJSON_ReplaceItemInObjectCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(root, "trust"), "server_id",
            cJSON_CreateString(std::string(64, '0').c_str()));
        char* json = cJSON_PrintUnformatted(root);
        rodakos::ServerAuthority decoded;
        std::string error;
        RODAK_CHECK_FALSE(rodakos::DecodeServerAuthority(json, decoded, error));
        cJSON_free(json); cJSON_Delete(root);
    }
    authority.pending.trust.server_id.back() = '0';
    RODAK_CHECK_FALSE(rodakos::EncodeServerAuthority(authority, encoded));
    RODAK_CHECK(encoded.empty());
    authority.pending.trust = trust_test::TestTrust();
    authority.pending.trust.ca_pem += "\n";
    std::string error;
    RODAK_CHECK(rodakos::ValidateServerTrust(authority.pending.trust, error));
    for (int version : {1, 2}) {
        rodakos::ServerAuthority decoded;
        RODAK_CHECK_FALSE(rodakos::DecodeServerAuthority(LegacyAuthorityRecord(authority, version), decoded, error));
    }
    RODAK_CHECK_FALSE(rodakos::EncodeServerAuthority(authority, encoded));
}

RODAK_TEST("Legacy authority migrates through bounded same-trust USB writes without replacing identity") {
    for (int version : {1, 2}) {
        trust_test::Reset(); trust_test::SeedBoundLegacy();
        rodakos::ServerAuthority authority;
        authority.active = {trust_test::BootstrapUrl(), trust_test::TestTrust(), true, "192.168.137.9"};
        trust_test::strings["device_cloud/server_auth"] = LegacyAuthorityRecord(authority, version);
        trust_test::booleans["device_cloud/server_pinned"] = true;
        authority.pending = {trust_test::BootstrapUrl(), trust_test::TestTrust(), true, {}};
        std::string compact;
        RODAK_CHECK(rodakos::EncodeServerAuthority(authority, compact));
        trust_test::authority_write_capacity = compact.size() + 1;
        RODAK_CHECK(LegacyAuthorityRecord(authority, 2).size() + 1 > trust_test::authority_write_capacity);
        DeviceCloudConfigService service;
        Install(service);
        DeviceCloudConfigService rebooted;
        DeviceCloudConfig config;
        rebooted.Load(config);
        RODAK_CHECK(config.server_trust_pending);
        RODAK_CHECK(config.server_requires_bound_identity);
        RODAK_CHECK(config.server_connect_address.empty());
        RODAK_CHECK_EQ(config.aiot_device_secret, "existing-device-secret");
        RODAK_CHECK_FALSE(config.has_mqtt_config);
        trust_test::RespondBound();
        RODAK_CHECK(rebooted.Refresh(config));
        RODAK_CHECK_FALSE(config.server_trust_pending);
        for (int repeat = 0; repeat < 3; ++repeat) {
            Install(rebooted);
            RODAK_CHECK(rebooted.Refresh(config));
            RODAK_CHECK_EQ(config.aiot_device_secret, "existing-device-secret");
            RODAK_CHECK_EQ(config.server_trust.server_id, authority.active.trust.server_id);
        }
    }
}

RODAK_TEST("An exhausted authority write preserves the previous record and identity") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    rodakos::ServerAuthority authority;
    authority.active = {trust_test::BootstrapUrl(), trust_test::TestTrust(), true, {}};
    const std::string previous = LegacyAuthorityRecord(authority, 2);
    trust_test::strings["device_cloud/server_auth"] = previous;
    trust_test::booleans["device_cloud/server_pinned"] = true;
    trust_test::authority_write_capacity = 32;
    DeviceCloudConfigService service;
    std::string proof;
    RODAK_CHECK_EQ(service.SaveSerialProvisioning(trust_test::BootstrapUrl(), "", proof,
                                                  &authority.active.trust),
                   ProvisioningUrlSaveResult::kStateUncertain);
    RODAK_CHECK_EQ(trust_test::strings["device_cloud/server_auth"], previous);
    RODAK_CHECK_EQ(trust_test::strings["device_cloud/device_secret"], "existing-device-secret");
    RODAK_CHECK(trust_test::booleans["device_cloud/server_pinned"]);
    RODAK_CHECK(proof.empty());
}

RODAK_TEST("Cloud diagnostics distinguish configuration, network, rejection and refresh failures") {
    using rodakos::CloudDiagnosticCode;
    trust_test::Reset();
    DeviceCloudConfigService empty;
    DeviceCloudConfig config;
    CloudDiagnosticCode failure;
    RODAK_CHECK_FALSE(empty.PrepareVoiceConfig(config, {}, &failure));
    RODAK_CHECK_EQ(failure, CloudDiagnosticCode::kUnconfigured);
    RODAK_CHECK(trust_test::requests.empty());
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service; Activate(service);
    RODAK_CHECK_EQ(service.diagnostic(), CloudDiagnosticCode::kVoiceUnavailable);
    service.InvalidateAccessTokenFreshness("new-token");
    trust_test::replies.clear();
    RODAK_CHECK_FALSE(service.PrepareVoiceConfig(config, {}, &failure));
    RODAK_CHECK_EQ(failure, CloudDiagnosticCode::kNetworkUnavailable);
    trust_test::RespondBound();
    const auto token_url = rodakos::ServerTrustUrlOrigin(trust_test::BootstrapUrl()) + "/api/v1/aiot/devices/auth/token";
    trust_test::replies[token_url] = {401, R"({"message":"raw-secret-token-response"})"};
    RODAK_CHECK_FALSE(service.PrepareVoiceConfig(config, {}, &failure));
    RODAK_CHECK_EQ(failure, CloudDiagnosticCode::kCredentialsRejected);
    RODAK_CHECK_EQ(trust_test::strings["device_cloud/device_secret"], "existing-device-secret");
    trust_test::replies[token_url] = {200, R"({"code":500,"message":"raw-secret-token-response"})"};
    RODAK_CHECK_FALSE(service.PrepareVoiceConfig(config, {}, &failure));
    RODAK_CHECK_EQ(failure, CloudDiagnosticCode::kRefreshFailed);
    RODAK_CHECK(service.last_error().find("raw-secret-token-response") == std::string::npos);
    RespondVoice();
    RODAK_CHECK(service.PrepareVoiceConfig(config, {}, &failure));
    RODAK_CHECK_EQ(service.diagnostic(), CloudDiagnosticCode::kReady);
    RODAK_CHECK(service.last_error().empty());
    RODAK_CHECK(config.has_realtime_voice_config);
    trust_test::AdvanceTimeMs(3600001);
    RODAK_CHECK_EQ(service.diagnostic(), CloudDiagnosticCode::kCredentialsExpired);
    DeviceCloudConfigService reboot;
    reboot.Load(config);
    RODAK_CHECK_EQ(reboot.diagnostic(), CloudDiagnosticCode::kCredentialsExpired);
}

RODAK_TEST("Cancelled HTTP preparation cannot publish ready or mutate credentials") {
    using rodakos::CloudDiagnosticCode;
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service; Activate(service);
    service.InvalidateAccessTokenFreshness("new-token");
    RespondVoice();
    const auto before = trust_test::strings;
    bool allowed = true;
    trust_test::on_http_open = [&](const auto&) { allowed = false; };
    DeviceCloudConfig config;
    CloudDiagnosticCode failure;
    RODAK_CHECK_FALSE(service.PrepareVoiceConfig(config, [&] { return allowed; }, &failure));
    RODAK_CHECK_EQ(failure, CloudDiagnosticCode::kCancelled);
    RODAK_CHECK_EQ(trust_test::strings, before);
    RODAK_CHECK_EQ(config.aiot_token_expires_at_ms, 0);
    trust_test::on_http_open = {};
    RODAK_CHECK(service.PrepareVoiceConfig(config, {}, &failure));
    RODAK_CHECK_EQ(service.diagnostic(), CloudDiagnosticCode::kReady);
}

RODAK_TEST("Repeated voice preparation only refreshes after the credential margin") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service; Activate(service);
    const auto authority = trust_test::strings["device_cloud/server_auth"];
    const auto secret = trust_test::strings["device_cloud/device_secret"];
    DeviceCloudConfig config;
    for (int rotation = 0; rotation < 3; ++rotation) {
        trust_test::requests.clear();
        RODAK_CHECK(service.PrepareVoiceConfig(config));
        RODAK_CHECK(trust_test::requests.empty());
        trust_test::AdvanceTimeMs(3569999);
        RODAK_CHECK(service.PrepareVoiceConfig(config));
        RODAK_CHECK(trust_test::requests.empty());

        trust_test::AdvanceTimeMs(1);
        const std::string token = "voice-rotation-" + std::to_string(rotation);
        RespondRotatedToken(token);
        RODAK_CHECK(service.PrepareVoiceConfig(config));
        RODAK_CHECK_EQ(trust_test::requests.size(), 2U);
        RODAK_CHECK(trust_test::requests.front().url.ends_with("/bootstrap"));
        RODAK_CHECK(trust_test::requests.back().url.ends_with("/auth/token"));
        RODAK_CHECK(trust_test::requests.front().tls_verified);
        RODAK_CHECK(trust_test::requests.back().tls_verified);
        RODAK_CHECK_EQ(config.aiot_access_token, token);
        RODAK_CHECK_EQ(config.mqtt_password, token);
        RODAK_CHECK_EQ(trust_test::strings["device_cloud/server_auth"], authority);
        RODAK_CHECK_EQ(trust_test::strings["device_cloud/device_secret"], secret);
        RODAK_CHECK_FALSE(config.has_pairing_request);
    }
}

RODAK_TEST("Invalid bootstrap identity is rejected before the token exchange") {
    for (const auto& data : {
            R"({"module":"other"})", R"({"protocol":"other"})",
            R"({"productKey":"other"})", R"({"protocolVersion":2})"}) {
        trust_test::Reset(); trust_test::SeedBoundLegacy();
        DeviceCloudConfigService service; Activate(service);
        service.InvalidateAccessTokenFreshness("new-token");
        const auto before_strings = trust_test::strings;
        const auto before_booleans = trust_test::booleans;
        const auto before_integers = trust_test::integers;
        trust_test::requests.clear();
        trust_test::replies[trust_test::BootstrapUrl()] = {
            200, std::string("{\"code\":200,\"data\":") + data + "}"};
        DeviceCloudConfig config;
        rodakos::CloudDiagnosticCode failure;
        RODAK_CHECK_FALSE(service.PrepareVoiceConfig(config, {}, &failure));
        RODAK_CHECK_EQ(failure, rodakos::CloudDiagnosticCode::kRefreshFailed);
        RODAK_CHECK_EQ(trust_test::requests.size(), 1U);
        RODAK_CHECK(trust_test::requests.front().url.ends_with("/bootstrap"));
        RODAK_CHECK_EQ(trust_test::strings, before_strings);
        RODAK_CHECK_EQ(trust_test::booleans, before_booleans);
        RODAK_CHECK_EQ(trust_test::integers, before_integers);
    }
}

RODAK_TEST("Voice refresh restores every persisted namespace after a staged write failure") {
    for (const auto& key : {"device_cloud/access_token", "unified_mqtt/server_key",
                            "unified_mqtt/home_prefix", "realtime_voice/endpoint",
                            "realtime_voice/preferred_vad", "device_cloud/server_auth"}) {
        trust_test::Reset(); trust_test::SeedBoundLegacy();
        DeviceCloudConfigService service; Activate(service);
        service.InvalidateAccessTokenFreshness("new-token");
        const auto before_strings = trust_test::strings;
        const auto before_booleans = trust_test::booleans;
        const auto before_integers = trust_test::integers;
        RespondRotatedToken("must-rollback");
        trust_test::write_error_key = key;
        trust_test::write_error_remaining = 1;
        DeviceCloudConfig config;
        RODAK_CHECK_FALSE(service.PrepareVoiceConfig(config));
        RODAK_CHECK_EQ(trust_test::write_error_remaining, 0U);
        RODAK_CHECK_EQ(trust_test::strings, before_strings);
        RODAK_CHECK_EQ(trust_test::booleans, before_booleans);
        RODAK_CHECK_EQ(trust_test::integers, before_integers);
        RODAK_CHECK_EQ(config.aiot_token_expires_at_ms, 0);
        RODAK_CHECK_EQ(service.last_error(), "Failed to persist AIoT credentials");
        RODAK_CHECK(service.PrepareVoiceConfig(config));
        RODAK_CHECK_EQ(config.aiot_access_token, "must-rollback");
        RODAK_CHECK_EQ(config.mqtt_password, "must-rollback");
    }
}

RODAK_TEST("Cancelled preparation cannot reuse fresh credentials or overwrite the current diagnosis") {
    using rodakos::CloudDiagnosticCode;
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service; Activate(service);
    service.InvalidateAccessTokenFreshness("new-token");
    RespondVoice();
    DeviceCloudConfig config;
    RODAK_CHECK(service.PrepareVoiceConfig(config));
    const auto before = service.diagnostic_state();
    const auto requests_before = trust_test::requests.size();
    trust_test::AdvanceTimeMs(20);
    CloudDiagnosticCode failure;
    RODAK_CHECK_FALSE(service.PrepareVoiceConfig(config, [] { return false; }, &failure));
    RODAK_CHECK_EQ(failure, CloudDiagnosticCode::kCancelled);
    RODAK_CHECK_EQ(config.aiot_token_expires_at_ms, 0);
    RODAK_CHECK_EQ(service.diagnostic_state().code, before.code);
    RODAK_CHECK_EQ(service.diagnostic_state().updated_at_ms, before.updated_at_ms);
    RODAK_CHECK_EQ(service.diagnostic_state().revision, before.revision);
    RODAK_CHECK_EQ(trust_test::requests.size(), requests_before);
    RODAK_CHECK(service.PrepareVoiceConfig(config));
    RODAK_CHECK(config.aiot_token_expires_at_ms > 0);
    RODAK_CHECK_EQ(trust_test::requests.size(), requests_before);
}

RODAK_TEST("Cloud diagnosis ordering advances for rejection, refresh and expiry even within one clock tick") {
    using rodakos::CloudDiagnosticCode;
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service; Activate(service);
    const auto first = service.diagnostic_state();
    service.InvalidateAccessTokenFreshness("new-token");
    const auto rejected = service.diagnostic_state();
    RODAK_CHECK_EQ(rejected.code, CloudDiagnosticCode::kCredentialsRejected);
    RODAK_CHECK(rodakos::IsNewerCloudDiagnostic(rejected.revision, first.revision));
    RespondVoice();
    bool observed_refreshing = false;
    trust_test::on_http_open = [&](const auto&) {
        const auto refreshing = service.diagnostic_state();
        observed_refreshing = refreshing.code == CloudDiagnosticCode::kRefreshing &&
                              rodakos::IsNewerCloudDiagnostic(refreshing.revision, rejected.revision);
    };
    DeviceCloudConfig config;
    RODAK_CHECK(service.PrepareVoiceConfig(config));
    RODAK_CHECK(observed_refreshing);
    const auto ready = service.diagnostic_state();
    RODAK_CHECK_EQ(ready.updated_at_ms, rejected.updated_at_ms);
    RODAK_CHECK(rodakos::IsNewerCloudDiagnostic(ready.revision, rejected.revision));
    trust_test::AdvanceTimeMs(3600001);
    const auto expired = service.diagnostic_state();
    RODAK_CHECK_EQ(expired.code, CloudDiagnosticCode::kCredentialsExpired);
    RODAK_CHECK(rodakos::IsNewerCloudDiagnostic(expired.revision, ready.revision));
    RODAK_CHECK_EQ(service.diagnostic_state().revision, expired.revision);
}

RODAK_TEST("Retrying a legacy bound device never opens pairing after credential rejection") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    const auto before_strings = trust_test::strings;
    const auto before_booleans = trust_test::booleans;
    DeviceCloudConfig config;
    service.Load(config);
    trust_test::replies[config.provisioning_url] = {200,
        R"({"code":200,"data":{"module":"aiot","protocol":"rodak-aiot","productKey":"rymcu-bigsmart","protocolVersion":1}})"};
    const auto token_url = rodakos::ServerTrustUrlOrigin(config.provisioning_url) + "/api/v1/aiot/devices/auth/token";
    trust_test::replies[token_url] = {401, R"({"message":"raw-secret-token-response"})"};
    RODAK_CHECK_FALSE(service.Refresh(config));
    RODAK_CHECK_EQ(service.diagnostic(), rodakos::CloudDiagnosticCode::kCredentialsRejected);
    RODAK_CHECK_EQ(trust_test::strings, before_strings);
    RODAK_CHECK_EQ(trust_test::booleans, before_booleans);
    RODAK_CHECK(config.aiot_registered && config.aiot_activated);
    RODAK_CHECK_FALSE(config.has_pairing_request);
    RODAK_CHECK_EQ(trust_test::requests.size(), 2U);
}

RODAK_TEST("Pairing response text cannot enter cloud errors consumed by MQTT logging") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    trust_test::booleans["device_cloud/registered"] = false;
    trust_test::booleans["device_cloud/activated"] = false;
    DeviceCloudConfigService service;
    DeviceCloudConfig config;
    service.Load(config);
    trust_test::replies[config.provisioning_url] = {200,
        R"({"code":200,"data":{"module":"aiot","protocol":"rodak-aiot","productKey":"rymcu-bigsmart","protocolVersion":1}})"};
    const auto origin = rodakos::ServerTrustUrlOrigin(config.provisioning_url);
    trust_test::replies[origin + "/api/v1/aiot/devices/binding/request"] = {200,
        R"({"code":500,"message":"raw-secret-token-response"})"};
    RODAK_CHECK_FALSE(service.Refresh(config));
    RODAK_CHECK_EQ(service.last_error(), "AIoT pairing request failed: Pairing request was rejected by the server");
}

RODAK_TEST("An in-flight configuration replacement invalidates the preparation diagnosis") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    DeviceCloudConfig config;
    service.Load(config);
    const auto old = config;
    trust_test::on_http_open = [&](const auto&) {
        RODAK_CHECK_EQ(service.SaveProvisioningUrl("http://192.168.137.2:9080/api/v1/aiot/devices/bootstrap"),
                       ProvisioningUrlSaveResult::kSaved);
    };
    rodakos::CloudDiagnosticCode failure;
    RODAK_CHECK_FALSE(service.PrepareVoiceConfig(config, {}, &failure));
    RODAK_CHECK_EQ(failure, rodakos::CloudDiagnosticCode::kCancelled);
    RODAK_CHECK_FALSE(service.IsVoiceConfigCurrent(old));
    RODAK_CHECK_EQ(service.diagnostic(), rodakos::CloudDiagnosticCode::kUnconfigured);
    service.Load(config);
    RODAK_CHECK_EQ(service.diagnostic(), rodakos::CloudDiagnosticCode::kUnconfigured);
    RODAK_CHECK_FALSE(config.has_realtime_voice_config);
    RODAK_CHECK_EQ(config.aiot_device_secret, "existing-device-secret");
}

RODAK_TEST("Cooperative credential deadline exhaustion is a refresh failure rather than a user stop") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service; Activate(service);
    service.InvalidateAccessTokenFreshness("new-token");
    RespondVoice();
    trust_test::on_http_open = [&](const auto&) { trust_test::AdvanceTimeMs(4000); };
    rodakos::CloudDiagnosticCode failure;
    DeviceCloudConfig config;
    RODAK_CHECK_FALSE(service.PrepareVoiceConfig(config, {}, &failure));
    RODAK_CHECK_EQ(failure, rodakos::CloudDiagnosticCode::kRefreshFailed);
    RODAK_CHECK_EQ(service.diagnostic(), failure);
    RODAK_CHECK_EQ(config.aiot_token_expires_at_ms, 0);
}

RODAK_TEST("Automatic refresh reloads the latest same authority token for a deferred consumer") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service; Activate(service);
    const auto authority = trust_test::strings["device_cloud/server_auth"];
    DeviceCloudConfig deferred;
    RespondRotatedToken("rotation-one");
    RODAK_CHECK(service.Refresh(deferred));
    RODAK_CHECK_EQ(deferred.mqtt_password, "rotation-one");
    DeviceCloudConfig newer;
    RespondRotatedToken("rotation-two");
    RODAK_CHECK(service.Refresh(newer));
    RODAK_CHECK(service.Load(deferred));
    RODAK_CHECK_EQ(deferred.mqtt_password, "rotation-two");
    RODAK_CHECK_EQ(deferred.aiot_access_token, newer.aiot_access_token);
    RODAK_CHECK_EQ(deferred.server_authority_record, authority);
    RODAK_CHECK_EQ(deferred.aiot_device_secret, "existing-device-secret");
    RODAK_CHECK(deferred.aiot_registered && deferred.aiot_activated);
    for (const auto& request : trust_test::requests) {
        RODAK_CHECK(request.url.find("binding/request") == std::string::npos);
    }
}

RODAK_TEST("Automatic refresh rejects cross namespace persistence and uncertain rollback") {
    for (bool rollback_fails : {false, true}) {
        trust_test::Reset(); trust_test::SeedBoundLegacy();
        DeviceCloudConfigService service; Activate(service);
        const auto before_strings = trust_test::strings;
        const auto before_booleans = trust_test::booleans;
        const auto before_integers = trust_test::integers;
        RespondRotatedToken("must-not-attach");
        trust_test::write_error_key = "unified_mqtt/password";
        if (!rollback_fails) trust_test::write_error_remaining = 1;
        DeviceCloudConfig rejected;
        RODAK_CHECK_FALSE(service.Refresh(rejected));
        RODAK_CHECK_EQ(service.last_error(), rollback_fails
            ? "AIoT credentials state is uncertain after persistence failure"
            : "Failed to persist AIoT credentials");
        RODAK_CHECK_EQ(trust_test::strings, before_strings);
        RODAK_CHECK_EQ(trust_test::booleans, before_booleans);
        RODAK_CHECK_EQ(trust_test::integers, before_integers);
        RODAK_CHECK_EQ(rejected.mqtt_password, "new-token");
        RODAK_CHECK_EQ(rejected.aiot_access_token, "new-token");
        trust_test::write_error_key.clear();
        DeviceCloudConfigService reboot;
        DeviceCloudConfig persisted;
        RODAK_CHECK(reboot.Load(persisted));
        RODAK_CHECK_EQ(persisted.mqtt_password, "new-token");
    }
}

RODAK_TEST("Automatic refresh cancelled during token HTTP cannot overwrite the new generation") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service;
    DeviceCloudConfig config;
    service.Load(config);
    const auto original = config;
    const auto old_origin = rodakos::ServerTrustUrlOrigin(config.provisioning_url);
    trust_test::RespondBound(9443, "mqtt");
    const auto trusted_origin = rodakos::ServerTrustUrlOrigin(trust_test::BootstrapUrl());
    trust_test::replies[config.provisioning_url] = trust_test::replies[trust_test::BootstrapUrl()];
    trust_test::replies[old_origin + "/api/v1/aiot/devices/auth/token"] =
        trust_test::replies[trusted_origin + "/api/v1/aiot/devices/auth/token"];
    const std::string replacement = "http://192.168.137.2:9080/api/v1/aiot/devices/bootstrap";
    bool superseded = false;
    trust_test::on_http_open = [&](const auto& url) {
        if (url.find("/auth/token") == std::string::npos) return;
        superseded = true;
        RODAK_CHECK_EQ(service.SaveProvisioningUrl(replacement), ProvisioningUrlSaveResult::kSaved);
    };
    RODAK_CHECK_FALSE(service.Refresh(config));
    RODAK_CHECK(superseded);
    RODAK_CHECK_EQ(config.provisioning_url, replacement);
    RODAK_CHECK(config.cloud_generation != original.cloud_generation);
    RODAK_CHECK_FALSE(config.has_mqtt_config);
    RODAK_CHECK(config.aiot_access_token.empty());
    RODAK_CHECK_EQ(config.aiot_device_secret, "existing-device-secret");
    RODAK_CHECK_EQ(trust_test::requests.size(), 2U);
    RODAK_CHECK_EQ(trust_test::discovery_calls, 0U);
    RODAK_CHECK_EQ(service.diagnostic(), rodakos::CloudDiagnosticCode::kUnconfigured);
}

RODAK_TEST("MQTT admission fence rejects a same generation token replaced after snapshot load") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service; Activate(service);
    DeviceCloudConfig old;
    RODAK_CHECK(service.Load(old));
    RespondRotatedToken("newer-fenced-token");
    DeviceCloudConfig current;
    RODAK_CHECK(service.Refresh(current));
    RODAK_CHECK_EQ(current.cloud_generation, old.cloud_generation);
    RODAK_CHECK(service.IsVoiceConfigCurrent(old));
    bool admitted = false;
    RODAK_CHECK_FALSE(service.ApplyIfMqttConfigCurrent(old, [&] { admitted = true; return true; }));
    RODAK_CHECK_FALSE(admitted);
    RODAK_CHECK(service.ApplyIfMqttConfigCurrent(current, [&] { admitted = true; return true; }));
    RODAK_CHECK(admitted);
    RODAK_CHECK_FALSE(service.ApplyIfMqttConfigCurrent(current, [] { return false; }));
}

RODAK_TEST("MQTT admission fence compares credential identity routing and transaction boundaries") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service; Activate(service);
    DeviceCloudConfig current;
    RODAK_CHECK(service.Load(current));
    for (const auto& mutate : std::vector<std::function<void(DeviceCloudConfig&)>>{
        [](auto& value) { ++value.cloud_generation; },
        [](auto& value) { value.mqtt_password = "different"; },
        [](auto& value) { value.aiot_access_token = "different"; },
        [](auto& value) { value.aiot_device_secret = "different"; },
        [](auto& value) { value.server_authority_record = "different"; },
        [](auto& value) { value.server_connect_address = "192.168.137.2"; },
        [](auto& value) { value.server_trust.ca_pem += "\n"; },
        [](auto& value) { value.mqtt_topic_commands += "different"; },
        [](auto& value) { ++value.mqtt_keepalive; },
        [](auto& value) { value.unbind_pending = true; },
        [](auto& value) { value.aiot_pending = true; }
    }) {
        auto stale = current;
        mutate(stale);
        bool admitted = false;
        RODAK_CHECK_FALSE(service.ApplyIfMqttConfigCurrent(stale, [&] { admitted = true; return true; }));
        RODAK_CHECK_FALSE(admitted);
    }
}

RODAK_TEST("MQTT admission preserves legacy MQTT only cache without treating Load false as read failure") {
    trust_test::Reset();
    trust_test::strings["unified_mqtt/broker_address"] = "192.168.137.1";
    trust_test::strings["unified_mqtt/username"] = "legacy-device";
    trust_test::strings["unified_mqtt/password"] = "legacy-mqtt-token";
    trust_test::strings["unified_mqtt/device_key"] = "legacy-device";
    DeviceCloudConfigService service;
    DeviceCloudConfig current;
    RODAK_CHECK_FALSE(service.Load(current));
    RODAK_CHECK(current.has_mqtt_config);
    bool admitted = false;
    RODAK_CHECK(service.ApplyIfMqttConfigCurrent(current, [&] { admitted = true; return true; }));
    RODAK_CHECK(admitted);
    trust_test::strings.erase("unified_mqtt/password");
    admitted = false;
    RODAK_CHECK_FALSE(service.ApplyIfMqttConfigCurrent(current, [&] { admitted = true; return true; }));
    RODAK_CHECK_FALSE(admitted);
}

RODAK_TEST("MQTT admission rejects incomplete AIoT credentials when the pinned authority requires binding") {
    trust_test::Reset(); trust_test::SeedBoundLegacy();
    DeviceCloudConfigService service; Activate(service);
    trust_test::strings.erase("device_cloud/access_token");
    DeviceCloudConfig current;
    RODAK_CHECK_FALSE(service.Load(current));
    RODAK_CHECK(current.has_mqtt_config);
    RODAK_CHECK(current.server_requires_bound_identity);
    bool admitted = false;
    RODAK_CHECK_FALSE(service.ApplyIfMqttConfigCurrent(current, [&] { admitted = true; return true; }));
    RODAK_CHECK_FALSE(admitted);
}
