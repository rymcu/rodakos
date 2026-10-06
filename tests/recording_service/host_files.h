#pragma once
#include "rodakos_adapters/file_service.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cerrno>
#include <mutex>
#include <set>
namespace recording_host {
class Files : public rodakos::FileService {
public:
    explicit Files(std::string root) : root_(std::move(root)) { std::filesystem::create_directories(root_); }
    bool Init() override { if (init_hook) init_hook(); mounted = !fail_mount; return mounted; }
    void Deinit() override { mounted = false; }
    bool IsMounted() const override { return mounted; }
    const char* GetMountPoint() const override { return root_.c_str(); }
    FileSystemType GetFileSystemType() const override { return FileSystemType::FATFS; }
    MediumType GetMediumType() const override { return MediumType::SDCard; }
    bool GetCapacity(Capacity&) override { return true; }
    bool ListDirectory(const std::string& path, std::vector<rodakos::FileEntry>& entries) override {
        entries.clear();
        if (list_hook) list_hook();
        if (fail_list) return false;
        std::error_code error;
        std::filesystem::directory_iterator iterator(Resolve(path), error);
        if (error) return false;
        for (const auto& entry : iterator) {
            const bool directory = entry.is_directory(error);
            if (error) return false;
            const size_t size = directory ? 0 : entry.file_size(error);
            if (error) return false;
            entries.push_back({entry.path().filename().string(), entry.path().string(), directory, size, 1});
        }
        return true;
    }
    bool ReadFile(const std::string& path, std::vector<uint8_t>& bytes) override {
        std::ifstream file(Resolve(path), std::ios::binary);
        if (!file) return false;
        bytes.assign(std::istreambuf_iterator<char>(file), {}); return !file.bad();
    }
    bool WriteFile(const std::string&, const std::vector<uint8_t>&, bool = false) override { return false; }
    bool WithWriteLease(const std::string& path, const std::function<bool()>& operation) override {
        {
            std::lock_guard<std::mutex> lock(lease_mutex);
            if (leased_paths.count(path) != 0) { errno = EBUSY; return false; }
            leased_paths.insert(path); lease_active = true; leased_path = path;
        }
        if (lease_hook) lease_hook();
        const bool result = operation();
        { std::lock_guard<std::mutex> lock(lease_mutex); leased_paths.erase(path); lease_active = !leased_paths.empty(); leased_path.clear(); }
        return result;
    }
    bool DeleteFile(const std::string& path) override {
        std::lock_guard<std::mutex> lock(lease_mutex);
        if (leased_paths.count(path) != 0) { errno = EBUSY; return false; }
        std::error_code error; return std::filesystem::remove(Resolve(path), error) && !error;
    }
    bool DeleteDirectory(const std::string&) override { return false; }
    bool CreateDirectory(const std::string& path) override {
        std::error_code error; std::filesystem::create_directories(Resolve(path), error); return !error;
    }
    bool Rename(const std::string& old_path, const std::string& new_path) override {
        std::lock_guard<std::mutex> lock(lease_mutex);
        if (leased_paths.count(old_path) != 0 || leased_paths.count(new_path) != 0) { errno = EBUSY; return false; }
        std::error_code error; std::filesystem::rename(Resolve(old_path), Resolve(new_path), error); return !error;
    }
    bool Exists(const std::string& path) override { return std::filesystem::exists(Resolve(path)); }
    size_t GetFileSize(const std::string& path) override { std::error_code error; const auto value = std::filesystem::file_size(Resolve(path), error); return error ? 0 : value; }
    std::string Resolve(const std::string& path) const { return root_ + (path.empty() || path[0] != '/' ? "/" : "") + path; }
    std::atomic<bool> mounted{false}, fail_mount{false}, fail_list{false};
    std::function<void()> list_hook, init_hook, lease_hook;
    std::mutex lease_mutex;
    bool lease_active = false;
    std::string leased_path;
    std::set<std::string> leased_paths;
private:
    std::string root_;
};
}
