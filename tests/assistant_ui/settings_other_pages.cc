// The Settings lifecycle and Device Cloud page are production code. Unrelated
// pages stay absent so this target does not pretend to validate their hardware.
#include "apps/settings/settings_app.h"
void SettingsApp::CreateMainPage() {}
void SettingsApp::CreateWiFiListPage() {}
void SettingsApp::CreateWiFiDetailPage() {}
void SettingsApp::UpdateWiFiDetailPage() {}
void SettingsApp::StartWiFiScan() {}
void SettingsApp::CreateDateTimePage() {}
void SettingsApp::CreateSystemShellPage() {}
void SettingsApp::CreateButtonBindingsPage() {}
void SettingsApp::UpdateButtonBindingsPage() {}
void SettingsApp::CloseButtonActionDialog() {}
void SettingsApp::CloseUsbDiskDialog() {}
void SettingsApp::CloseNtpServerDialog() {}
void SettingsWebFilesPage::Create(lv_obj_t*, PhoneAppContext&, PhoneUi&) {}
void SettingsWebFilesPage::Hide() {}
void SettingsWebFilesPage::Show() {}
void SettingsWebFilesPage::Update() {}
void SettingsWebFilesPage::Reset() {}
// 首次接入页与服务属于 lan_onboarding；Device Cloud 页只读 Available() 决定是否显示入口。
void SettingsApp::ShowLanOnboarding() {}
#include "phone_os/lan_onboarding_service.h"
bool rodakos::LanOnboardingService::Available() { return false; }
// 扫码配网页与服务需要相机，不在本目标内；Device Cloud 页同样只读 Available()。
void SettingsApp::ShowSetupQr() {}
void SettingsApp::CloseSetupQr() {}
void SettingsApp::TeardownSetupQr() {}
#include "phone_os/setup_qr_scan_service.h"
bool rodakos::SetupQrScanService::Available() const { return false; }
