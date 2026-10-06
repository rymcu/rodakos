#include "test_framework.h"
#include "host_runtime.h"
#include "phone_os/device_cloud_config.h"
#include "phone_os/server_trust_transport.h"

#include <cJSON.h>

using rodakos::DeviceCloudConfig;
using rodakos::DeviceCloudConfigService;
using rodakos::ProvisioningUrlSaveResult;

namespace {
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
    authority.active = {trust_test::BootstrapUrl(), trust_test::TestTrust(), true};
    authority.pending = {trust_test::BootstrapUrl(9555), trust_test::TestTrust(), true};
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
    trust_test::RespondBound(9555);
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
    const auto urls = rodakos::DiscoverServerTrustBootstrapUrls(trust);
    RODAK_CHECK_EQ(urls.size(), 3U);
    RODAK_CHECK_EQ(urls.front(), trust_test::BootstrapUrl(1000));
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
    trust_test::RespondBound(9555);
    trust_test::replies["https://" + trust.tls_name +
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
