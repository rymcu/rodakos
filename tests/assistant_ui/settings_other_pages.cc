// The Settings lifecycle and Device Cloud page are production code. Unrelated
// pages stay absent so this target does not pretend to validate their hardware.
#include "apps/settings/settings_app.h"
void SettingsApp::CreateMainPage() {}
void SettingsApp::CreateAppearancePage() {}
void SettingsApp::UpdateAppearancePage() {}
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
