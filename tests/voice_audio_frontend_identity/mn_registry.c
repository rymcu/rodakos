// 直接编译固定 SDK 的完整命令表实现，静态状态仅通过只读观察函数暴露。
#include RODAK_ESP_SR_COMMANDS_SOURCE

int rodak_mn_registry_present(void) { return esp_mn_root != NULL; }
int rodak_mn_registry_matches(const esp_mn_iface_t* iface, model_iface_data_t* data) {
    return esp_mn_root != NULL && esp_mn_model_handle == iface && esp_mn_model_data == data;
}
