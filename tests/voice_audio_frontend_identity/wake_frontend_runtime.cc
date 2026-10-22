#include "wake_frontend_runtime.h"

#include "esp_afe_aec.h"
#include "esp_agc.h"

#include <cstring>
#include <mutex>
#include <string>

// The real AEC and AGC are closed DSP. These doubles keep their frame contracts: AEC takes
// interleaved [mic, ref] frames of its chunk size and writes the near-end frame; AGC takes
// exactly 10 ms. The AEC double subtracts the reference so tests can see which input it used.
namespace {
std::mutex runtime_mutex;
bool aec_creatable = true;
bool agc_creatable = true;
bool aec_fails = false;
int aec_frame = 256;
std::string aec_format;
int agc_gain_db = 0;
size_t aec_live = 0;
size_t agc_live = 0;
size_t aec_frames = 0;
size_t agc_blocks = 0;
int agc_marker = 0;
}  // namespace

afe_aec_handle_t* afe_aec_create(const char* input_format, int, int, int) {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    if (!aec_creatable) return nullptr;
    aec_format = input_format;
    ++aec_live;
    auto* handle = new afe_aec_handle_t;
    handle->frame_size = aec_frame;
    return handle;
}
size_t afe_aec_process(afe_aec_handle_t* handle, const int16_t* indata, int16_t* outdata) {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    if (aec_fails) return 0;
    for (int i = 0; i < handle->frame_size; ++i) {
        outdata[i] = static_cast<int16_t>(indata[i * 2] - indata[i * 2 + 1]);
    }
    ++aec_frames;
    return static_cast<size_t>(handle->frame_size) * sizeof(int16_t);
}
int afe_aec_get_chunksize(afe_aec_handle_t* handle) { return handle->frame_size; }
void afe_aec_destroy(afe_aec_handle_t* handle) {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    --aec_live;
    delete handle;
}

void* esp_agc_open(agc_mode_t, int sample_rate) {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    if (!agc_creatable || sample_rate != 16000) return nullptr;
    ++agc_live;
    return &agc_marker;
}
void set_agc_config(void*, int gain_db, int, int) {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    agc_gain_db = gain_db;
}
int esp_agc_process(void*, short* in_pcm, short* out_pcm, int frame_size, int sample_rate) {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    if (sample_rate != 16000) return -2;
    if (frame_size != 160) return -3;
    for (int i = 0; i < frame_size; ++i) out_pcm[i] = static_cast<short>(in_pcm[i] * 2);
    ++agc_blocks;
    return 0;
}
void esp_agc_close(void*) {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    --agc_live;
}

namespace rodakos_test::wake_frontend {
void Reset() {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    aec_creatable = agc_creatable = true;
    aec_fails = false;
    aec_frame = 256;
    aec_format.clear();
    agc_gain_db = 0;
    aec_frames = agc_blocks = 0;
}
void SetCreatable(bool aec, bool agc) {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    aec_creatable = aec;
    agc_creatable = agc;
}
void SetAecFails(bool fails) { std::lock_guard<std::mutex> lock(runtime_mutex); aec_fails = fails; }
void SetAecFrame(int samples) { std::lock_guard<std::mutex> lock(runtime_mutex); aec_frame = samples; }
std::string AecFormat() { std::lock_guard<std::mutex> lock(runtime_mutex); return aec_format; }
int AgcGainDb() { std::lock_guard<std::mutex> lock(runtime_mutex); return agc_gain_db; }
size_t LiveAec() { std::lock_guard<std::mutex> lock(runtime_mutex); return aec_live; }
size_t LiveAgc() { std::lock_guard<std::mutex> lock(runtime_mutex); return agc_live; }
size_t AecFrames() { std::lock_guard<std::mutex> lock(runtime_mutex); return aec_frames; }
size_t AgcBlocks() { std::lock_guard<std::mutex> lock(runtime_mutex); return agc_blocks; }
}  // namespace rodakos_test::wake_frontend
