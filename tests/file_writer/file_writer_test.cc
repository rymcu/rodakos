#include "test_framework.h"
#include "rodakos_adapters/file_writer.h"

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <iterator>
#include <thread>
#include <unistd.h>

namespace {
using rodakos::FileWriteMode;
using rodakos::WriteFileBytes;
struct Faults {
    bool open = false, stream_open = false, short_write = false, stream_error = false;
    bool flush = false, close = false, unlink = false;
    int flush_calls = 0, close_calls = 0, fd_close_calls = 0, unlink_calls = 0;
    int fd = -1;
    FILE* file = nullptr;
};
thread_local Faults faults;
const std::vector<uint8_t> kBytes{1, 3, 5, 7, 9};
struct Fixture {
    std::string directory, path;
    Fixture() {
        faults = {};
        char pattern[] = "/tmp/rodakos-file-writer-XXXXXX";
        const char* created = mkdtemp(pattern);
        RODAK_CHECK(created != nullptr);
        directory = created;
        path = directory + "/capture.jpg";
    }
    ~Fixture() {
        faults = {};
        std::filesystem::remove_all(directory);
    }
    std::vector<uint8_t> Read() const {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
};
}

extern "C" {
int __real_open(const char*, int, ...);
FILE* __real_fdopen(int, const char*);
FILE* __real_fopen(const char*, const char*);
size_t __real_fwrite(const void*, size_t, size_t, FILE*);
int __real_ferror(FILE*);
int __real_fflush(FILE*);
int __real_fclose(FILE*);
int __real_close(int);
int __real_unlink(const char*);

int __wrap_open(const char* path, int flags, ...) {
    if (faults.open) { errno = EACCES; return -1; }
    va_list args;
    va_start(args, flags);
    const int mode = (flags & O_CREAT) != 0 ? va_arg(args, int) : 0;
    va_end(args);
    faults.fd = __real_open(path, flags, mode);
    return faults.fd;
}
FILE* __wrap_fdopen(int fd, const char* mode) {
    if (faults.stream_open) { errno = ENOMEM; return nullptr; }
    faults.file = __real_fdopen(fd, mode);
    return faults.file;
}
FILE* __wrap_fopen(const char* path, const char* mode) {
    if (faults.open) { errno = EACCES; return nullptr; }
    faults.file = __real_fopen(path, mode);
    return faults.file;
}
size_t __wrap_fwrite(const void* data, size_t size, size_t count, FILE* file) {
    if (file == faults.file && faults.short_write && count != 0) {
        const size_t written = __real_fwrite(data, size, count - 1, file);
        errno = ENOSPC;
        return written;
    }
    return __real_fwrite(data, size, count, file);
}
int __wrap_ferror(FILE* file) {
    if (file == faults.file && faults.stream_error) { errno = EIO; return 1; }
    return __real_ferror(file);
}
int __wrap_fflush(FILE* file) {
    if (file == faults.file) {
        ++faults.flush_calls;
        if (faults.flush) { errno = EIO; return EOF; }
    }
    return __real_fflush(file);
}
int __wrap_fclose(FILE* file) {
    const bool tracked = file == faults.file;
    const int result = __real_fclose(file);
    if (tracked) {
        faults.file = nullptr;
        ++faults.close_calls;
        if (faults.close) { errno = EIO; return EOF; }
    }
    return result;
}
int __wrap_close(int fd) {
    if (fd == faults.fd) ++faults.fd_close_calls;
    return __real_close(fd);
}
int __wrap_unlink(const char* path) {
    ++faults.unlink_calls;
    if (faults.unlink) { errno = EACCES; return -1; }
    return __real_unlink(path);
}
}

RODAK_TEST("new file success requires full contents flush and close") {
    Fixture f;
    RODAK_CHECK(WriteFileBytes(f.path, kBytes, FileWriteMode::kCreateNew));
    RODAK_CHECK_EQ(f.Read(), kBytes);
    RODAK_CHECK_EQ(faults.flush_calls, 1);
    RODAK_CHECK_EQ(faults.close_calls, 1);
    RODAK_CHECK_EQ(faults.unlink_calls, 0);
}
RODAK_TEST("empty file create remains a real flush and close") {
    Fixture f;
    RODAK_CHECK(WriteFileBytes(f.path, {}, FileWriteMode::kCreateNew));
    RODAK_CHECK_EQ(std::filesystem::file_size(f.path), 0U);
    RODAK_CHECK_EQ(faults.flush_calls, 1);
    RODAK_CHECK_EQ(faults.close_calls, 1);
}
RODAK_TEST("exclusive create never truncates or deletes an existing file") {
    Fixture f;
    RODAK_CHECK(WriteFileBytes(f.path, kBytes, FileWriteMode::kCreateNew));
    faults = {};
    RODAK_CHECK_FALSE(WriteFileBytes(f.path, {2}, FileWriteMode::kCreateNew));
    RODAK_CHECK_EQ(errno, EEXIST);
    RODAK_CHECK_EQ(f.Read(), kBytes);
    RODAK_CHECK_EQ(faults.unlink_calls, 0);
}
RODAK_TEST("open failure cannot remove an existing file or fabricate a saved result") {
    Fixture f;
    RODAK_CHECK(WriteFileBytes(f.path, kBytes, FileWriteMode::kCreateNew));
    faults = {}; faults.open = true;
    RODAK_CHECK_FALSE(WriteFileBytes(f.path, {2}, FileWriteMode::kCreateNew));
    RODAK_CHECK_EQ(errno, EACCES);
    faults.open = false;
    RODAK_CHECK_EQ(f.Read(), kBytes);
    RODAK_CHECK_EQ(faults.unlink_calls, 0);
}
RODAK_TEST("fdopen failure closes the new descriptor and removes only the new file") {
    Fixture f;
    faults.stream_open = true;
    RODAK_CHECK_FALSE(WriteFileBytes(f.path, kBytes, FileWriteMode::kCreateNew));
    RODAK_CHECK_EQ(errno, ENOMEM);
    RODAK_CHECK_EQ(faults.fd_close_calls, 1);
    RODAK_CHECK_FALSE(std::filesystem::exists(f.path));
}
RODAK_TEST("short write and stream error clean up the owned file and permit retry") {
    for (const bool short_write : {false, true}) {
        Fixture f;
        faults.short_write = short_write;
        faults.stream_error = !short_write;
        RODAK_CHECK_FALSE(WriteFileBytes(f.path, kBytes, FileWriteMode::kCreateNew));
        RODAK_CHECK_EQ(errno, short_write ? ENOSPC : EIO);
        RODAK_CHECK_EQ(faults.close_calls, 1);
        RODAK_CHECK_FALSE(std::filesystem::exists(f.path));
        faults = {};
        RODAK_CHECK(WriteFileBytes(f.path, kBytes, FileWriteMode::kCreateNew));
        RODAK_CHECK_EQ(f.Read(), kBytes);
    }
}
RODAK_TEST("flush or final close failure is not success even if the bytes were written") {
    for (const bool fail_flush : {false, true}) {
        Fixture f;
        faults.flush = fail_flush; faults.close = !fail_flush;
        RODAK_CHECK_FALSE(WriteFileBytes(f.path, kBytes, FileWriteMode::kCreateNew));
        RODAK_CHECK_EQ(errno, EIO);
        RODAK_CHECK_EQ(faults.close_calls, 1);
        RODAK_CHECK_FALSE(std::filesystem::exists(f.path));
    }
}
RODAK_TEST("cleanup failure preserves the write error and cannot delete a later existing file") {
    Fixture f;
    faults.short_write = true; faults.close = true; faults.unlink = true;
    RODAK_CHECK_FALSE(WriteFileBytes(f.path, kBytes, FileWriteMode::kCreateNew));
    RODAK_CHECK_EQ(errno, ENOSPC);
    RODAK_CHECK(std::filesystem::exists(f.path));
    const auto partial = f.Read();
    faults = {};
    RODAK_CHECK_FALSE(WriteFileBytes(f.path, {9}, FileWriteMode::kCreateNew));
    RODAK_CHECK_EQ(errno, EEXIST);
    RODAK_CHECK_EQ(f.Read(), partial);
    RODAK_CHECK_EQ(faults.unlink_calls, 0);
}
RODAK_TEST("replace and append retain their semantics and never delete a failed target") {
    Fixture f;
    RODAK_CHECK(WriteFileBytes(f.path, {1, 2}, FileWriteMode::kReplace));
    RODAK_CHECK(WriteFileBytes(f.path, {3}, FileWriteMode::kAppend));
    RODAK_CHECK_EQ(f.Read(), (std::vector<uint8_t>{1, 2, 3}));
    RODAK_CHECK(WriteFileBytes(f.path, {4}, FileWriteMode::kReplace));
    RODAK_CHECK_EQ(f.Read(), (std::vector<uint8_t>{4}));
    for (const auto mode : {FileWriteMode::kReplace, FileWriteMode::kAppend}) {
        faults = {}; faults.close = true;
        RODAK_CHECK_FALSE(WriteFileBytes(f.path, {5}, mode));
        RODAK_CHECK(std::filesystem::exists(f.path));
        RODAK_CHECK_EQ(faults.unlink_calls, 0);
    }
}
RODAK_TEST("concurrent new writers have one winner and never mix or overwrite contents") {
    Fixture f;
    std::atomic<int> ready{0}, successes{0};
    const auto writer = [&](uint8_t value) {
        ++ready;
        while (ready.load() != 2) std::this_thread::yield();
        if (WriteFileBytes(f.path, {value, value}, FileWriteMode::kCreateNew)) ++successes;
    };
    std::thread first(writer, 3), second(writer, 7);
    first.join(); second.join();
    RODAK_CHECK_EQ(successes.load(), 1);
    const auto saved = f.Read();
    RODAK_CHECK(saved == std::vector<uint8_t>({3, 3}) || saved == std::vector<uint8_t>({7, 7}));
}
