#pragma once

#include "apps/home/home_layout_model.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace rodakos {

enum class HomeLayoutDecodeStatus {
    kOk,
    kTooLarge,
    kMalformed,
    kUnsupportedVersion,
};

struct HomeLayoutDecodeResult {
    HomeLayoutDecodeStatus status = HomeLayoutDecodeStatus::kMalformed;
    HomeLayout layout;
    uint32_t source_version = 0;
};

HomeLayoutDecodeResult DecodeHomeLayout(std::string_view json);

enum class HomeLayoutEncodeStatus {
    kOk,
    kInvalidModel,
    kTooLarge,
    kOutOfMemory,
};

struct HomeLayoutEncodeResult {
    HomeLayoutEncodeStatus status = HomeLayoutEncodeStatus::kInvalidModel;
    std::string json;
};

HomeLayoutEncodeResult EncodeHomeLayout(const HomeLayout& layout);

}  // namespace rodakos
