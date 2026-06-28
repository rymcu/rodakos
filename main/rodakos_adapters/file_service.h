#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace rodakos {

/**
 * @brief File entry information
 */
struct FileEntry {
    std::string name;        ///< File name
    std::string path;        ///< Full path
    bool is_directory;       ///< Is directory
    size_t size;            ///< File size in bytes
    uint64_t modified_time; ///< Modified time (Unix timestamp)
};

/**
 * @brief File service for SD card management
 *
 * Based on Brookesia HAL StorageFsIface design pattern.
 * Provides high-level file operations on top of mounted file systems.
 */
class FileService {
public:
    /**
     * @brief Storage medium type
     */
    enum class MediumType {
        Flash,   ///< Internal flash
        SDCard,  ///< SD/TF card
    };

    /**
     * @brief File system type
     */
    enum class FileSystemType {
        SPIFFS,   ///< SPIFFS
        FATFS,    ///< FAT file system
        LittleFS, ///< LittleFS
    };

    /**
     * @brief File system capacity information
     */
    struct Capacity {
        uint64_t total_bytes = 0; ///< Total size
        uint64_t used_bytes = 0;  ///< Used size
        uint64_t free_bytes = 0;  ///< Free size
    };

    virtual ~FileService() = default;

    /**
     * @brief Initialize file service (mount SD card)
     * @return true if successful
     */
    virtual bool Init() = 0;

    /**
     * @brief Deinitialize (unmount SD card)
     */
    virtual void Deinit() = 0;

    /**
     * @brief Check if SD card is mounted
     */
    virtual bool IsMounted() const = 0;

    /**
     * @brief Get mount point path
     */
    virtual const char* GetMountPoint() const = 0;

    /**
     * @brief Get file system type
     */
    virtual FileSystemType GetFileSystemType() const = 0;

    /**
     * @brief Get storage medium type
     */
    virtual MediumType GetMediumType() const = 0;

    /**
     * @brief Get capacity information
     * @param capacity Output capacity info
     * @return true if successful
     */
    virtual bool GetCapacity(Capacity& capacity) = 0;

    /**
     * @brief List directory contents
     * @param path Directory path (relative to mount point)
     * @param entries Output file entries
     * @return true if successful
     */
    virtual bool ListDirectory(const std::string& path, std::vector<FileEntry>& entries) = 0;

    /**
     * @brief Read file contents
     * @param path File path
     * @param data Output file data
     * @return true if successful
     */
    virtual bool ReadFile(const std::string& path, std::vector<uint8_t>& data) = 0;

    /**
     * @brief Write file contents
     * @param path File path
     * @param data File data
     * @param append Append mode (default: overwrite)
     * @return true if successful
     */
    virtual bool WriteFile(const std::string& path, const std::vector<uint8_t>& data, bool append = false) = 0;

    /**
     * @brief Delete file
     * @param path File path
     * @return true if successful
     */
    virtual bool DeleteFile(const std::string& path) = 0;

    /**
     * @brief Delete directory recursively
     * @param path Directory path
     * @return true if successful
     */
    virtual bool DeleteDirectory(const std::string& path) = 0;

    /**
     * @brief Create directory
     * @param path Directory path
     * @return true if successful
     */
    virtual bool CreateDirectory(const std::string& path) = 0;

    /**
     * @brief Rename file or directory
     * @param old_path Old path
     * @param new_path New path
     * @return true if successful
     */
    virtual bool Rename(const std::string& old_path, const std::string& new_path) = 0;

    /**
     * @brief Check if file or directory exists
     * @param path Path to check
     * @return true if exists
     */
    virtual bool Exists(const std::string& path) = 0;

    /**
     * @brief Get file size
     * @param path File path
     * @return File size in bytes, 0 if failed
     */
    virtual size_t GetFileSize(const std::string& path) = 0;
};

/**
 * @brief Create file service instance
 * @return Pointer to file service (caller owns)
 */
FileService* CreateFileService();

} // namespace rodakos
