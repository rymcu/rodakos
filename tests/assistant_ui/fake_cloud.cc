#include "fake_cloud.h"

namespace rodakos {
bool DeviceCloudConfigService::Load(DeviceCloudConfig& result) { result = cloud_ui_test::config; return true; }
bool DeviceCloudConfigService::Refresh(DeviceCloudConfig& result) {
    ++cloud_ui_test::refreshes;
    if (cloud_ui_test::on_refresh) cloud_ui_test::on_refresh();
    result = cloud_ui_test::config;
    return cloud_ui_test::refresh_ok;
}
bool DeviceCloudConfigService::Unbind(DeviceCloudConfig& result) {
    ++cloud_ui_test::unbinds; result = cloud_ui_test::config; return false;
}
CloudDiagnosticState DeviceCloudConfigService::diagnostic_state() const { return cloud_ui_test::state; }
CloudDiagnosticCode DeviceCloudConfigService::diagnostic() const { return cloud_ui_test::state.code; }
std::string DeviceCloudConfigService::last_error() const { return "raw-secret-token-response"; }
std::string DeviceCloudConfigService::GetClientId() { return "test-device"; }
const char* DeviceCloudConfigService::DefaultProvisioningUrl() { return "http://rodak.local/bootstrap"; }
ProvisioningUrlSaveResult DeviceCloudConfigService::SaveProvisioningUrl(const std::string& url,
                                                                       ProvisioningUrlSaveMode) {
    cloud_ui_test::config.provisioning_url = url; return ProvisioningUrlSaveResult::kSaved;
}
}
