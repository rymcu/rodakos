#pragma once
#include "esp_peer.h"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace rodakos_test::display_host {
struct SentFrame {
    esp_peer_handle_t peer;
    uint16_t stream_id;
    esp_peer_data_channel_type_t type;
    std::string payload;
};

void Reset();
void JoinTasks();
esp_peer_handle_t LatestPeer();
void OpenControlChannel(esp_peer_handle_t peer, uint16_t stream_id);
void Receive(esp_peer_handle_t peer, uint16_t stream_id, const std::string& payload);
void EmitState(esp_peer_handle_t peer, esp_peer_state_t state);
void CloseControlChannel(esp_peer_handle_t peer, uint16_t stream_id);
std::vector<SentFrame> SentFrames();
bool IsClosed(esp_peer_handle_t peer);
bool CloseOverlappedSend();
void BlockNextSend();
bool WaitForBlockedSend();
void ReleaseSend();
void NotifyCleanupEntry();
bool WaitForCleanupEntry();
void SetSendResult(int result);
void SetOpenResult(int result);
void SetDefaultImplAvailable(bool available);
void SetConnectionResult(int result);
void SetTaskCreationAllowed(bool allowed);
void AdvanceTimeUs(int64_t delta);
void RunPeerTasks();
bool WaitForMainLoops(size_t minimum);
}
