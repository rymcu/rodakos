#include "host_runtime.h"

#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"

#include <cstring>
#include <mutex>

// 闭源声学模型不能在宿主执行。仅按 pinned mn5q8 的反汇编模拟 create/destroy
// 对命令表的调用；链表、关联模型、清理和命令更新入口使用完整 SDK C 源。
struct model_iface_data_t {};
extern "C" {
int rodak_mn_registry_present();
int rodak_mn_registry_matches(const esp_mn_iface_t*, model_iface_data_t*);
esp_err_t __real_esp_mn_commands_alloc(const esp_mn_iface_t*, model_iface_data_t*);
esp_err_t __real_esp_mn_commands_free();
esp_err_t __real_esp_mn_commands_clear();
}

namespace {
std::mutex mn_mutex;
bool command_update_succeeds = true;
bool command_clear_succeeds = true;
bool command_add_succeeds = true;
bool model_create_succeeds = true;
bool detection_enabled = false;
bool last_detection_ran = false;
bool model_alive = false;
int chunk_samples = 320;
size_t model_create_count = 0;
size_t model_destroy_count = 0;
rodakos_test::voice_frontend::CommandRegistryStats registry_stats{};
std::string selected_model = "mn5q8_cn";
std::string registered_command;
model_iface_data_t model_data;
esp_mn_results_t results{};
esp_mn_error_t command_error{};
esp_mn_iface_t iface;

model_iface_data_t* Create(const char*, int) {
    {
        std::lock_guard<std::mutex> lock(mn_mutex);
        if (!model_create_succeeds) return nullptr;
        ++model_create_count;
        model_alive = true;
    }
    // 与库正常返回路径相同：内部初始化 registry，调用者不再分配。
    esp_mn_commands_alloc(&iface, &model_data);
    return &model_data;
}
void Destroy(model_iface_data_t*) {
    esp_mn_commands_free();
    std::lock_guard<std::mutex> lock(mn_mutex);
    ++model_destroy_count;
    model_alive = false;
}
int SetThreshold(model_iface_data_t*, float) { return 0; }
int GetChunk(model_iface_data_t*) { return chunk_samples; }
esp_mn_state_t Detect(model_iface_data_t* data, int16_t*) {
    std::lock_guard<std::mutex> lock(mn_mutex);
    last_detection_ran = model_alive && rodak_mn_registry_matches(&iface, data);
    return detection_enabled && last_detection_ran ? ESP_MN_STATE_DETECTED : ESP_MN_STATE_DETECTING;
}
esp_mn_results_t* GetResults(model_iface_data_t*) { return &results; }
void Clean(model_iface_data_t*) {}
int CheckCommand(model_iface_data_t* data, const char*) {
    std::lock_guard<std::mutex> lock(mn_mutex);
    return command_add_succeeds && model_alive && rodak_mn_registry_matches(&iface, data);
}
esp_mn_error_t* SetCommands(model_iface_data_t* data, esp_mn_node_t* root) {
    std::lock_guard<std::mutex> lock(mn_mutex);
    command_error.num = command_update_succeeds && model_alive &&
        rodak_mn_registry_matches(&iface, data) ? 0 : 1;
    if (command_error.num == 0 && root->next != nullptr) {
        registered_command = root->next->phrase->string;
    }
    return &command_error;
}
}

extern "C" esp_err_t __wrap_esp_mn_commands_alloc(
    const esp_mn_iface_t* handle, model_iface_data_t* data) {
    std::lock_guard<std::mutex> lock(mn_mutex);
    ++registry_stats.allocations;
    if (rodak_mn_registry_present()) ++registry_stats.reallocations;
    return __real_esp_mn_commands_alloc(handle, data);
}
extern "C" esp_err_t __wrap_esp_mn_commands_free() {
    std::lock_guard<std::mutex> lock(mn_mutex);
    ++registry_stats.frees;
    if (!rodak_mn_registry_present()) ++registry_stats.free_without_registry;
    return __real_esp_mn_commands_free();
}
extern "C" esp_err_t __wrap_esp_mn_commands_clear() {
    std::lock_guard<std::mutex> lock(mn_mutex);
    return command_clear_succeeds ? __real_esp_mn_commands_clear() : ESP_FAIL;
}

namespace rodakos_test::voice_frontend {
void ResetMultiNet() {
    std::lock_guard<std::mutex> lock(mn_mutex);
    // 不替测试清理：上一个生产析构必须已释放真实 registry。
    if (rodak_mn_registry_present() || model_alive) std::abort();
    iface = {};
    iface.create = &Create;
    iface.destroy = &Destroy;
    iface.set_det_threshold = &SetThreshold;
    iface.get_samp_chunksize = &GetChunk;
    iface.detect = &Detect;
    iface.get_results = &GetResults;
    iface.clean = &Clean;
    iface.check_speech_command = &CheckCommand;
    iface.set_speech_commands = &SetCommands;
    command_update_succeeds = command_clear_succeeds = command_add_succeeds = true;
    model_create_succeeds = true;
    detection_enabled = last_detection_ran = false;
    model_create_count = model_destroy_count = 0;
    registry_stats = {};
    chunk_samples = 320;
    selected_model = "mn5q8_cn";
    registered_command.clear();
    results = {};
}
void SetCommandUpdateResult(bool value) { std::lock_guard<std::mutex> lock(mn_mutex); command_update_succeeds = value; }
void SetCommandClearResult(bool value) { std::lock_guard<std::mutex> lock(mn_mutex); command_clear_succeeds = value; }
void SetCommandAddResult(bool value) { std::lock_guard<std::mutex> lock(mn_mutex); command_add_succeeds = value; }
void SetModelCreateResult(bool value) { std::lock_guard<std::mutex> lock(mn_mutex); model_create_succeeds = value; }
void SetModelChunkSamples(int value) { chunk_samples = value; }
void SetSelectedModel(const char* value) { selected_model = value; }
void SetDetection(bool detected) {
    std::lock_guard<std::mutex> lock(mn_mutex);
    detection_enabled = detected;
    results.num = detected ? 1 : 0;
    results.command_id[0] = 1;
    std::strcpy(results.string, "wake");
}
bool LastDetectionRan() { std::lock_guard<std::mutex> lock(mn_mutex); return last_detection_ran; }
size_t ModelCreateCount() { std::lock_guard<std::mutex> lock(mn_mutex); return model_create_count; }
size_t ModelDestroyCount() { std::lock_guard<std::mutex> lock(mn_mutex); return model_destroy_count; }
CommandRegistryStats RegistryStats() { std::lock_guard<std::mutex> lock(mn_mutex); return registry_stats; }
bool RegistryMatchesModel() { return model_alive && rodak_mn_registry_matches(&iface, &model_data); }
bool RegistryPresent() { return rodak_mn_registry_present(); }
const std::string& RegisteredCommand() { return registered_command; }
}

srmodel_list_t* srmodel_load(const void*) {
    static srmodel_list_t models{1};
    return &models;
}
char* esp_srmodel_filter(srmodel_list_t*, const char*, const char*) { return selected_model.data(); }
esp_mn_iface_t* esp_mn_handle_from_name(const char*) { return &iface; }
void esp_srmodel_deinit(srmodel_list_t*) {}
