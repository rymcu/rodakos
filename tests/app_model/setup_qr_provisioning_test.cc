#include "test_framework.h"

#include "phone_os/setup_qr_provisioning.h"

#include <string>
#include <vector>

namespace {

using rodakos::ApplySetupQrProvisioning;
using rodakos::ProvisioningUrlSaveResult;
using rodakos::SetupQrApplyStatus;
using rodakos::SetupQrApplyStatusCode;
using rodakos::SetupQrKind;
using rodakos::SetupQrPayload;
using rodakos::SetupQrServerTrust;

// In-memory store. Each write can be made to fail at a chosen call index.
class FakeStore final : public rodakos::SetupQrProvisioningStore {
public:
    SetupQrServerTrust trust = SetupQrServerTrust::kNone;
    std::string ssid;
    std::string password;
    bool pending = false;
    std::string url = "https://old.example.com/api/v1/aiot/devices/bootstrap";
    ProvisioningUrlSaveResult url_result = ProvisioningUrlSaveResult::kSaved;

    // 1-based call numbers that fail; 0 disables injection.
    int fail_save_wifi_call = 0;
    int fail_clear_wifi_call = 0;
    int fail_pending_call = 0;

    std::vector<std::string> log;
    int save_wifi_calls = 0;
    int clear_wifi_calls = 0;
    int pending_calls = 0;
    int url_calls = 0;

    SetupQrServerTrust ServerTrust() override { return trust; }

    bool LoadWifi(std::string& out_ssid, std::string& out_password) override {
        out_ssid = ssid;
        out_password = password;
        return !ssid.empty();
    }

    bool SaveWifi(const std::string& new_ssid, const std::string& new_password) override {
        log.push_back("wifi:" + new_ssid);
        if (++save_wifi_calls == fail_save_wifi_call) return false;
        ssid = new_ssid;
        password = new_password;
        return true;
    }

    bool ClearWifi() override {
        log.push_back("clear");
        if (++clear_wifi_calls == fail_clear_wifi_call) return false;
        ssid.clear();
        password.clear();
        return true;
    }

    bool SetTransactionPending(bool value) override {
        log.push_back(value ? "pending:1" : "pending:0");
        if (++pending_calls == fail_pending_call) return false;
        pending = value;
        return true;
    }

    ProvisioningUrlSaveResult SaveBootstrapUrl(const std::string& new_url) override {
        log.push_back("url");
        ++url_calls;
        if (url_result == ProvisioningUrlSaveResult::kSaved) url = new_url;
        return url_result;
    }
};

SetupQrPayload RodakPayload() {
    SetupQrPayload payload;
    payload.kind = SetupQrKind::kRodak;
    payload.ssid = "HomeNet";
    payload.password = "s3cret-pass";
    payload.bootstrap_url = "https://ic.example.com/api/v1/aiot/devices/bootstrap";
    return payload;
}

SetupQrPayload WifiPayload() {
    SetupQrPayload payload;
    payload.kind = SetupQrKind::kWifiOnly;
    payload.ssid = "Guest";
    payload.open_network = true;
    return payload;
}

FakeStore StoreWithWifi() {
    FakeStore store;
    store.ssid = "OldNet";
    store.password = "old-password";
    return store;
}

std::string Code(SetupQrApplyStatus status) { return SetupQrApplyStatusCode(status); }

}  // namespace

RODAK_TEST("Setup QR apply saves WiFi then URL inside the pending marker") {
    FakeStore store = StoreWithWifi();
    RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(RodakPayload(), store)),
                   std::string("qr_server_changed"));
    RODAK_CHECK_EQ(store.ssid, std::string("HomeNet"));
    RODAK_CHECK_EQ(store.password, std::string("s3cret-pass"));
    RODAK_CHECK_EQ(store.url, std::string("https://ic.example.com/api/v1/aiot/devices/bootstrap"));
    RODAK_CHECK_FALSE(store.pending);
    const std::vector<std::string> expected = {"pending:1", "wifi:HomeNet", "url", "pending:0"};
    RODAK_CHECK(store.log == expected);
}

RODAK_TEST("Setup QR apply reports an unchanged server and keeps credentials") {
    FakeStore store = StoreWithWifi();
    store.url_result = ProvisioningUrlSaveResult::kUnchanged;
    RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(RodakPayload(), store)),
                   std::string("qr_server_unchanged"));
    RODAK_CHECK_EQ(store.ssid, std::string("HomeNet"));
    RODAK_CHECK_FALSE(store.pending);
}

RODAK_TEST("Setup QR apply with a WiFi-only code never touches the URL") {
    FakeStore store = StoreWithWifi();
    RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(WifiPayload(), store)),
                   std::string("qr_wifi_saved"));
    RODAK_CHECK_EQ(store.ssid, std::string("Guest"));
    RODAK_CHECK(store.password.empty());
    RODAK_CHECK_EQ(store.url_calls, 0);
    RODAK_CHECK_FALSE(store.pending);
}

RODAK_TEST("Setup QR apply refuses a pinned or unreadable server before any write") {
    for (const auto trust : {SetupQrServerTrust::kPinned, SetupQrServerTrust::kUnreadable}) {
        FakeStore store = StoreWithWifi();
        store.trust = trust;
        RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(RodakPayload(), store)),
                       std::string("qr_server_pinned"));
        RODAK_CHECK(store.log.empty());
        RODAK_CHECK_EQ(store.ssid, std::string("OldNet"));
    }
}

RODAK_TEST("Setup QR apply lets a WiFi-only code through on a pinned server") {
    FakeStore store = StoreWithWifi();
    store.trust = SetupQrServerTrust::kPinned;
    RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(WifiPayload(), store)),
                   std::string("qr_wifi_saved"));
    RODAK_CHECK_EQ(store.ssid, std::string("Guest"));
    RODAK_CHECK_EQ(store.url_calls, 0);
}

RODAK_TEST("Setup QR apply makes no change when the pending marker cannot be set") {
    FakeStore store = StoreWithWifi();
    store.fail_pending_call = 1;
    RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(RodakPayload(), store)),
                   std::string("qr_storage_failed"));
    RODAK_CHECK_EQ(store.ssid, std::string("OldNet"));
    RODAK_CHECK_EQ(store.save_wifi_calls, 0);
    RODAK_CHECK_EQ(store.url_calls, 0);
}

RODAK_TEST("Setup QR apply restores WiFi and clears the marker after a WiFi write failure") {
    FakeStore store = StoreWithWifi();
    store.fail_save_wifi_call = 1;
    RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(RodakPayload(), store)),
                   std::string("qr_storage_failed"));
    RODAK_CHECK_EQ(store.ssid, std::string("OldNet"));
    RODAK_CHECK_EQ(store.password, std::string("old-password"));
    RODAK_CHECK_FALSE(store.pending);
    RODAK_CHECK_EQ(store.url_calls, 0);
}

RODAK_TEST("Setup QR apply restores WiFi after a rolled-back URL failure") {
    FakeStore store = StoreWithWifi();
    store.url_result = ProvisioningUrlSaveResult::kFailedRolledBack;
    RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(RodakPayload(), store)),
                   std::string("qr_storage_failed"));
    RODAK_CHECK_EQ(store.ssid, std::string("OldNet"));
    RODAK_CHECK_EQ(store.password, std::string("old-password"));
    RODAK_CHECK_EQ(store.url, std::string("https://old.example.com/api/v1/aiot/devices/bootstrap"));
    RODAK_CHECK_FALSE(store.pending);
}

RODAK_TEST("Setup QR apply clears WiFi after a URL failure when none was saved before") {
    FakeStore store;
    store.url_result = ProvisioningUrlSaveResult::kFailedRolledBack;
    RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(RodakPayload(), store)),
                   std::string("qr_storage_failed"));
    RODAK_CHECK(store.ssid.empty());
    RODAK_CHECK_EQ(store.clear_wifi_calls, 1);
    RODAK_CHECK_FALSE(store.pending);
}

RODAK_TEST("Setup QR apply keeps the marker when cloud state is uncertain") {
    FakeStore store = StoreWithWifi();
    store.url_result = ProvisioningUrlSaveResult::kStateUncertain;
    RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(RodakPayload(), store)),
                   std::string("qr_state_uncertain"));
    RODAK_CHECK(store.pending);
    RODAK_CHECK_EQ(store.ssid, std::string("OldNet"));
}

RODAK_TEST("Setup QR apply keeps the marker when the WiFi rollback fails") {
    FakeStore store = StoreWithWifi();
    store.url_result = ProvisioningUrlSaveResult::kFailedRolledBack;
    store.fail_save_wifi_call = 2;
    RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(RodakPayload(), store)),
                   std::string("qr_state_uncertain"));
    RODAK_CHECK(store.pending);

    FakeStore first_write = StoreWithWifi();
    first_write.fail_save_wifi_call = 1;
    first_write.fail_clear_wifi_call = 1;
    first_write.ssid.clear();
    RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(RodakPayload(), first_write)),
                   std::string("qr_state_uncertain"));
    RODAK_CHECK(first_write.pending);
}

RODAK_TEST("Setup QR apply reports uncertain state when the final marker clear fails") {
    FakeStore store = StoreWithWifi();
    store.fail_pending_call = 2;
    RODAK_CHECK_EQ(Code(ApplySetupQrProvisioning(RodakPayload(), store)),
                   std::string("qr_state_uncertain"));
    // Both values were saved; boot recovery will clear them, which is the conservative outcome.
    RODAK_CHECK_EQ(store.ssid, std::string("HomeNet"));
    RODAK_CHECK(store.pending);
}

RODAK_TEST("Setup QR apply success helper matches the status set") {
    RODAK_CHECK(rodakos::IsSetupQrApplySuccess(SetupQrApplyStatus::kWifiSaved));
    RODAK_CHECK(rodakos::IsSetupQrApplySuccess(SetupQrApplyStatus::kServerUnchanged));
    RODAK_CHECK(rodakos::IsSetupQrApplySuccess(SetupQrApplyStatus::kServerChanged));
    RODAK_CHECK_FALSE(rodakos::IsSetupQrApplySuccess(SetupQrApplyStatus::kServerPinned));
    RODAK_CHECK_FALSE(rodakos::IsSetupQrApplySuccess(SetupQrApplyStatus::kStorageFailed));
    RODAK_CHECK_FALSE(rodakos::IsSetupQrApplySuccess(SetupQrApplyStatus::kStateUncertain));
}
