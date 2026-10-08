#include "host_runtime.h"
#include "afe_fetch_control.h"

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "freertos/task.h"
#include "task_retirement_host.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
std::mutex state_mutex;
size_t supplied_audio_reads = 0;
size_t afe_feed_count = 0;


}

namespace rodakos_test::voice_frontend {
void Reset() {
    retirement_host::Reset();
    retirement_host::SetDynamicTasksAllowed(true);
    ResetAllocationObserver();
    ResetMultiNet();
    afe_fetch::ResetToDefault();
    std::lock_guard<std::mutex> lock(state_mutex);
    supplied_audio_reads = afe_feed_count = 0;
}

void SupplyAudioReads(size_t count) {
    std::lock_guard<std::mutex> lock(state_mutex);
    supplied_audio_reads = count;
}
size_t AfeFeedCount() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return afe_feed_count;
}
}

namespace rodakos {
AudioCodecInput::AudioCodecInput() = default;
AudioCodecInput::~AudioCodecInput() = default;
bool AudioCodecInput::Init() { return true; }
void AudioCodecInput::Deinit() {}
bool AudioCodecInput::Open(uint32_t, uint16_t, uint16_t, int, uint16_t) { return true; }
void AudioCodecInput::Close() {}
bool AudioCodecInput::Read(void*, int) { return false; }
bool AudioCodecInput::SetGain(int) { return true; }
bool AudioCodecInput::OpenForOwner(const char*, int, uint32_t, uint16_t, uint16_t, int,
                                   uint16_t, InputGainProfile) { return true; }
void AudioCodecInput::CloseForOwner(const char*) {}
bool AudioCodecInput::ReadForOwner(const char*, void* output, int bytes) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (supplied_audio_reads == 0) return false;
    --supplied_audio_reads;
    std::fill_n(static_cast<int16_t*>(output), bytes / sizeof(int16_t), int16_t{250});
    return true;
}
bool AudioCodecInput::IsOpen() const { return false; }
}

extern "C" const uint8_t rodakos_voice_models_start[]
    asm("_binary_rodakos_voice_models_start") = {1};

afe_config_t* afe_config_init(const char*, void*, int, int) { return new afe_config_t; }
void afe_config_free(afe_config_t* config) { delete config; }
const esp_afe_sr_iface_t* esp_afe_handle_from_config(afe_config_t*) {
    static esp_afe_sr_iface_t afe{
        [](afe_config_t*) { return new esp_afe_sr_data_t; },
        [](esp_afe_sr_data_t*) { return 320; },
        [](esp_afe_sr_data_t*) { return 2; },
        [](esp_afe_sr_data_t* data) {
            rodakos_test::afe_fetch::OnDestroy();
            delete data;
        },
        [](esp_afe_sr_data_t*, int16_t* buffer) {
            rodakos_test::voice_frontend::ObserveAfeFeedBuffer(buffer);
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                ++afe_feed_count;
            }
            rodakos_test::afe_fetch::OnFeed(buffer);
        },
        [](esp_afe_sr_data_t*, TickType_t timeout) -> afe_fetch_result_t* {
            return rodakos_test::afe_fetch::OnFetch(timeout);
        }};
    return &afe;
}
