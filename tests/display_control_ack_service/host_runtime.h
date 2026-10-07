#pragma once
#include "esp_peer.h"
#include "esp_peer_default.h"
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace rodakos_test::display_host {
struct OpenAttempt {
    esp_peer_cfg_t config{};
    esp_peer_default_cfg_t defaults{};
    bool default_abi_valid = false;
};
struct SentSignal {
    esp_peer_handle_t peer;
    esp_peer_msg_type_t type;
    std::string payload;
    bool nul_terminated;
};
std::vector<OpenAttempt> OpenAttempts();
std::vector<SentSignal> SentSignals();
void SetSignalResult(int result);
void SetCloseStateCallback(bool enabled);
void SetLogCapture(bool enabled);
std::vector<std::string> CapturedLogs();
void SetHeapValues(const std::array<size_t, 6>& values);
void CaptureLog(const char* tag, const char* format, ...);
struct SentFrame {
    esp_peer_handle_t peer;
    uint16_t stream_id;
    esp_peer_data_channel_type_t type;
    std::string payload;
    int result = ESP_PEER_ERR_WRONG_STATE;
    bool returned = false;
};

void Reset();
void JoinTasks();
esp_peer_handle_t LatestPeer();
void OpenControlChannel(esp_peer_handle_t peer, uint16_t stream_id);
void OpenVideoChannel(esp_peer_handle_t peer, uint16_t stream_id);
void Receive(esp_peer_handle_t peer, uint16_t stream_id, const std::string& payload);
void EmitState(esp_peer_handle_t peer, esp_peer_state_t state);
void CloseControlChannel(esp_peer_handle_t peer, uint16_t stream_id);
std::vector<SentFrame> SentFrames();
bool WaitForSendReturns(size_t minimum);
bool WaitForVideoSendReturns(uint16_t stream_id, size_t minimum);
size_t MainLoopCount();
bool IsClosed(esp_peer_handle_t peer);
bool CloseOverlappedSend();
void BlockNextSend();
bool WaitForBlockedSend();
void ReleaseSend();
void NotifyCleanupEntry();
bool WaitForCleanupEntry();
void SetSendResult(int result);
void OnNextSendReturn(std::function<void()> callback);
void SetOpenResult(int result);
void SetDefaultImplAvailable(bool available);
void SetConnectionResult(int result);
void SetTaskCreationAllowed(bool allowed);
void AdvanceTimeUs(int64_t delta);
void RunPeerTasks();
bool WaitForMainLoops(size_t minimum);
size_t ClockReads();
bool WaitForClockReads(size_t minimum);
void SetMainLoopCostUs(int64_t value);
void SetSendCostUs(int64_t value);
}
