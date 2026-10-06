#include "test_framework.h"
#include "rodakos_adapters/file_path_lease.h"

#include <atomic>
#include <thread>

using rodakos::NormalizeStoragePath;
using rodakos::StoragePathLeaseSet;
using rodakos::StoragePathsConflict;

RODAK_TEST("normalizes relative, full mount and FAT case/slash variants") {
    std::string normalized;
    RODAK_CHECK(NormalizeStoragePath("photos\\IMG.JPG", "/SDCARD", normalized));
    RODAK_CHECK_EQ(normalized, "/photos/img.jpg");
    RODAK_CHECK(NormalizeStoragePath("/sdcard//photos/IMG.JPG", "/sdcard", normalized));
    RODAK_CHECK_EQ(normalized, "/photos/img.jpg");
    RODAK_CHECK(NormalizeStoragePath("/photos/IMG.JPG", "/sdcard", normalized));
    RODAK_CHECK_EQ(normalized, "/photos/img.jpg");
    RODAK_CHECK(NormalizeStoragePath("/SDCARD", "/sdcard", normalized));
    RODAK_CHECK_EQ(normalized, "/");
    RODAK_CHECK(NormalizeStoragePath("\\SDCARD\\", "/sdcard", normalized));
    RODAK_CHECK_EQ(normalized, "/");
    RODAK_CHECK(NormalizeStoragePath("", "/sdcard", normalized));
    RODAK_CHECK_EQ(normalized, "/");
}

RODAK_TEST("rejects every parent traversal spelling") {
    std::string normalized;
    RODAK_CHECK_FALSE(NormalizeStoragePath("/photos/../secret", "/sdcard", normalized));
    RODAK_CHECK_FALSE(NormalizeStoragePath("../secret", "/sdcard", normalized));
    RODAK_CHECK_FALSE(NormalizeStoragePath("/sdcard/a/../../secret", "/sdcard", normalized));
    RODAK_CHECK_FALSE(NormalizeStoragePath("", "/sdcard/../bad", normalized));
    std::string nul_path = "/photos";
    nul_path.push_back('\0');
    nul_path += "/hidden.jpg";
    RODAK_CHECK_FALSE(NormalizeStoragePath(nul_path, "/sdcard", normalized));
}

RODAK_TEST("path conflicts are exact and ancestor/descendant only") {
    RODAK_CHECK(StoragePathsConflict("/recordings/a.wav", "/recordings/a.wav"));
    RODAK_CHECK(StoragePathsConflict("/recordings", "/recordings/a.wav"));
    RODAK_CHECK(StoragePathsConflict("/recordings/a.wav", "/recordings"));
    RODAK_CHECK_FALSE(StoragePathsConflict("/recordings/a.wav", "/recordings/a.wav.bak"));
    RODAK_CHECK_FALSE(StoragePathsConflict("/music", "/recordings/a.wav"));
    RODAK_CHECK_FALSE(StoragePathsConflict("/recordings/a.wav", "/recordings/a.wav.bak"));
    RODAK_CHECK_FALSE(StoragePathsConflict("/recordings/a", "/recordings/ab/file.wav"));
}

RODAK_TEST("lease set rejects overlapping paths and releases them") {
    StoragePathLeaseSet leases;
    RODAK_CHECK(leases.TryAcquire("/recordings/a.wav"));
    RODAK_CHECK_FALSE(leases.TryAcquire("/recordings"));
    RODAK_CHECK(leases.TryAcquire("/recordings/a.wav.bak"));
    RODAK_CHECK(leases.TryAcquire("/music/a.mp3"));
    leases.Release("/recordings/a.wav");
    leases.Release("/recordings/a.wav.bak");
    RODAK_CHECK(leases.TryAcquire("/recordings"));
    RODAK_CHECK(leases.Conflicts("/recordings/new.wav"));
    leases.Release("/recordings");
    RODAK_CHECK_FALSE(leases.Conflicts("/recordings/new.wav"));
}

RODAK_TEST("concurrent disjoint leases can proceed while overlap has one winner") {
    StoragePathLeaseSet leases;
    std::atomic<int> winners{0};
    auto acquire = [&](const char* path) {
        if (leases.TryAcquire(path)) ++winners;
    };
    std::thread first(acquire, "/recordings/a.wav");
    std::thread second(acquire, "/recordings/a.wav");
    std::thread third(acquire, "/photos/a.jpg");
    first.join();
    second.join();
    third.join();
    RODAK_CHECK_EQ(winners.load(), 2);
}
