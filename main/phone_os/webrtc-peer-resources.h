#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <esp_heap_caps.h>
#include <esp_log.h>

#include "phone_os/webrtc-peer-config.h"

namespace rodakos {

// Call under the owning service mutex. Samples describe shared heaps at these
// points, not peer-owned bytes, SDK admission, a continuous peak or boot minima.
class WebRtcPeerResources {
public:
    void Begin(const char* tag) {
        ++generation_;
        candidate_calls_ = 0;
        sampled_minima_.fill(std::numeric_limits<size_t>::max());
        Log(tag, "open-before");
    }

    void Log(const char* tag, const char* phase, int result = ESP_PEER_ERR_NONE) {
        const auto sample = Sample();
        LogSample(tag, phase, result, sample);
    }

    void Signal(const char* tag, esp_peer_msg_type_t type, int result) {
        if (type == ESP_PEER_MSG_TYPE_CANDIDATE && candidate_calls_ != UINT32_MAX) {
            ++candidate_calls_;
        }
        const auto sample = Sample();
        // Count calls, including duplicates and failures. The SDK has no public
        // per-candidate admission result; crossing 32 must not change ACKs.
        if (type == ESP_PEER_MSG_TYPE_SDP || candidate_calls_ == 1 ||
            candidate_calls_ == 10 || candidate_calls_ == 18 ||
            candidate_calls_ == 32 || candidate_calls_ == 33) {
            LogSample(tag, type == ESP_PEER_MSG_TYPE_SDP ? "sdp-return" : "candidate-return",
                      result, sample);
        }
    }

private:
    using SampleValues = std::array<size_t, 6>;

    SampleValues Sample() {
        const SampleValues sample{
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_8BIT),
            heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_8BIT),
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
            heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)};
        for (size_t i = 0; i < sample.size(); ++i) {
            sampled_minima_[i] = std::min(sampled_minima_[i], sample[i]);
        }
        return sample;
    }

    void LogSample(const char* tag, const char* phase, int result, const SampleValues& s) const {
        ESP_LOGI(tag, "peer_resources phase=%s generation=%u max_candidates=%u candidate_calls=%u result=%d "
                 "internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u psram_free=%u psram_largest=%u "
                 "sampled_min_internal_free=%u sampled_min_internal_largest=%u sampled_min_dma_free=%u "
                 "sampled_min_dma_largest=%u sampled_min_psram_free=%u sampled_min_psram_largest=%u",
                 phase, static_cast<unsigned>(generation_), static_cast<unsigned>(kWebRtcPeerMaxCandidates),
                 static_cast<unsigned>(candidate_calls_), result,
                 static_cast<unsigned>(s[0]), static_cast<unsigned>(s[1]),
                 static_cast<unsigned>(s[2]), static_cast<unsigned>(s[3]),
                 static_cast<unsigned>(s[4]), static_cast<unsigned>(s[5]),
                 static_cast<unsigned>(sampled_minima_[0]), static_cast<unsigned>(sampled_minima_[1]),
                 static_cast<unsigned>(sampled_minima_[2]), static_cast<unsigned>(sampled_minima_[3]),
                 static_cast<unsigned>(sampled_minima_[4]), static_cast<unsigned>(sampled_minima_[5]));
    }

    SampleValues sampled_minima_{};
    uint32_t generation_ = 0;
    uint32_t candidate_calls_ = 0;
};

}  // namespace rodakos
