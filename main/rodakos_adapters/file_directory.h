#pragma once

#include "rodakos_adapters/file_service.h"

namespace rodakos {

// Caller owns mount lifetime and serializes storage access. Failure leaves no partial entries.
bool ReadFileDirectory(const std::string& full_path, std::vector<FileEntry>& entries);

}  // namespace rodakos
