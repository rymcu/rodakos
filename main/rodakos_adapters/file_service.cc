#include "file_service.h"

#include <dev_fs_fat.h>
#include <esp_board_manager.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <cerrno>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>

namespace rodakos {

namespace {
constexpr const char* TAG = "FileService";
constexpr const char* kSdCardDeviceName = "fs_sdcard";
constexpr const char* kMountPoint = "/sdcard";

/**
 * @brief Brookesia-style file service implementation
 */
class FileServiceImpl : public FileService {
public:
    FileServiceImpl() = default;
    ~FileServiceImpl() override { Deinit(); }

    bool Init() override {
        if (is_mounted_) {
            ESP_LOGW(TAG, "SD card already mounted");
            return true;
        }

        ESP_LOGI(TAG, "Initializing SD card");

        esp_err_t ret = esp_board_manager_init_device_by_name(kSdCardDeviceName);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SD card mount skipped (%s)", esp_err_to_name(ret));
            return false;
        }
        board_ref_acquired_ = true;

        void* fs_handle = nullptr;
        ret = esp_board_manager_get_device_handle(kSdCardDeviceName, &fs_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SD card is not mounted by Board Manager (%s)", esp_err_to_name(ret));
            ReleaseBoardRef();
            return false;
        }

        auto* handle = static_cast<dev_fs_fat_handle_t*>(fs_handle);
        if (handle == nullptr || handle->card == nullptr || handle->mount_point == nullptr) {
            ESP_LOGW(TAG, "Board Manager SD card handle is incomplete");
            ReleaseBoardRef();
            return false;
        }

        is_mounted_ = true;
        card_ = handle->card;
        mount_point_ = handle->mount_point;

        sdmmc_card_print_info(stdout, card_);
        ESP_LOGI(TAG, "SD card mounted at %s", mount_point_.c_str());

        return true;
    }

    void Deinit() override {
        if (!is_mounted_) {
            return;
        }

        is_mounted_ = false;
        card_ = nullptr;
        mount_point_ = kMountPoint;
        ReleaseBoardRef();

        ESP_LOGI(TAG, "File service released SD card mount");
    }

    bool IsMounted() const override {
        return is_mounted_;
    }

    const char* GetMountPoint() const override {
        return mount_point_.c_str();
    }

    FileSystemType GetFileSystemType() const override {
        return FileSystemType::FATFS;
    }

    MediumType GetMediumType() const override {
        return MediumType::SDCard;
    }

    bool GetCapacity(Capacity& capacity) override {
        if (!is_mounted_) {
            return false;
        }

        // Get total capacity from card
        if (card_ != nullptr) {
            capacity.total_bytes = static_cast<uint64_t>(card_->csd.capacity) * card_->csd.sector_size;
        } else {
            capacity.total_bytes = 0;
        }

        // Get free space from FAT
        FATFS* fs;
        DWORD fre_clust;
        if (f_getfree("0:", &fre_clust, &fs) == FR_OK) {
            capacity.free_bytes = static_cast<uint64_t>(fre_clust) * fs->csize * 512;
            capacity.used_bytes = capacity.total_bytes - capacity.free_bytes;
        } else {
            capacity.free_bytes = 0;
            capacity.used_bytes = 0;
        }

        return true;
    }

    bool ListDirectory(const std::string& path, std::vector<FileEntry>& entries) override {
        if (!is_mounted_) {
            ESP_LOGE(TAG, "SD card not mounted");
            return false;
        }

        std::string full_path = GetFullPath(path);
        DIR* dir = opendir(full_path.c_str());
        if (dir == nullptr) {
            ESP_LOGE(TAG, "Failed to open directory: %s (%s)", full_path.c_str(), std::strerror(errno));
            return false;
        }

        entries.clear();
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            // Skip "." and ".."
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }

            FileEntry entry;
            entry.name = ent->d_name;
            entry.path = full_path + "/" + ent->d_name;
            entry.is_directory = false;

            // Get file info
            struct stat st;
            if (stat(entry.path.c_str(), &st) == 0) {
                entry.is_directory = S_ISDIR(st.st_mode);
                entry.size = st.st_size;
                entry.modified_time = st.st_mtime;
            } else {
                entry.size = 0;
                entry.modified_time = 0;
            }

            entries.push_back(entry);
        }

        closedir(dir);

        // Sort: directories first, then by name
        std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b) {
            if (a.is_directory != b.is_directory) {
                return a.is_directory;
            }
            return a.name < b.name;
        });

        ESP_LOGI(TAG, "Listed %zu entries in %s", entries.size(), full_path.c_str());
        return true;
    }

    bool ReadFile(const std::string& path, std::vector<uint8_t>& data) override {
        if (!is_mounted_) {
            ESP_LOGE(TAG, "SD card not mounted");
            return false;
        }

        std::string full_path = GetFullPath(path);
        FILE* f = fopen(full_path.c_str(), "rb");
        if (f == nullptr) {
            ESP_LOGE(TAG, "Failed to open file: %s (%s)", full_path.c_str(), std::strerror(errno));
            return false;
        }

        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (file_size < 0) {
            ESP_LOGE(TAG, "Failed to get file size: %s", full_path.c_str());
            fclose(f);
            return false;
        }

        data.resize(file_size);
        size_t read_bytes = fread(data.data(), 1, file_size, f);
        fclose(f);

        if (read_bytes != static_cast<size_t>(file_size)) {
            ESP_LOGE(TAG, "Failed to read file: %s", full_path.c_str());
            return false;
        }

        ESP_LOGI(TAG, "Read %zu bytes from %s", read_bytes, full_path.c_str());
        return true;
    }

    bool WriteFile(const std::string& path, const std::vector<uint8_t>& data, bool append) override {
        if (!is_mounted_) {
            ESP_LOGE(TAG, "SD card not mounted");
            return false;
        }

        std::string full_path = GetFullPath(path);
        const char* mode = append ? "ab" : "wb";
        FILE* f = fopen(full_path.c_str(), mode);
        if (f == nullptr) {
            ESP_LOGE(TAG, "Failed to open file: %s (%s)", full_path.c_str(), std::strerror(errno));
            return false;
        }

        size_t written = fwrite(data.data(), 1, data.size(), f);
        fclose(f);

        if (written != data.size()) {
            ESP_LOGE(TAG, "Failed to write file: %s", full_path.c_str());
            return false;
        }

        ESP_LOGI(TAG, "Wrote %zu bytes to %s", written, full_path.c_str());
        return true;
    }

    bool DeleteFile(const std::string& path) override {
        if (!is_mounted_) {
            ESP_LOGE(TAG, "SD card not mounted");
            return false;
        }

        std::string full_path = GetFullPath(path);
        if (unlink(full_path.c_str()) != 0) {
            ESP_LOGE(TAG, "Failed to delete file: %s (%s)", full_path.c_str(), std::strerror(errno));
            return false;
        }

        ESP_LOGI(TAG, "Deleted file: %s", full_path.c_str());
        return true;
    }

    bool DeleteDirectory(const std::string& path) override {
        if (!is_mounted_) {
            ESP_LOGE(TAG, "SD card not mounted");
            return false;
        }

        std::string full_path = GetFullPath(path);

        // Recursively delete directory contents
        std::vector<FileEntry> entries;
        if (!ListDirectory(path, entries)) {
            return false;
        }

        for (const auto& entry : entries) {
            if (entry.is_directory) {
                if (!DeleteDirectory(entry.path)) {
                    return false;
                }
            } else {
                if (unlink(entry.path.c_str()) != 0) {
                    ESP_LOGE(TAG, "Failed to delete file: %s (%s)", entry.path.c_str(), std::strerror(errno));
                    return false;
                }
            }
        }

        // Delete empty directory
        if (rmdir(full_path.c_str()) != 0) {
            ESP_LOGE(TAG, "Failed to delete directory: %s (%s)", full_path.c_str(), std::strerror(errno));
            return false;
        }

        ESP_LOGI(TAG, "Deleted directory: %s", full_path.c_str());
        return true;
    }

    bool CreateDirectory(const std::string& path) override {
        if (!is_mounted_) {
            ESP_LOGE(TAG, "SD card not mounted");
            return false;
        }

        std::string full_path = GetFullPath(path);
        if (mkdir(full_path.c_str(), 0775) != 0) {
            const int err = errno;
            struct stat st;
            if (err == EEXIST && stat(full_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                return true;
            }
            ESP_LOGE(TAG, "Failed to create directory: %s (%s)", full_path.c_str(), std::strerror(err));
            return false;
        }

        ESP_LOGI(TAG, "Created directory: %s", full_path.c_str());
        return true;
    }

    bool Rename(const std::string& old_path, const std::string& new_path) override {
        if (!is_mounted_) {
            ESP_LOGE(TAG, "SD card not mounted");
            return false;
        }

        std::string full_old = GetFullPath(old_path);
        std::string full_new = GetFullPath(new_path);

        if (rename(full_old.c_str(), full_new.c_str()) != 0) {
            ESP_LOGE(TAG, "Failed to rename %s to %s (%s)", full_old.c_str(), full_new.c_str(), std::strerror(errno));
            return false;
        }

        ESP_LOGI(TAG, "Renamed %s to %s", full_old.c_str(), full_new.c_str());
        return true;
    }

    bool Exists(const std::string& path) override {
        if (!is_mounted_) {
            return false;
        }

        std::string full_path = GetFullPath(path);
        struct stat st;
        return stat(full_path.c_str(), &st) == 0;
    }

    size_t GetFileSize(const std::string& path) override {
        if (!is_mounted_) {
            return 0;
        }

        std::string full_path = GetFullPath(path);
        struct stat st;
        if (stat(full_path.c_str(), &st) != 0) {
            return 0;
        }

        return st.st_size;
    }

private:
    bool is_mounted_ = false;
    sdmmc_card_t* card_ = nullptr;
    std::string mount_point_ = kMountPoint;
    bool board_ref_acquired_ = false;

    void ReleaseBoardRef() {
        if (!board_ref_acquired_) {
            return;
        }
        esp_err_t ret = esp_board_manager_deinit_device_by_name(kSdCardDeviceName);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to release SD card device (%s)", esp_err_to_name(ret));
        }
        board_ref_acquired_ = false;
    }

    std::string GetFullPath(const std::string& path) const {
        if (path.empty() || path == "/") {
            return mount_point_;
        }
        if (path == mount_point_ ||
            path.compare(0, mount_point_.size() + 1, mount_point_ + "/") == 0) {
            return path;
        }
        if (path[0] == '/') {
            return mount_point_ + path;
        }
        return mount_point_ + "/" + path;
    }
};

}  // namespace

FileService* CreateFileService() {
    return new FileServiceImpl();
}

} // namespace rodakos
