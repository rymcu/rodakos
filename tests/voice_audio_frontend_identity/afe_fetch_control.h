#pragma once

#include "esp_afe_sr_iface.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Host-only controlled SDK boundary; production lifecycle and PCM delivery remain real.
namespace rodakos_test::afe_fetch {

struct FetchReply {
    esp_err_t status = ESP_FAIL;
    std::vector<int16_t> samples;
    int vad_state = VAD_SILENCE;
};

struct FetchSummary {
    bool observed = false;
    unsigned generation = 0;
    unsigned current_failures = 0;
    unsigned cancelled_results = 0;
};

// BeginScript resets only this controller, after all previous AFE workers have stopped.
// Default mode remains immediate nullptr for the existing tests.
void BeginScript();
void ResetToDefault();
size_t QueueFetch(FetchReply reply);
bool WaitFetchEntered(size_t ticket);
void ReleaseFetch(size_t ticket);
void BlockFeedUntilFetch(size_t feed_call, size_t fetch_call);
bool WaitFeedBlocked(size_t feed_call);
void ForceReleaseAll();
size_t RejectedWarningCount();
FetchSummary LastSummary();
size_t DestroyDuringOperationCount();
std::vector<int16_t> FedMicrophoneSamples();

// Integration hooks: hold the real production lease while these fake operations block.
void OnFeed(const int16_t* samples);
afe_fetch_result_t* OnFetch(TickType_t timeout);
void OnDestroy();
void CaptureLog(char level, const char* tag, const char* format, ...);

}  // namespace rodakos_test::afe_fetch
