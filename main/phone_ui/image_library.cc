#include "image_library.h"
#include "rodakos_adapters/file_service.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "jpg/jpeg_to_image.h"

namespace rodakos {

namespace {
constexpr const char* TAG = "ImageLibrary";
constexpr size_t kDisplayWidth = 320;
constexpr size_t kDisplayHeight = 240;
constexpr char kLvglStdioDriveLetter = 'S';

uint16_t ReadBe16(const uint8_t* data) {
    return (static_cast<uint16_t>(data[0]) << 8) | data[1];
}

uint32_t ReadBe32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           data[3];
}

uint32_t ReadLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool EndsWith(const std::string& str, const char* suffix) {
    std::string lower = ToLower(str);
    size_t suffix_len = std::strlen(suffix);
    return lower.size() >= suffix_len &&
           lower.compare(lower.size() - suffix_len, suffix_len, suffix) == 0;
}

bool IsJpeg(const std::string& filename) {
    return EndsWith(filename, ".jpg") || EndsWith(filename, ".jpeg");
}

std::string ToLvglStdioPath(const std::string& path) {
    if (path.size() >= 2 && path[1] == ':') {
        return path;
    }
    std::string lvgl_path;
    lvgl_path.reserve(path.size() + 2);
    lvgl_path.push_back(kLvglStdioDriveLetter);
    lvgl_path.push_back(':');
    lvgl_path += path;
    return lvgl_path;
}

bool ParsePngSize(const uint8_t* data, size_t size, int* width, int* height) {
    static constexpr uint8_t kPngSignature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (size < 24 || std::memcmp(data, kPngSignature, sizeof(kPngSignature)) != 0) {
        return false;
    }

    *width = static_cast<int>(ReadBe32(data + 16));
    *height = static_cast<int>(ReadBe32(data + 20));
    return *width > 0 && *height > 0;
}

bool ParseBmpSize(const uint8_t* data, size_t size, int* width, int* height) {
    if (size < 26 || data[0] != 'B' || data[1] != 'M') {
        return false;
    }

    *width = static_cast<int>(ReadLe32(data + 18));
    int32_t signed_height = static_cast<int32_t>(ReadLe32(data + 22));
    if (signed_height < 0) {
        signed_height = -signed_height;
    }
    *height = signed_height;
    return *width > 0 && *height > 0;
}

bool ParseJpegSize(const uint8_t* data, size_t size, int* width, int* height) {
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        return false;
    }

    size_t offset = 2;
    while (offset + 4 < size) {
        while (offset < size && data[offset] != 0xFF) {
            offset++;
        }
        while (offset < size && data[offset] == 0xFF) {
            offset++;
        }
        if (offset >= size) {
            break;
        }

        const uint8_t marker = data[offset++];
        if (marker == 0xD9 || marker == 0xDA) {
            break;
        }
        if (offset + 2 > size) {
            break;
        }

        const uint16_t segment_len = ReadBe16(data + offset);
        if (segment_len < 2 || offset + segment_len > size) {
            break;
        }

        const bool is_sof =
            (marker >= 0xC0 && marker <= 0xC3) ||
            (marker >= 0xC5 && marker <= 0xC7) ||
            (marker >= 0xC9 && marker <= 0xCB) ||
            (marker >= 0xCD && marker <= 0xCF);
        if (is_sof && segment_len >= 7) {
            *height = static_cast<int>(ReadBe16(data + offset + 3));
            *width = static_cast<int>(ReadBe16(data + offset + 5));
            return *width > 0 && *height > 0;
        }

        offset += segment_len;
    }

    return false;
}

bool ParseImageSize(const uint8_t* data, size_t size, int* width, int* height) {
    return ParsePngSize(data, size, width, height) ||
           ParseJpegSize(data, size, width, height) ||
           ParseBmpSize(data, size, width, height);
}

void ScanDirectory(FileService* fs, const std::string& dir, int depth, int max_depth,
                  std::vector<std::string>& files) {
    if (depth > max_depth || fs == nullptr) {
        return;
    }

    std::vector<FileEntry> entries;
    if (!fs->ListDirectory(dir, entries)) {
        return;
    }

    for (const auto& entry : entries) {
        if (entry.is_directory) {
            ScanDirectory(fs, entry.path, depth + 1, max_depth, files);
        } else if (ImageLibrary::IsSupportedImage(entry.name)) {
            files.push_back(entry.path);
        }
    }
}

}  // namespace

// LvglAllocatedImage implementation
LvglAllocatedImage::LvglAllocatedImage(void* data, size_t size) {
    std::memset(&image_dsc_, 0, sizeof(image_dsc_));
    image_dsc_.data = static_cast<const uint8_t*>(data);
    image_dsc_.data_size = size;

    if (lv_image_decoder_get_info(&image_dsc_, &image_dsc_.header) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "Failed to get image info, data=%p size=%u", data, static_cast<unsigned>(size));
        throw std::runtime_error("Failed to get image info");
    }
}

LvglAllocatedImage::LvglAllocatedImage(void* data, size_t size, int width, int height,
                                       int stride, lv_color_format_t format) {
    std::memset(&image_dsc_, 0, sizeof(image_dsc_));
    image_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_dsc_.header.cf = format;
    image_dsc_.header.flags = 0;
    image_dsc_.header.w = width;
    image_dsc_.header.h = height;
    image_dsc_.header.stride = stride;
    image_dsc_.data = static_cast<const uint8_t*>(data);
    image_dsc_.data_size = size;
}

LvglAllocatedImage::~LvglAllocatedImage() {
    if (image_dsc_.data != nullptr) {
        heap_caps_free(const_cast<uint8_t*>(image_dsc_.data));
    }
}

LvglFileImage::LvglFileImage(std::string lvgl_path) : lvgl_path_(std::move(lvgl_path)) {}

// ImageLibrary namespace functions
namespace ImageLibrary {

std::string Basename(const std::string& path) {
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

bool IsSupportedImage(const std::string& filename) {
    return EndsWith(filename, ".png") ||
           EndsWith(filename, ".jpg") ||
           EndsWith(filename, ".jpeg") ||
           EndsWith(filename, ".bmp");
}

bool IsMemoryRenderableImage(const std::string& filename) {
    return EndsWith(filename, ".png") ||
           IsJpeg(filename);
}

bool IsFileRenderableImage(const std::string& filename) {
    return EndsWith(filename, ".bmp");
}

std::vector<std::string> ScanImages(const std::string& directory, int max_depth) {
    // Note: This function needs FileService instance
    // For now, return empty vector - will be called from PhotosApp with context
    std::vector<std::string> files;
    return files;
}

std::shared_ptr<LvglImage> LoadImage(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) {
        ESP_LOGW(TAG, "Failed to open image: %s", path.c_str());
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(f);
        return nullptr;
    }

    // Allocate in SPIRAM
    void* data = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == nullptr) {
        // Fallback to internal RAM if SPIRAM not available
        data = heap_caps_malloc(file_size, MALLOC_CAP_8BIT);
        if (data == nullptr) {
            fclose(f);
            ESP_LOGW(TAG, "Failed to allocate memory for image, size=%ld", file_size);
            return nullptr;
        }
    }

    size_t read_size = fread(data, 1, file_size, f);
    fclose(f);

    if (read_size != static_cast<size_t>(file_size)) {
        heap_caps_free(data);
        ESP_LOGW(TAG, "Failed to read image completely: %s", path.c_str());
        return nullptr;
    }

    int width = 0;
    int height = 0;
    if (!ParseImageSize(static_cast<const uint8_t*>(data), static_cast<size_t>(file_size),
                        &width, &height)) {
        ESP_LOGW(TAG, "Failed to parse image size: %s", path.c_str());
    } else {
        ESP_LOGI(TAG, "Loaded image: %s (%dx%d, %ld bytes)", path.c_str(), width, height, file_size);
    }

    if (IsJpeg(path)) {
        uint8_t* decoded = nullptr;
        size_t decoded_len = 0;
        size_t decoded_width = 0;
        size_t decoded_height = 0;
        size_t decoded_stride = 0;
        esp_err_t ret = jpeg_to_image_scaled(
            static_cast<const uint8_t*>(data), static_cast<size_t>(file_size),
            &decoded, &decoded_len, &decoded_width, &decoded_height, &decoded_stride,
            kDisplayWidth, kDisplayHeight);
        heap_caps_free(data);

        if (ret != ESP_OK || decoded == nullptr || decoded_len == 0 ||
            decoded_width == 0 || decoded_height == 0 || decoded_stride == 0) {
            if (decoded != nullptr) {
                heap_caps_free(decoded);
            }
            ESP_LOGW(TAG, "Failed to decode JPEG: %s (%s)", path.c_str(), esp_err_to_name(ret));
            return nullptr;
        }

        ESP_LOGI(TAG, "Decoded JPEG: %s (%ux%u, stride=%u, %u bytes)",
                 path.c_str(),
                 static_cast<unsigned>(decoded_width),
                 static_cast<unsigned>(decoded_height),
                 static_cast<unsigned>(decoded_stride),
                 static_cast<unsigned>(decoded_len));
        try {
            return std::make_shared<LvglAllocatedImage>(
                decoded, decoded_len, static_cast<int>(decoded_width), static_cast<int>(decoded_height),
                static_cast<int>(decoded_stride), LV_COLOR_FORMAT_RGB888);
        } catch (...) {
            heap_caps_free(decoded);
            ESP_LOGW(TAG, "Failed to create decoded JPEG image: %s", path.c_str());
            return nullptr;
        }
    }

    try {
        return std::make_shared<LvglAllocatedImage>(data, file_size);
    } catch (...) {
        heap_caps_free(data);
        ESP_LOGW(TAG, "Failed to create image: %s", path.c_str());
        return nullptr;
    }
}

std::shared_ptr<LvglImage> LoadImageForDisplay(const std::string& path) {
    if (IsFileRenderableImage(path)) {
        std::string lvgl_path = ToLvglStdioPath(path);
        ESP_LOGI(TAG, "Using LVGL file source for image: %s", lvgl_path.c_str());
        return std::make_shared<LvglFileImage>(std::move(lvgl_path));
    }

    if (!IsMemoryRenderableImage(path)) {
        ESP_LOGW(TAG, "Unsupported image display format: %s", path.c_str());
        return nullptr;
    }

    return LoadImage(path);
}

std::vector<std::string> ScanImagesWithFileService(FileService* fs, const std::string& directory, int max_depth) {
    std::vector<std::string> files;
    if (fs == nullptr || !fs->IsMounted()) {
        return files;
    }

    ScanDirectory(fs, directory, 0, max_depth, files);

    // Sort by name
    std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
        return ToLower(a) < ToLower(b);
    });

    return files;
}

}  // namespace ImageLibrary

}  // namespace rodakos
