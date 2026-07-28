#pragma once

#include "apps/wol/wol_device_codec.h"

#include <vector>

namespace rodakos {

enum class WolDeviceLoadStatus {
    kMissing,
    kLoaded,
    kCorrupt,
    kTooLarge,
    kUnsupportedVersion,
    kStorageError,
};

struct WolDeviceLoadResult {
    WolDeviceLoadStatus status = WolDeviceLoadStatus::kStorageError;
    std::vector<WolDevice> devices;
    bool write_allowed = false;
};

class WolDeviceStore {
public:
    WolDeviceLoadResult Load();
    bool Save(const std::vector<WolDevice>& devices);
    bool Reset();

    bool write_allowed() const { return loaded_ && write_allowed_; }
    bool reset_allowed() const { return loaded_ && reset_allowed_; }

private:
    bool loaded_ = false;
    bool write_allowed_ = false;
    bool reset_allowed_ = false;
};

}  // namespace rodakos
