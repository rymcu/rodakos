#include "test_framework.h"
#include "rodakos_adapters/file_directory.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>

namespace {
struct Faults {
    bool open = false;
    int read_after = -1;
    bool stat = false;
    bool close = false;
    bool successful_stat_errno = false;
    int entries_read = 0;
    int close_calls = 0;
    DIR* directory = nullptr;
} faults;

struct DirectoryFixture {
    std::string path;
    DirectoryFixture() {
        faults = {};
        char pattern[] = "/tmp/rodakos-file-directory-XXXXXX";
        const char* created = mkdtemp(pattern);
        RODAK_CHECK(created != nullptr);
        path = created;
    }
    ~DirectoryFixture() {
        faults = {};
        std::filesystem::remove_all(path);
    }
    void File(const char* name, const char* content = "audio") {
        std::ofstream file(path + "/" + name, std::ios::binary);
        file << content;
        RODAK_CHECK(file.good());
    }
};

std::vector<rodakos::FileEntry> OldEntries() {
    return {{"stale.wav", "/old/stale.wav", false, 5, 0}};
}
}  // namespace

extern "C" {
DIR* __real_opendir(const char*);
struct dirent* __real_readdir(DIR*);
int __real_stat(const char*, struct stat*);
int __real_closedir(DIR*);

DIR* __wrap_opendir(const char* path) {
    if (faults.open) {
        errno = EIO;
        return nullptr;
    }
    faults.directory = __real_opendir(path);
    return faults.directory;
}
struct dirent* __wrap_readdir(DIR* directory) {
    if (directory == faults.directory && faults.read_after >= 0 &&
        faults.entries_read >= faults.read_after) {
        errno = EIO;
        return nullptr;
    }
    auto* entry = __real_readdir(directory);
    if (entry != nullptr && std::strcmp(entry->d_name, ".") != 0 &&
        std::strcmp(entry->d_name, "..") != 0) {
        ++faults.entries_read;
    }
    return entry;
}
int __wrap_stat(const char* path, struct stat* info) {
    if (faults.stat) {
        errno = ENOENT;
        return -1;
    }
    const int result = __real_stat(path, info);
    if (faults.successful_stat_errno) {
        errno = EBUSY;
    }
    return result;
}
int __wrap_closedir(DIR* directory) {
    ++faults.close_calls;
    const int result = __real_closedir(directory);
    faults.directory = nullptr;
    if (faults.close) {
        errno = EBADF;
        return -1;
    }
    return result;
}
}

RODAK_TEST("directory listing returns sorted complete entries with file metadata") {
    DirectoryFixture fixture;
    fixture.File("z.wav", "12345");
    fixture.File("a.mp3", "123");
    std::filesystem::create_directory(fixture.path + "/music");
    auto entries = OldEntries();
    RODAK_CHECK(rodakos::ReadFileDirectory(fixture.path, entries));
    RODAK_CHECK_EQ(entries.size(), 3U);
    RODAK_CHECK_EQ(entries[0].name, "music");
    RODAK_CHECK(entries[0].is_directory);
    RODAK_CHECK_EQ(entries[1].name, "a.mp3");
    RODAK_CHECK_EQ(entries[1].path, fixture.path + "/a.mp3");
    RODAK_CHECK_EQ(entries[1].size, 3U);
    RODAK_CHECK(entries[1].modified_time > 0);
    RODAK_CHECK_EQ(entries[2].size, 5U);
    RODAK_CHECK_EQ(faults.close_calls, 1);
}

RODAK_TEST("empty directory succeeds and removes prior entries") {
    DirectoryFixture fixture;
    auto entries = OldEntries();
    RODAK_CHECK(rodakos::ReadFileDirectory(fixture.path, entries));
    RODAK_CHECK(entries.empty());
}

RODAK_TEST("unavailable directory fails without retaining stale entries") {
    DirectoryFixture fixture;
    auto entries = OldEntries();
    faults.open = true;
    RODAK_CHECK_FALSE(rodakos::ReadFileDirectory(fixture.path, entries));
    RODAK_CHECK_EQ(errno, EIO);
    RODAK_CHECK(entries.empty());
    RODAK_CHECK_EQ(faults.close_calls, 0);
}

RODAK_TEST("mid-listing read failure discards partial entries and closes the directory") {
    DirectoryFixture fixture;
    fixture.File("first.wav");
    fixture.File("second.wav");
    faults.read_after = 1;
    auto entries = OldEntries();
    RODAK_CHECK_FALSE(rodakos::ReadFileDirectory(fixture.path, entries));
    RODAK_CHECK_EQ(errno, EIO);
    RODAK_CHECK(entries.empty());
    RODAK_CHECK_EQ(faults.entries_read, 1);
    RODAK_CHECK_EQ(faults.close_calls, 1);
    faults.read_after = -1;
    RODAK_CHECK(rodakos::ReadFileDirectory(fixture.path, entries));
    RODAK_CHECK_EQ(entries.size(), 2U);
}

RODAK_TEST("failed metadata lookup cannot publish an invented zero-byte playable file") {
    DirectoryFixture fixture;
    fixture.File("missing.wav");
    faults.stat = true;
    auto entries = OldEntries();
    RODAK_CHECK_FALSE(rodakos::ReadFileDirectory(fixture.path, entries));
    RODAK_CHECK_EQ(errno, ENOENT);
    RODAK_CHECK(entries.empty());
    RODAK_CHECK_EQ(faults.close_calls, 1);
}

RODAK_TEST("failed directory close is a failed scan with no published entries") {
    DirectoryFixture fixture;
    fixture.File("track.wav");
    faults.close = true;
    auto entries = OldEntries();
    RODAK_CHECK_FALSE(rodakos::ReadFileDirectory(fixture.path, entries));
    RODAK_CHECK_EQ(errno, EBADF);
    RODAK_CHECK(entries.empty());
    RODAK_CHECK_EQ(faults.close_calls, 1);
}

RODAK_TEST("cleanup failure preserves the original metadata failure") {
    DirectoryFixture fixture;
    fixture.File("track.wav");
    faults.stat = true;
    faults.close = true;
    auto entries = OldEntries();
    RODAK_CHECK_FALSE(rodakos::ReadFileDirectory(fixture.path, entries));
    RODAK_CHECK_EQ(errno, ENOENT);
    RODAK_CHECK(entries.empty());
    RODAK_CHECK_EQ(faults.close_calls, 1);
}

RODAK_TEST("successful metadata calls leaving errno set do not turn EOF into a read error") {
    DirectoryFixture fixture;
    fixture.File("track.wav");
    faults.successful_stat_errno = true;
    auto entries = OldEntries();
    RODAK_CHECK(rodakos::ReadFileDirectory(fixture.path, entries));
    RODAK_CHECK_EQ(entries.size(), 1U);
}
