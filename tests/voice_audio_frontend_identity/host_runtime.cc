#include "host_runtime.h"

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
bool command_update_succeeds = true;
bool command_clear_succeeds = true;
bool command_add_succeeds = true;
bool model_create_succeeds = true;
bool detection_enabled = false;
bool last_detection_ran = false;
size_t model_create_count = 0;
size_t model_destroy_count = 0;
std::string registered_command;

size_t supplied_audio_reads = 0;
size_t afe_feed_count = 0;

model_iface_data_t model_data;
esp_mn_results_t results{};

esp_mn_state_t Detect(model_iface_data_t*, int16_t*) {
    std::lock_guard<std::mutex> lock(state_mutex);
    last_detection_ran = true;
    return detection_enabled ? ESP_MN_STATE_DETECTED : ESP_MN_STATE_DETECTING;
}
model_iface_data_t* Create(const char*, int) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (!model_create_succeeds) return nullptr;
    ++model_create_count;
    return &model_data;
}
void Destroy(model_iface_data_t*) {
    std::lock_guard<std::mutex> lock(state_mutex);
    ++model_destroy_count;
}
void SetThreshold(model_iface_data_t*, float) {}
int GetChunk(model_iface_data_t*) { return 320; }
esp_mn_results_t* GetResults(model_iface_data_t*) { return &results; }
void Clean(model_iface_data_t*) {}
esp_mn_iface_t iface{&Create, &Destroy, &SetThreshold, &GetChunk, &Detect, &GetResults, &Clean};
}

namespace rodakos_test::voice_frontend {
void Reset() {
    retirement_host::Reset();
    retirement_host::SetDynamicTasksAllowed(true);
    ResetAllocationObserver();
    std::lock_guard<std::mutex> lock(state_mutex);
    command_update_succeeds = true;
    command_clear_succeeds = true;
    command_add_succeeds = true;
    model_create_succeeds = true;
    detection_enabled = false;
    last_detection_ran = false;
    model_create_count = 0;
    model_destroy_count = 0;
    registered_command.clear();
    results = {};
    supplied_audio_reads = afe_feed_count = 0;
}

void SetCommandUpdateResult(bool succeeds) {
    std::lock_guard<std::mutex> lock(state_mutex);
    command_update_succeeds = succeeds;
}
void SetCommandClearResult(bool succeeds) {
    std::lock_guard<std::mutex> lock(state_mutex);
    command_clear_succeeds = succeeds;
}
void SetCommandAddResult(bool succeeds) {
    std::lock_guard<std::mutex> lock(state_mutex);
    command_add_succeeds = succeeds;
}
void SetModelCreateResult(bool succeeds) {
    std::lock_guard<std::mutex> lock(state_mutex);
    model_create_succeeds = succeeds;
}
void SetDetection(bool detected) {
    std::lock_guard<std::mutex> lock(state_mutex);
    detection_enabled = detected;
    results.num = detected ? 1 : 0;
    results.command_id[0] = 1;
    results.string = "wake";
}
bool LastDetectionRan() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return last_detection_ran;
}
size_t ModelCreateCount() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return model_create_count;
}
size_t ModelDestroyCount() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return model_destroy_count;
}
const std::string& RegisteredCommand() { return registered_command; }
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

esp_err_t esp_mn_commands_alloc(esp_mn_iface_t*, model_iface_data_t*) { return ESP_OK; }
esp_err_t esp_mn_commands_clear() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return command_clear_succeeds ? ESP_OK : ESP_FAIL;
}
esp_err_t esp_mn_commands_add(int, const char* command) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (!command_add_succeeds) return ESP_FAIL;
    registered_command = command != nullptr ? command : "";
    return ESP_OK;
}
const char* esp_mn_commands_update() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return command_update_succeeds ? nullptr : "rejected";
}
void esp_mn_commands_free() {}

srmodel_list_t* srmodel_load(const void*) {
    static srmodel_list_t models{1};
    return &models;
}
char* esp_srmodel_filter(srmodel_list_t*, const char*, const char*) {
    static char name[] = "multinet";
    return name;
}
esp_mn_iface_t* esp_mn_handle_from_name(const char*) { return &iface; }
void esp_srmodel_deinit(srmodel_list_t*) {}

afe_config_t* afe_config_init(const char*, void*, int, int) { return new afe_config_t; }
void afe_config_free(afe_config_t* config) { delete config; }
const esp_afe_sr_iface_t* esp_afe_handle_from_config(afe_config_t*) {
    static esp_afe_sr_iface_t afe{
        [](afe_config_t*) { return new esp_afe_sr_data_t; },
        [](esp_afe_sr_data_t*) { return 320; },
        [](esp_afe_sr_data_t*) { return 2; },
        [](esp_afe_sr_data_t* data) { delete data; },
        [](esp_afe_sr_data_t*, int16_t* buffer) {
            rodakos_test::voice_frontend::ObserveAfeFeedBuffer(buffer);
            std::lock_guard<std::mutex> lock(state_mutex);
            ++afe_feed_count;
        },
        [](esp_afe_sr_data_t*, TickType_t) -> afe_fetch_result_t* { return nullptr; }};
    return &afe;
}
