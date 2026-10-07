#pragma once
#include "rodakos_adapters/file_service.h"
#include "esp_err.h"
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace photo_test {
inline bool fail_timer = false;
inline bool short_read = false;
inline bool fail_close = false;
inline bool fail_seek = false;
inline bool fail_async = false;
inline int short_read_after = -1;
inline int decode_result = ESP_OK;
inline bool decode_partial_failure = false;
void ResetFailures();

struct TestFiles {
    std::filesystem::path root;
    TestFiles();
    ~TestFiles();
    std::string Jpeg(const std::string& name);
    std::string Bmp(const std::string& name);
    std::string Png(const std::string& name);
    std::string PngRgb(const std::string& name);
    std::string Save(const std::string& name, const std::vector<uint8_t>& bytes);
};

class Files final : public rodakos::FileService {
public:
    bool mounted = true;
    bool mount_ok = true;
    std::map<std::string, std::vector<rodakos::FileEntry>> directories{{"/", {}}};
    std::map<std::string, int> failures;
    std::vector<std::string> reads;
    bool Init() override { mounted = mount_ok; return mounted; }
    void Deinit() override { mounted = false; }
    bool IsMounted() const override { return mounted; }
    const char* GetMountPoint() const override { return "/"; }
    FileSystemType GetFileSystemType() const override { return FileSystemType::FATFS; }
    MediumType GetMediumType() const override { return MediumType::SDCard; }
    bool GetCapacity(Capacity&) override { return false; }
    bool ListDirectory(const std::string& path, std::vector<rodakos::FileEntry>& entries) override {
        reads.push_back(path); entries.clear();
        if (!mounted) { errno = ENODEV; return false; }
        if (failures.contains(path)) { errno = failures[path]; return false; }
        if (!directories.contains(path)) { errno = ENOENT; return false; }
        entries = directories[path]; return true;
    }
    bool ReadFile(const std::string&, std::vector<uint8_t>&) override { return false; }
    bool WriteFile(const std::string&, const std::vector<uint8_t>&, bool) override { return false; }
    bool DeleteFile(const std::string&) override { return false; }
    bool DeleteDirectory(const std::string&) override { return false; }
    bool CreateDirectory(const std::string&) override { return false; }
    bool Rename(const std::string&, const std::string&) override { return false; }
    bool Exists(const std::string&) override { return false; }
    size_t GetFileSize(const std::string&) override { return 0; }
    void Album(const std::string& path) {
        directories["/"].push_back({"photos", path, true, 0, 0});
        directories[path] = {};
    }
    void Image(const std::string& directory, const std::string& path) {
        directories[directory].push_back({std::filesystem::path(path).filename().string(), path, false, 20, 0});
    }
};
}
