#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using esp_err_t = int;
using nvs_handle_t = uint32_t;
inline constexpr int ESP_OK = 0;
inline constexpr int ESP_ERR_NVS_NOT_FOUND = 1;
inline constexpr int NVS_READONLY = 0;
inline constexpr int NVS_READWRITE = 1;
namespace fake_nvs {
inline std::map<std::string, std::vector<uint8_t>> blobs;
inline bool read_error = false;
inline bool commit_error = false;
inline bool tear_write = false;
inline int writes = 0;
inline void Reset() { blobs.clear(); read_error = commit_error = tear_write = false; writes = 0; }
}
inline int nvs_open_from_partition(const char*, const char*, int, nvs_handle_t* handle) { *handle = 1; return 0; }
inline void nvs_close(nvs_handle_t) {}
inline int nvs_get_blob(nvs_handle_t, const char* key, void* output, size_t* size) {
    if (fake_nvs::read_error) return 2;
    auto found = fake_nvs::blobs.find(key);
    if (found == fake_nvs::blobs.end()) return ESP_ERR_NVS_NOT_FOUND;
    if (output && *size < found->second.size()) return 2;
    if (output) std::memcpy(output, found->second.data(), found->second.size());
    *size = found->second.size(); return 0;
}
inline int nvs_set_blob(nvs_handle_t, const char* key, const void* value, size_t size) {
    ++fake_nvs::writes;
    const auto* bytes = static_cast<const uint8_t*>(value);
    fake_nvs::blobs[key] = {bytes, bytes + (fake_nvs::tear_write ? size / 2 : size)};
    return fake_nvs::tear_write ? 2 : 0;
}
inline int nvs_commit(nvs_handle_t) { return fake_nvs::commit_error ? 2 : 0; }
inline int nvs_open(const char*, int, nvs_handle_t* handle) { *handle = 1; return 0; }
inline int nvs_get_str(nvs_handle_t handle, const char* key, char* output, size_t* size) {
    return nvs_get_blob(handle, key, output, size);
}
inline int nvs_set_str(nvs_handle_t handle, const char* key, const char* text) {
    return nvs_set_blob(handle, key, text, std::strlen(text) + 1);
}
