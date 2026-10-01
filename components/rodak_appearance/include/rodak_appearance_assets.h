#pragma once

#include "rodak_appearance.h"

#include <memory>
#include <vector>

namespace rodakos {
struct AppearanceBootAssets {
    AppearancePackageMetadata metadata;
    std::vector<std::shared_ptr<uint8_t>> buffers;
    uint32_t revision = 0;
    const uint8_t* ResourceData(const AppearanceResource& resource) const {
        for (size_t i = 0; i < metadata.resources.size(); ++i) {
            if (metadata.resources[i].id == resource.id && i < buffers.size()) return buffers[i].get();
        }
        return nullptr;
    }
    std::shared_ptr<uint8_t> WallpaperBuffer() const {
        for (size_t i = 0; i < metadata.resources.size(); ++i) {
            if (metadata.resources[i].id == metadata.wallpaper_resource_id && i < buffers.size()) return buffers[i];
        }
        return {};
    }
};
}  // namespace rodakos
