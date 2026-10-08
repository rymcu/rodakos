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

struct FlowStats {
    size_t feeds = 0;
    size_t returns = 0;
    size_t fetches = 0;
    size_t partial_lost = 0;
    size_t valid_samples = 0;
    size_t queued_samples = 0;
    size_t resets = 0;
    size_t vad_resets = 0;
    size_t reset_during_operation = 0;
    size_t stalls = 0;
    size_t feed_errors = 0;
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
void BeginStreaming(bool block_first = false);
void SetFeedResult(int result);
void SetResetResult(int result);
void QueueStreamingFailure(size_t consumed_samples);
void ReleaseFirstFeed();
bool WaitFeedReturns(size_t count);
bool WaitFlowResets(size_t count);
bool WaitFlowSamples(size_t count);
bool WaitFlowStalls(size_t count);
FlowStats FlowSnapshot();
int FeedChunkSamples();
int FetchChunkSamples();
int FetchChannels();
int SampleRate();
void SetFormat(int feed_samples, int fetch_samples, int fetch_channels, int sample_rate);
int ResetBuffer();
int ResetVad();
size_t RejectedWarningCount();
FetchSummary LastSummary();
size_t DestroyDuringOperationCount();
std::vector<int16_t> FedMicrophoneSamples();

// Integration hooks: hold the real production lease while these fake operations block.
int OnFeed(const int16_t* samples);
afe_fetch_result_t* OnFetch(TickType_t timeout);
void OnDestroy();
void CaptureLog(char level, const char* tag, const char* format, ...);

}  // namespace rodakos_test::afe_fetch
