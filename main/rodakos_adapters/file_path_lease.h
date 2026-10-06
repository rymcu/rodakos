#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

namespace rodakos {

// Normalizes the mount-relative spelling used by FileService. FAT paths are
// compared case-insensitively for ASCII names and accept either slash style.
// A parent traversal is never accepted.
bool NormalizeStoragePath(std::string_view path, std::string_view mount_point,
                          std::string& normalized);

bool StoragePathsConflict(std::string_view left, std::string_view right);

class StoragePathLeaseSet {
public:
    bool TryAcquire(const std::string& normalized_path);
    bool Conflicts(const std::string& normalized_path) const;
    void Release(const std::string& normalized_path);

private:
    mutable std::mutex mutex_;
    std::unordered_set<std::string> active_paths_;
};

}  // namespace rodakos
