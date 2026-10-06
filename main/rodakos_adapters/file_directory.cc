#include "rodakos_adapters/file_directory.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <utility>

namespace rodakos {

bool ReadFileDirectory(const std::string& full_path, std::vector<FileEntry>& entries) {
    entries.clear();
    DIR* directory = opendir(full_path.c_str());
    if (directory == nullptr) {
        return false;
    }

    std::vector<FileEntry> scanned;
    int read_error = 0;
    while (true) {
        // readdir uses nullptr for both end-of-directory and I/O failure.
        errno = 0;
        const dirent* item = readdir(directory);
        if (item == nullptr) {
            read_error = errno;
            break;
        }
        if (std::strcmp(item->d_name, ".") == 0 || std::strcmp(item->d_name, "..") == 0) {
            continue;
        }

        FileEntry entry{};
        entry.name = item->d_name;
        entry.path = full_path + "/" + entry.name;
        struct stat info{};
        if (stat(entry.path.c_str(), &info) != 0) {
            read_error = errno != 0 ? errno : EIO;
            break;
        }
        entry.is_directory = S_ISDIR(info.st_mode);
        entry.size = info.st_size;
        entry.modified_time = info.st_mtime;
        scanned.push_back(std::move(entry));
    }

    const int close_result = closedir(directory);
    if (close_result != 0 && read_error == 0) {
        read_error = errno != 0 ? errno : EIO;
    }
    if (read_error != 0) {
        errno = read_error;
        return false;
    }

    std::sort(scanned.begin(), scanned.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.is_directory != b.is_directory) {
            return a.is_directory;
        }
        return a.name < b.name;
    });
    entries = std::move(scanned);
    return true;
}

}  // namespace rodakos
