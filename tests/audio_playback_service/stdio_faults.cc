#include "stdio_faults.h"
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <set>
namespace {
std::mutex mutex;
unsigned remaining = 0;
bool error = false;
size_t short_read = 0;
std::set<FILE*> failures;
}
namespace audio_host {
void FailRead(unsigned number, bool io_error, size_t short_bytes) {
    std::lock_guard<std::mutex> lock(mutex);
    remaining = number; error = io_error; short_read = short_bytes;
}
void ResetReadFault() {
    std::lock_guard<std::mutex> lock(mutex); remaining = 0; failures.clear();
}
}
extern "C" size_t __real_fread(void*, size_t, size_t, FILE*);
extern "C" int __real_ferror(FILE*);
extern "C" int __real_fclose(FILE*);
extern "C" size_t __wrap_fread(void* buffer, size_t size, size_t count, FILE* fp) {
    std::lock_guard<std::mutex> lock(mutex);
    if (size * count >= 64 && remaining != 0 && --remaining == 0) {
        if (error) failures.insert(fp);
        return __real_fread(buffer, size, std::min(count, short_read), fp);
    }
    return __real_fread(buffer, size, count, fp);
}
extern "C" int __wrap_ferror(FILE* fp) {
    std::lock_guard<std::mutex> lock(mutex);
    return failures.count(fp) ? 1 : __real_ferror(fp);
}
extern "C" int __wrap_fclose(FILE* fp) {
    std::lock_guard<std::mutex> lock(mutex); failures.erase(fp); return __real_fclose(fp);
}
