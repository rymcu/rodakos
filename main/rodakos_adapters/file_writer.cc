#include "rodakos_adapters/file_writer.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

namespace rodakos {

bool WriteFileBytes(const std::string& full_path, const std::vector<uint8_t>& data,
                    FileWriteMode mode) {
    FILE* file = nullptr;
    if (mode == FileWriteMode::kCreateNew) {
        const int fd = open(full_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0) {
            return false;
        }
        file = fdopen(fd, "wb");
        if (file == nullptr) {
            const int first_error = errno != 0 ? errno : EIO;
            close(fd);
            unlink(full_path.c_str());
            errno = first_error;
            return false;
        }
    } else if (mode == FileWriteMode::kReplace || mode == FileWriteMode::kAppend) {
        file = std::fopen(full_path.c_str(), mode == FileWriteMode::kAppend ? "ab" : "wb");
        if (file == nullptr) {
            return false;
        }
    } else {
        errno = EINVAL;
        return false;
    }

    int first_error = 0;
    errno = 0;
    if ((!data.empty() && std::fwrite(data.data(), 1, data.size(), file) != data.size()) ||
        std::ferror(file)) {
        first_error = errno != 0 ? errno : EIO;
    }
    if (first_error == 0) {
        errno = 0;
        if (std::fflush(file) != 0) {
            first_error = errno != 0 ? errno : EIO;
        }
    }
    errno = 0;
    if (std::fclose(file) != 0 && first_error == 0) {
        first_error = errno != 0 ? errno : EIO;
    }
    if (first_error != 0) {
        // Replace/append may have changed user data already; never unlink it.
        // A failed cleanup can leave the new partial file, but cannot become success.
        if (mode == FileWriteMode::kCreateNew) {
            unlink(full_path.c_str());
        }
        errno = first_error;
        return false;
    }
    return true;
}

}  // namespace rodakos
