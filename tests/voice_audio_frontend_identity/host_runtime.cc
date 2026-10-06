#include "host_runtime.h"

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "freertos/task.h"

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

struct HostTask {
    std::thread thread;
    bool suspended = false;
};
thread_local HostTask* current_task = nullptr;
thread_local char external_task;
std::vector<HostTask*> task_registry;

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
void CleanupCompletedTasks() {
    std::lock_guard<std::mutex> lock(state_mutex);
    auto it = task_registry.begin();
    while (it != task_registry.end()) {
        HostTask* task = *it;
        if (task->suspended) {
            if (task->thread.joinable()) task->thread.join();
            delete task;
            it = task_registry.erase(it);
        } else {
            ++it;
        }
    }
}
void Reset() {
    CleanupCompletedTasks();
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
}
struct CleanupAtExit {
    ~CleanupAtExit() {
        CleanupCompletedTasks();
        std::lock_guard<std::mutex> lock(state_mutex);
        for (HostTask* task : task_registry) {
            if (task->thread.joinable()) task->thread.join();
            delete task;
        }
        task_registry.clear();
    }
};
CleanupAtExit cleanup_at_exit;
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
bool AudioCodecInput::ReadForOwner(const char*, void*, int) { return false; }
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
        [](esp_afe_sr_data_t*, int16_t*) {},
        [](esp_afe_sr_data_t*, TickType_t) -> afe_fetch_result_t* { return nullptr; }};
    return &afe;
}
TaskHandle_t xTaskGetCurrentTaskHandle() {
    return current_task != nullptr ? static_cast<TaskHandle_t>(current_task)
                                   : static_cast<TaskHandle_t>(&external_task);
}
BaseType_t StartTask(TaskFunction_t entry, void* argument, TaskHandle_t* output) {
    auto* task = new HostTask;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        task_registry.push_back(task);
    }
    *output = task;
    task->thread = std::thread([task, entry, argument] {
        current_task = task;
        entry(argument);
        task->suspended = true;
        current_task = nullptr;
    });
    return pdPASS;
}
BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t* output) {
    auto* task = new HostTask;
    task->suspended = true;
    *output = task;
    return pdPASS;
}
BaseType_t xTaskCreateWithCaps(TaskFunction_t entry, const char*, uint32_t, void* argument,
                               UBaseType_t, TaskHandle_t* output, uint32_t) {
    return StartTask(entry, argument, output);
}
BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t entry, const char*, uint32_t,
                                           void* argument, UBaseType_t, TaskHandle_t* output,
                                           BaseType_t, uint32_t) {
    return StartTask(entry, argument, output);
}
void vTaskDelay(TickType_t) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
void vTaskDelete(TaskHandle_t handle) {
    auto* task = static_cast<HostTask*>(handle);
    if (task == nullptr || task == current_task) return;
    if (task->thread.joinable()) task->thread.join();
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        auto it = std::find(task_registry.begin(), task_registry.end(), task);
        if (it != task_registry.end()) task_registry.erase(it);
    }
    delete task;
}
void vTaskDeleteWithCaps(TaskHandle_t handle) { vTaskDelete(handle); }
void vTaskSuspend(TaskHandle_t) {
    if (current_task != nullptr) current_task->suspended = true;
}
eTaskState eTaskGetState(TaskHandle_t handle) {
    auto* task = static_cast<HostTask*>(handle);
    return task == nullptr || task->suspended ? eSuspended : eRunning;
}
void xTaskNotifyGive(TaskHandle_t) {}
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) { return 1; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 8192; }
