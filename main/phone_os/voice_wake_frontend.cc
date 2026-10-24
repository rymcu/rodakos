#include "phone_os/voice_wake_frontend.h"

#include <algorithm>

#include <esp_afe_aec.h>
#include <esp_agc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "VoiceWakeFrontend";
constexpr int kSampleRate = 16000;
// esp_agc_process accepts exactly 10 ms per call.
constexpr size_t kAgcBlockSamples = kSampleRate / 100;
// The S3 header recommends filter length 4; it covers about 64 ms of echo path.
constexpr int kAecFilterLength = 4;
// AEC takes about 19% of one core (measured), so it runs only while the ES8311 loopback
// carries playback: mean square above 1024 (about -60 dBFS), plus 1 s for the echo tail.
// Without playback there is no echo to cancel and the microphone passes through.
constexpr uint64_t kReferenceActiveEnergy = 1024;
constexpr size_t kAecHangoverSamples = kSampleRate;
// Speech-recognition AGC: lift quiet distant speech toward -3 dBFS peaks with a limiter,
// without the nonlinear suppression of the voice-communication profile.
constexpr int kAgcCompressionGainDb = 9;
constexpr int kAgcTargetLevelDbfs = 3;

afe_aec_handle_t* Aec(void* handle) { return static_cast<afe_aec_handle_t*>(handle); }
}  // namespace

VoiceWakeFrontend::~VoiceWakeFrontend() { Stop(); }

bool VoiceWakeFrontend::Start(bool aec, bool agc) {
    Stop();
    if (aec) {
        // Channel 0 is the selected microphone, channel 1 the ES8311 loopback (MIC3).
        aec_ = afe_aec_create("MR", kAecFilterLength, AFE_TYPE_SR, AFE_MODE_LOW_COST);
        const int frame = aec_ != nullptr ? afe_aec_get_chunksize(Aec(aec_)) : 0;
        if (frame > 0) {
            aec_frame_ = static_cast<size_t>(frame);
            aec_output_ = static_cast<int16_t*>(heap_caps_aligned_calloc(
                16, aec_frame_, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        }
        if (aec_output_ == nullptr) {
            ESP_LOGW(TAG, "Wake AEC unavailable; continuing without echo cancellation");
            if (aec_ != nullptr) afe_aec_destroy(Aec(aec_));
            aec_ = nullptr;
            aec_frame_ = 0;
        }
    }
    if (agc) {
        agc_ = esp_agc_open(AGC_MODE_2, kSampleRate);
        if (agc_ != nullptr) {
            set_agc_config(agc_, kAgcCompressionGainDb, 1, kAgcTargetLevelDbfs);
        } else {
            ESP_LOGW(TAG, "Wake AGC unavailable; continuing without gain control");
        }
    }
    stats_ = {};
    ESP_LOGI(TAG, "Wake frontend started: aec=%d frame=%u agc=%d", aec_ != nullptr ? 1 : 0,
             static_cast<unsigned>(aec_frame_), agc_ != nullptr ? 1 : 0);
    return IsActive();
}

void VoiceWakeFrontend::Stop() {
    if (aec_ != nullptr) afe_aec_destroy(Aec(aec_));
    aec_ = nullptr;
    aec_frame_ = 0;
    heap_caps_free(aec_output_);
    aec_output_ = nullptr;
    if (agc_ != nullptr) esp_agc_close(agc_);
    agc_ = nullptr;
    Reset();
}

void VoiceWakeFrontend::Reset() {
    pending_.clear();
    agc_pending_.clear();
    aec_hangover_samples_ = 0;
    stats_ = {};
}

void VoiceWakeFrontend::Process(const int16_t* microphone, size_t microphone_stride,
                                const int16_t* reference, size_t reference_stride, size_t count,
                                std::vector<int16_t>& output) {
    const int64_t started_us = esp_timer_get_time();
    stats_.processed_samples += count;
    output.clear();
    if (aec_ == nullptr) {
        agc_pending_.reserve(agc_pending_.size() + count);
        for (size_t i = 0; i < count; ++i) agc_pending_.push_back(microphone[i * microphone_stride]);
    } else {
        pending_.reserve(pending_.size() + count * 2);
        for (size_t i = 0; i < count; ++i) {
            pending_.push_back(microphone[i * microphone_stride]);
            pending_.push_back(reference[i * reference_stride]);
        }
        size_t consumed = 0;
        while (pending_.size() - consumed >= aec_frame_ * 2) {
            uint64_t reference_energy = 0;
            for (size_t i = 0; i < aec_frame_; ++i) {
                const int32_t reference = pending_[consumed + i * 2 + 1];
                reference_energy += static_cast<uint64_t>(reference * reference);
            }
            stats_.reference_energy += reference_energy;
            if (reference_energy / aec_frame_ >= kReferenceActiveEnergy) {
                aec_hangover_samples_ = kAecHangoverSamples;
            } else {
                aec_hangover_samples_ -= std::min(aec_hangover_samples_, aec_frame_);
            }
            if (aec_hangover_samples_ == 0) {
                ++stats_.bypassed_frames;
                for (size_t i = 0; i < aec_frame_; ++i) {
                    agc_pending_.push_back(pending_[consumed + i * 2]);
                }
                consumed += aec_frame_ * 2;
                continue;
            }
            const size_t written =
                afe_aec_process(Aec(aec_), pending_.data() + consumed, aec_output_);
            consumed += aec_frame_ * 2;
            if (written != aec_frame_ * sizeof(int16_t)) {
                // Keep the timeline: a failed frame passes the raw microphone through.
                ++stats_.failures;
                for (size_t i = 0; i < aec_frame_; ++i) {
                    agc_pending_.push_back(pending_[consumed - aec_frame_ * 2 + i * 2]);
                }
                continue;
            }
            ++stats_.aec_frames;
            for (size_t i = 0; i < aec_frame_; ++i) {
                const int32_t in = pending_[consumed - aec_frame_ * 2 + i * 2];
                stats_.aec_in_energy += static_cast<uint64_t>(in * in);
                stats_.aec_out_energy += static_cast<uint64_t>(aec_output_[i] * aec_output_[i]);
            }
            agc_pending_.insert(agc_pending_.end(), aec_output_, aec_output_ + aec_frame_);
        }
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(consumed));
    }
    if (agc_ == nullptr) {
        output.swap(agc_pending_);
        agc_pending_.clear();
    } else {
        const size_t samples = agc_pending_.size() / kAgcBlockSamples * kAgcBlockSamples;
        ApplyGain(agc_pending_.data(), samples);
        output.assign(agc_pending_.begin(),
                      agc_pending_.begin() + static_cast<std::ptrdiff_t>(samples));
        agc_pending_.erase(agc_pending_.begin(),
                           agc_pending_.begin() + static_cast<std::ptrdiff_t>(samples));
    }
    stats_.process_us += static_cast<uint64_t>(esp_timer_get_time() - started_us);
}

void VoiceWakeFrontend::ApplyGain(int16_t* samples, size_t count) {
    for (size_t offset = 0; offset + kAgcBlockSamples <= count; offset += kAgcBlockSamples) {
        if (esp_agc_process(agc_, samples + offset, samples + offset,
                            static_cast<int>(kAgcBlockSamples), kSampleRate) < 0) {
            ++stats_.failures;
            continue;
        }
        ++stats_.agc_blocks;
    }
}

}  // namespace rodakos
