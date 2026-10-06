#include "stdio_faults.h"
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <set>
#include <unistd.h>
namespace {
std::mutex mutex;
recording_host::StdioFaults faults;
struct Writer { int descriptor; unsigned headers = 0; };
std::map<FILE*, Writer> writers;
std::set<int> descriptors;
std::set<FILE*> readers, read_errors;
unsigned reader_count = 0, reader_close_count = 0;
std::vector<std::string> events;
bool IsRecordingPath(const char* path) { return path && std::strstr(path, "/recordings/") != nullptr; }
}
namespace recording_host {
void SetStdioFaults(const StdioFaults& next) {
    std::lock_guard<std::mutex> lock(mutex); faults = next; reader_count = reader_close_count = 0;
}
void ResetStdioFaults() {
    std::lock_guard<std::mutex> lock(mutex);
    faults = {}; reader_count = reader_close_count = 0; events.clear();
}
size_t OutstandingRecordingDescriptors() { std::lock_guard<std::mutex> lock(mutex); return descriptors.size(); }
std::vector<std::string> StdioEvents() { std::lock_guard<std::mutex> lock(mutex); return events; }
}
extern "C" int __real_open(const char*, int, ...);
extern "C" int __real_close(int);
extern "C" FILE* __real_fdopen(int, const char*);
extern "C" FILE* __real_fopen(const char*, const char*);
extern "C" size_t __real_fwrite(const void*, size_t, size_t, FILE*);
extern "C" size_t __real_fread(void*, size_t, size_t, FILE*);
extern "C" int __real_ferror(FILE*);
extern "C" int __real_fseek(FILE*, long, int);
extern "C" int __real_fflush(FILE*);
extern "C" int __real_fclose(FILE*);
extern "C" int __real_remove(const char*);
extern "C" int __wrap_open(const char* path, int flags, ...) {
    int mode = 0;
    if (flags & O_CREAT) { va_list arguments; va_start(arguments, flags); mode = va_arg(arguments, int); va_end(arguments); }
    std::lock_guard<std::mutex> lock(mutex);
    const bool recording = IsRecordingPath(path) && (flags & O_EXCL);
    if (recording && (faults.create || faults.all_names_exist)) {
        errno = faults.all_names_exist ? EEXIST : EIO; return -1;
    }
    const int descriptor = __real_open(path, flags, mode);
    if (recording && descriptor >= 0) { descriptors.insert(descriptor); events.push_back("create"); }
    return descriptor;
}
extern "C" int __wrap_close(int descriptor) {
    std::lock_guard<std::mutex> lock(mutex); descriptors.erase(descriptor); return __real_close(descriptor);
}
extern "C" FILE* __wrap_fdopen(int descriptor, const char* mode) {
    std::lock_guard<std::mutex> lock(mutex);
    if (descriptors.count(descriptor) && faults.fdopen) { errno = EIO; return nullptr; }
    FILE* fp = __real_fdopen(descriptor, mode);
    if (fp && descriptors.count(descriptor)) writers.emplace(fp, Writer{descriptor});
    return fp;
}
extern "C" FILE* __wrap_fopen(const char* path, const char* mode) {
    std::lock_guard<std::mutex> lock(mutex);
    FILE* fp = __real_fopen(path, mode);
    if (fp && IsRecordingPath(path) && mode[0] == 'r') readers.insert(fp);
    return fp;
}
extern "C" size_t __wrap_fwrite(const void* data, size_t size, size_t count, FILE* fp) {
    std::lock_guard<std::mutex> lock(mutex);
    auto writer = writers.find(fp);
    if (writer != writers.end()) {
        bool fail = false;
        if (size * count == 44) {
            const auto header = ++writer->second.headers;
            events.push_back(header == 1 ? "initial-header" : "final-header");
            if (header != 1 && faults.final_header_hook) faults.final_header_hook();
            fail = header == 1 ? faults.initial_header : faults.final_header;
        } else { events.push_back("data"); fail = faults.data_write; }
        if (fail) return __real_fwrite(data, size, count / 2, fp);
    }
    return __real_fwrite(data, size, count, fp);
}
extern "C" size_t __wrap_fread(void* data, size_t size, size_t count, FILE* fp) {
    std::lock_guard<std::mutex> lock(mutex);
    if (readers.count(fp) && ++reader_count == faults.library_read_error) { read_errors.insert(fp); return 0; }
    return __real_fread(data, size, count, fp);
}
extern "C" int __wrap_ferror(FILE* fp) {
    std::lock_guard<std::mutex> lock(mutex); return read_errors.count(fp) ? 1 : __real_ferror(fp);
}
extern "C" int __wrap_fseek(FILE* fp, long offset, int whence) {
    std::lock_guard<std::mutex> lock(mutex);
    if (writers.count(fp)) {
        events.push_back("seek");
        if (faults.seek) { errno = EIO; return -1; }
    }
    return __real_fseek(fp, offset, whence);
}
extern "C" int __wrap_fflush(FILE* fp) {
    std::lock_guard<std::mutex> lock(mutex);
    const bool recording = writers.count(fp);
    if (recording) events.push_back("flush");
    const int result = __real_fflush(fp);
    if (recording && faults.flush) { errno = EIO; return EOF; }
    return result;
}
extern "C" int __wrap_fclose(FILE* fp) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto writer = writers.find(fp);
    bool fail = false;
    if (writer != writers.end()) {
        if (faults.close_hook) faults.close_hook();
        events.push_back("close"); descriptors.erase(writer->second.descriptor); writers.erase(writer); fail = faults.close;
    }
    if (readers.erase(fp)) fail = ++reader_close_count == faults.library_close_error;
    read_errors.erase(fp);
    const int result = __real_fclose(fp);
    if (fail) { errno = EIO; return EOF; }
    return result;
}
extern "C" int __wrap_remove(const char* path) {
    std::lock_guard<std::mutex> lock(mutex);
    if (IsRecordingPath(path)) {
        events.push_back("remove");
        if (faults.remove) { errno = EIO; return -1; }
    }
    return __real_remove(path);
}
extern "C" std::time_t __wrap_time(std::time_t* result) {
    constexpr std::time_t value = 1800000000;
    if (result) *result = value;
    return value;
}
