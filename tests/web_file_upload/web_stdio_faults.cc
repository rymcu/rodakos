#include "web_stdio_faults.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
namespace {
std::mutex mutex;
bool fail_flush = false;
bool fail_close = false;
bool fail_error = false;
std::set<FILE*> writers;
bool IsUpload(const char* path) { return path != nullptr && std::strstr(path, "/recordings/") != nullptr; }
}
namespace web_upload_test {
void SetFaults(bool flush, bool close, bool error) { std::lock_guard<std::mutex> lock(mutex); fail_flush = flush; fail_close = close; fail_error = error; }
void ResetFaults() { SetFaults(false, false); }
}
extern "C" FILE* __real_fopen(const char*, const char*);
extern "C" size_t __real_fwrite(const void*, size_t, size_t, FILE*);
extern "C" int __real_fflush(FILE*);
extern "C" int __real_ferror(FILE*);
extern "C" int __real_fclose(FILE*);
extern "C" int __real_remove(const char*);
extern "C" FILE* __wrap_fopen(const char* path, const char* mode) {
    FILE* file = __real_fopen(path, mode);
    if (file != nullptr && IsUpload(path) && mode != nullptr && mode[0] == 'w') {
        std::lock_guard<std::mutex> lock(mutex); writers.insert(file);
    }
    return file;
}
extern "C" size_t __wrap_fwrite(const void* data, size_t size, size_t count, FILE* file) {
    return __real_fwrite(data, size, count, file);
}
extern "C" int __wrap_fflush(FILE* file) {
    const int result = __real_fflush(file);
    std::lock_guard<std::mutex> lock(mutex);
    return writers.count(file) != 0 && fail_flush ? EOF : result;
}
extern "C" int __wrap_ferror(FILE* file) {
    const int result = __real_ferror(file);
    std::lock_guard<std::mutex> lock(mutex);
    return writers.count(file) != 0 && fail_error ? 1 : result;
}
extern "C" int __wrap_fclose(FILE* file) {
    const int result = __real_fclose(file);
    std::lock_guard<std::mutex> lock(mutex);
    const bool tracked = writers.erase(file) != 0;
    return tracked && fail_close ? EOF : result;
}
extern "C" int __wrap_remove(const char* path) { return __real_remove(path); }
