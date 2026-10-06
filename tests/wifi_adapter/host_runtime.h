#pragma once
#include "fakes/host_sdk.h"
#include <atomic>
#include <string>

namespace wifi_host {
void Reset();
void Drain();
void Advance(uint32_t milliseconds, bool drain = true);
void Connected(const std::string& ssid);
void StaleConnected(const std::string& ssid);
void GotIP(uint32_t ip = 0xc0000201);
void StaleGotIP(uint32_t ip);
void LostIP();
void StaleLostIP();
void ForeignLostIP();
void MissingLostIP();
void Disconnected(const std::string& ssid, bool still_associated = false);
void SetConnectedDriver(const std::string& ssid);
void SetConnectResult(esp_err_t error);
void SetDisconnectResult(esp_err_t error);
void SetAPInfoResult(esp_err_t error);
void SetIPInfoResult(esp_err_t error);
void SetEventPostFailure(bool failure);
void BeforeConnect(std::function<void()> hook);
void BeforeTimer(std::function<void()> hook);
int ConnectCalls();
int DisconnectCalls();
int DriverOverlaps();
int IPInfoCalls();
int LiveTimers();
bool TimerDeletionRequested();
std::string ConfiguredSSID();
}
