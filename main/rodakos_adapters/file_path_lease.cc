#include "rodakos_adapters/file_path_lease.h"

#include <cctype>

namespace rodakos {
namespace {

char NormalizeAscii(char value) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

void AppendSegment(std::string& output, std::string_view segment) {
    if (segment.empty() || segment == ".") {
        return;
    }
    if (output.size() > 1) {
        output.push_back('/');
    }
    for (const char value : segment) {
        output.push_back(NormalizeAscii(value));
    }
}

bool NormalizeAbsolute(std::string_view path, std::string& normalized) {
    normalized = "/";
    size_t start = 0;
    while (start <= path.size()) {
        const size_t separator = path.find('/', start);
        const size_t end = separator == std::string_view::npos ? path.size() : separator;
        const std::string_view segment = path.substr(start, end - start);
        if (segment == "..") {
            return false;
        }
        AppendSegment(normalized, segment);
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return true;
}

}  // namespace

bool NormalizeStoragePath(std::string_view path, std::string_view mount_point,
                          std::string& normalized) {
    std::string raw;
    raw.reserve(path.size());
    for (const char value : path) {
        if (value == '\0') {
            return false;
        }
        raw.push_back(value == '\\' ? '/' : value);
    }

    for (const char value : mount_point) {
        if (value == '\0') {
            return false;
        }
    }

    std::string canonical_mount;
    if (!NormalizeAbsolute(mount_point, canonical_mount)) {
        return false;
    }

    std::string canonical_input;
    std::string canonical_input_source;
    if (raw.empty()) {
        canonical_input = "/";
    } else if (raw.front() == '/') {
        canonical_input_source = raw;
        if (!NormalizeAbsolute(canonical_input_source, canonical_input)) {
            return false;
        }
    } else {
        canonical_input_source = "/" + raw;
        if (!NormalizeAbsolute(canonical_input_source, canonical_input)) {
            return false;
        }
    }

    if (canonical_input == canonical_mount) {
        normalized = "/";
        return true;
    }
    const std::string mount_prefix = canonical_mount == "/" ? "/" : canonical_mount + "/";
    if (canonical_input.rfind(mount_prefix, 0) == 0) {
        normalized = canonical_mount == "/"
            ? canonical_input
            : canonical_input.substr(canonical_mount.size());
    } else if (!raw.empty() && raw.front() == '/') {
        // A leading slash is mount-relative in FileService ("/photos"),
        // unless it explicitly names the configured mount point.
        normalized = canonical_input;
    } else {
        normalized = canonical_input;
    }

    if (normalized.empty() || normalized.front() != '/') {
        normalized.insert(normalized.begin(), '/');
    }
    return true;
}

bool StoragePathsConflict(std::string_view left, std::string_view right) {
    const auto is_prefix = [](std::string_view prefix, std::string_view value) {
        return value == prefix ||
               (value.size() > prefix.size() && value.rfind(prefix, 0) == 0 &&
                prefix != "/" && value[prefix.size()] == '/') ||
               (prefix == "/" && !value.empty() && value.front() == '/');
    };
    return is_prefix(left, right) || is_prefix(right, left);
}

bool StoragePathLeaseSet::TryAcquire(const std::string& normalized_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& active : active_paths_) {
        if (StoragePathsConflict(active, normalized_path)) {
            return false;
        }
    }
    active_paths_.insert(normalized_path);
    return true;
}

bool StoragePathLeaseSet::Conflicts(const std::string& normalized_path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& active : active_paths_) {
        if (StoragePathsConflict(active, normalized_path)) {
            return true;
        }
    }
    return false;
}

void StoragePathLeaseSet::Release(const std::string& normalized_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_paths_.erase(normalized_path);
}

}  // namespace rodakos
