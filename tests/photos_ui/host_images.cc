#include "host_images.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_common.h"
#include "phone_os/resource_failure_injection.h"
#include "phone_ui/jpg/jpeg_to_image.h"
#include <lvgl.h>
#include <cstring>
#include <unistd.h>

namespace photo_test {
void ResetFailures() {
    fail_timer = short_read = fail_close = fail_seek = fail_async = decode_partial_failure = false;
    decode_result = ESP_OK; fail_allocations = 0;
    short_read_after = -1;
    rodakos::ArmResourceFailure("clear");
}
TestFiles::TestFiles() {
    static unsigned sequence = 0;
    root = std::filesystem::temp_directory_path() /
        ("rodakos-photos-" + std::to_string(getpid()) + "-" + std::to_string(++sequence));
    std::filesystem::create_directory(root);
}
TestFiles::~TestFiles() { std::filesystem::remove_all(root); }
std::string TestFiles::Save(const std::string& name, const std::vector<uint8_t>& bytes) {
    const auto path = root / name;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return path.string();
}
std::string TestFiles::Jpeg(const std::string& name) {
    return Save(name, {0xff,0xd8,0xff,0xc0,0,11,8,0,2,0,2,1,1,0x11,0,0xff,0xd9});
}
std::string TestFiles::Bmp(const std::string& name) {
    std::vector<uint8_t> bytes(70, 0);
    bytes[0]='B'; bytes[1]='M'; bytes[2]=70; bytes[10]=54; bytes[14]=40;
    bytes[18]=2; bytes[22]=2; bytes[26]=1; bytes[28]=24; bytes[34]=16;
    for (size_t i=54;i<bytes.size();++i) bytes[i]=80;
    return Save(name, bytes);
}
std::string TestFiles::Png(const std::string& name) {
    return Save(name,{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,
        8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,248,223,192,240,31,
        0,6,128,2,127,16,76,27,225,0,0,0,0,73,69,78,68,174,66,96,130});
}
}
extern "C" {
lv_timer_t* __real_lv_timer_create(lv_timer_cb_t, uint32_t, void*);
lv_timer_t* __wrap_lv_timer_create(lv_timer_cb_t callback, uint32_t period, void* data) {
    if (photo_test::fail_timer && period == 120) { photo_test::fail_timer=false; return nullptr; }
    return __real_lv_timer_create(callback, period, data);
}
lv_result_t __real_lv_async_call(lv_async_cb_t, void*);
lv_result_t __wrap_lv_async_call(lv_async_cb_t callback, void* data) {
    if (photo_test::fail_async) { photo_test::fail_async=false; return LV_RESULT_INVALID; }
    return __real_lv_async_call(callback,data);
}
size_t __real_fread(void*, size_t, size_t, FILE*);
size_t __wrap_fread(void* data, size_t size, size_t count, FILE* file) {
    if (photo_test::short_read || photo_test::short_read_after == 0) {
        photo_test::short_read=false; photo_test::short_read_after=-1;
        return __real_fread(data,size,count/2,file);
    }
    if (photo_test::short_read_after > 0) --photo_test::short_read_after;
    return __real_fread(data,size,count,file);
}
int __real_fclose(FILE*);
int __wrap_fclose(FILE* file) {
    const int result=__real_fclose(file);
    if (photo_test::fail_close) { photo_test::fail_close=false; return EOF; }
    return result;
}
int __real_fseek(FILE*, long, int);
int __wrap_fseek(FILE* file, long offset, int origin) {
    if (photo_test::fail_seek) { photo_test::fail_seek=false; return -1; }
    return __real_fseek(file,offset,origin);
}
esp_err_t jpeg_to_image_scaled(const uint8_t*, size_t, uint8_t** out, size_t* length,
    size_t* width, size_t* height, size_t* stride, size_t, size_t) {
    if (photo_test::decode_result == ESP_OK || photo_test::decode_partial_failure) {
        *out=static_cast<uint8_t*>(std::calloc(12,1));
        ++photo_test::decoded_buffers;
        *length=12; *width=2; *height=2; *stride=6;
    }
    return photo_test::decode_result;
}
}
