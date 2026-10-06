#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rodakos {

enum class FileWriteMode {
    kReplace,
    kAppend,
    kCreateNew,
};

// Success includes normal stream flush/close, not power-loss durability. On failure,
// errno retains the first error; only a file created by kCreateNew is removed.
bool WriteFileBytes(const std::string& full_path, const std::vector<uint8_t>& data,
                    FileWriteMode mode);

}  // namespace rodakos
