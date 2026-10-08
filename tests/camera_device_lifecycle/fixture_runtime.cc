#include "esp_board_device.h"
#include "esp_board_manager.h"
#include "esp_board_entry.h"
#include "esp_board_find_utils.h"
#include "dev_camera.h"
#include <cstring>

extern "C" {
esp_err_t periph_i2c_get_effective_addr_internal(const char*, unsigned short*) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t periph_i2c_set_effective_addr_internal(const char*, const char*, unsigned short) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t periph_i2c_clear_effective_addr_internal(const char*) { return ESP_ERR_NOT_SUPPORTED; }
static dev_camera_handle_t camera_handle{"/dev/fake-camera", nullptr};
static int subtype_deinit_result = ESP_FAIL;
static int subtype_deinit_calls = 0;
static bool force_get_handle_failure = false;
static bool invalid_camera_path = false;
static bool missing_subtype_deinit = false;
static const char camera_sub_type[] = "dvp";
static const dev_camera_config_t camera_config{"camera", "camera", camera_sub_type, {nullptr, 0}};

static esp_err_t camera_sub_init(void*, int, void** handle) { *handle = &camera_handle; return ESP_OK; }
static esp_err_t camera_sub_deinit(void*) { ++subtype_deinit_calls; return subtype_deinit_result; }

const esp_board_entry_desc_t* esp_board_entry_find_subtype_desc(const char*, const char*) {
    static const esp_board_entry_desc_t entry{"camera_dvp", camera_sub_init, camera_sub_deinit};
    static const esp_board_entry_desc_t missing{"camera_dvp", camera_sub_init, nullptr};
    return missing_subtype_deinit ? &missing : &entry;
}

const esp_board_device_desc_t g_esp_board_devices[] = {
    {nullptr, "camera", "gc0308", "camera", "dvp", &camera_config, (uint16_t)sizeof(camera_config), 0, nullptr, nullptr, 0},
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, nullptr, nullptr, 0}
};
esp_board_device_handle_t g_esp_board_device_handles[] = {
    {nullptr, "camera", "gc0308", "camera", nullptr, 0, dev_camera_init, dev_camera_deinit},
    {nullptr, nullptr, nullptr, nullptr, nullptr, 0, nullptr, nullptr}
};

esp_err_t esp_board_periph_init_all(void) { return ESP_OK; }
esp_err_t esp_board_periph_deinit_all(void) { return ESP_OK; }
esp_err_t esp_board_periph_get_handle(const char*, void**) { return ESP_ERR_NOT_FOUND; }
esp_err_t esp_board_periph_get_config(const char*, void**) { return ESP_ERR_NOT_FOUND; }

esp_err_t esp_board_manager_init_device_by_name(const char* name) { return esp_board_device_init(name); }
esp_err_t esp_board_manager_deinit_device_by_name(const char* name) { return esp_board_device_deinit(name); }
esp_err_t esp_board_manager_get_device_handle(const char* name, void** handle) {
    if (force_get_handle_failure) return ESP_ERR_NOT_FOUND;
    const auto ret = esp_board_device_get_handle(name, handle);
    if (ret == ESP_OK) {
        static_cast<dev_camera_handle_t*>(*handle)->dev_path =
            invalid_camera_path ? nullptr : "/dev/fake-camera";
    }
    return ret;
}
bool esp_board_manager_check_name(const char* name) { return name && std::strcmp(name, "camera") == 0; }

void fixture_reset(void) {
    g_esp_board_device_handles[0].device_handle = nullptr;
    g_esp_board_device_handles[0].ref_count = 0;
    subtype_deinit_result = ESP_FAIL;
    subtype_deinit_calls = 0;
    force_get_handle_failure = false;
    camera_handle.dev_path = "/dev/fake-camera";
    invalid_camera_path = false;
    missing_subtype_deinit = false;
}
void fixture_set_subtype_result(int result) { subtype_deinit_result = result; }
void fixture_set_get_failure(bool value) { force_get_handle_failure = value; }
void fixture_set_invalid_path(bool value) { invalid_camera_path = value; }
void fixture_set_missing_callback(bool value) { missing_subtype_deinit = value; }
int fixture_subtype_deinit_calls(void) { return subtype_deinit_calls; }
bool fixture_has_board_handle(void) { return g_esp_board_device_handles[0].device_handle != nullptr; }
}
