#include "phone_os/resource_failure_injection.h"
#include "image_library.h"
#include "rodakos_adapters/file_service.h"

#include <esp_jpeg_common.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

#include "jpg/jpeg_to_image.h"
#include <src/draw/lv_image_decoder_private.h>
#include <src/misc/cache/instance/lv_image_header_cache.h>

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
    if (signed_height == INT32_MIN) return false;
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

bool ScanDirectory(FileService* fs, const std::string& dir, int depth, int max_depth,
                   ImageLibrary::ImageScanResult& result) {
    if (depth > max_depth) return true;
    std::vector<FileEntry> entries;
    errno = 0;
    if (!fs->ListDirectory(dir, entries)) {
        const int failure = errno;
        result.status = failure == ENODEV ? ImageLibrary::ImageScanStatus::kStorageUnavailable
            : failure == ENOENT ? ImageLibrary::ImageScanStatus::kDirectoryMissing
                               : ImageLibrary::ImageScanStatus::kReadFailed;
        result.failed_path = dir;
        return false;
    }

    for (const auto& entry : entries) {
        if (entry.is_directory) {
            if (!ScanDirectory(fs, entry.path, depth + 1, max_depth, result)) return false;
        } else if (ImageLibrary::IsSupportedImage(entry.name)) {
            result.paths.push_back(entry.path);
        }
    }
    return true;
}

}  // namespace

// LvglAllocatedImage implementation
LvglAllocatedImage::LvglAllocatedImage(void* data, size_t size)
    : LvglAllocatedImage(data, size, nullptr) {}

LvglAllocatedImage::LvglAllocatedImage(void* data, size_t size, FreeFunc free_func)
    : free_func_(free_func) {
    std::memset(&image_dsc_, 0, sizeof(image_dsc_));
    image_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_dsc_.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    image_dsc_.header.flags = 0;
    image_dsc_.data = static_cast<const uint8_t*>(data);
    image_dsc_.data_size = size;

    lv_image_header_t decoded_header{};
    if (lv_image_decoder_get_info(&image_dsc_, &decoded_header) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "Failed to get image info, data=%p size=%u", data, static_cast<unsigned>(size));
        throw std::runtime_error("Failed to get image info");
    }
    // Keep the encoded format; marking compressed PNG bytes as ARGB skips decoding on draw.
    image_dsc_.header.w = decoded_header.w;
    image_dsc_.header.h = decoded_header.h;
}

LvglAllocatedImage::LvglAllocatedImage(void* data, size_t size, int width, int height,
                                       int stride, lv_color_format_t format, FreeFunc free_func)
    : free_func_(free_func) {
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
    lv_image_cache_drop(&image_dsc_);
    if (image_dsc_.data != nullptr) {
        void* data = const_cast<uint8_t*>(image_dsc_.data);
        if (free_func_ != nullptr) {
            free_func_(data);
        } else {
            heap_caps_free(data);
        }
    }
}

LvglFileImage::LvglFileImage(std::string lvgl_path) : lvgl_path_(std::move(lvgl_path)) {}
LvglFileImage::~LvglFileImage() { lv_image_cache_drop(lvgl_path_.c_str()); }

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

namespace {
ImageLoadResult LoadMemoryImage(const std::string& path, size_t width, size_t height) {
    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) return {ImageLoadStatus::kReadFailed, {}};
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return {ImageLoadStatus::kReadFailed, {}};
    }
    const long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return {ImageLoadStatus::kReadFailed, {}};
    }
    if (size == 0) {
        fclose(file);
        return {ImageLoadStatus::kDecodeFailed, {}};
    }
    const bool injected = FailResource(ResourceFailure::kImage);
    void* data = injected ? nullptr : heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == nullptr && !injected) data = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (data == nullptr) {
        fclose(file);
        return {ImageLoadStatus::kInsufficientMemory, {}};
    }
    const size_t count = fread(data, 1, size, file);
    const bool read_ok = count == static_cast<size_t>(size) && ferror(file) == 0;
    const int close_result = fclose(file);
    if (!read_ok || close_result != 0) {
        heap_caps_free(data);
        return {ImageLoadStatus::kReadFailed, {}};
    }
    int parsed_width = 0, parsed_height = 0;
    if (!ParseImageSize(static_cast<const uint8_t*>(data), size, &parsed_width, &parsed_height)) {
        heap_caps_free(data);
        return {ImageLoadStatus::kDecodeFailed, {}};
    }
    if (IsJpeg(path)) {
        uint8_t* decoded = nullptr;
        size_t length = 0, decoded_width = 0, decoded_height = 0, stride = 0;
        const esp_err_t result = jpeg_to_image_scaled(static_cast<const uint8_t*>(data), size,
            &decoded, &length, &decoded_width, &decoded_height, &stride, width, height);
        heap_caps_free(data);
        if (result != ESP_OK || decoded == nullptr || length == 0 || decoded_width == 0 ||
            decoded_height == 0 || stride == 0) {
            if (decoded != nullptr) jpeg_free_align(decoded);
            return {result == ESP_ERR_NO_MEM ? ImageLoadStatus::kInsufficientMemory
                                             : ImageLoadStatus::kDecodeFailed, {}};
        }
        try {
            return {ImageLoadStatus::kLoaded, std::make_shared<LvglAllocatedImage>(decoded, length,
                static_cast<int>(decoded_width), static_cast<int>(decoded_height),
                static_cast<int>(stride), LV_COLOR_FORMAT_RGB888, jpeg_free_align)};
        } catch (const std::bad_alloc&) {
            jpeg_free_align(decoded);
            return {ImageLoadStatus::kInsufficientMemory, {}};
        } catch (...) {
            jpeg_free_align(decoded);
            return {ImageLoadStatus::kDecodeFailed, {}};
        }
    }
    std::shared_ptr<LvglImage> image;
    try {
        image = std::make_shared<LvglAllocatedImage>(data, size);
    } catch (const std::bad_alloc&) {
        heap_caps_free(data);
        return {ImageLoadStatus::kInsufficientMemory, {}};
    } catch (...) {
        heap_caps_free(data);
        return {ImageLoadStatus::kDecodeFailed, {}};
    }
    lv_image_decoder_dsc_t decoder{};
    lv_image_decoder_args_t args{};
    args.no_cache = true;
    // Header inspection alone accepts truncated PNGs; exercise the actual decoder.
    if (lv_image_decoder_open(&decoder, image->GetImageSource(), &args) != LV_RESULT_OK)
        return {ImageLoadStatus::kDecodeFailed, {}};
    const bool decoded = decoder.decoded != nullptr;
    lv_image_decoder_close(&decoder);
    if (!decoded) return {ImageLoadStatus::kDecodeFailed, {}};
    return {ImageLoadStatus::kLoaded, std::move(image)};
}
void SortPaths(ImageScanResult& result) {
    std::sort(result.paths.begin(), result.paths.end(), [](const auto& left, const auto& right) {
        return ToLower(left) < ToLower(right);
    });
    result.paths.erase(std::unique(result.paths.begin(), result.paths.end()), result.paths.end());
}
}  // namespace

ImageLoadResult LoadImageForDisplayDetailed(const std::string& path) {
    if (!IsSupportedImage(path)) return {ImageLoadStatus::kUnsupported, {}};
    if (!IsFileRenderableImage(path)) return LoadMemoryImage(path, kDisplayWidth, kDisplayHeight);
    std::unique_ptr<FILE, decltype(&fclose)> file(fopen(path.c_str(), "rb"), fclose);
    if (file == nullptr) return {ImageLoadStatus::kReadFailed, {}};
    uint8_t header_bytes[54]{};
    const size_t count = fread(header_bytes, 1, sizeof(header_bytes), file.get());
    uint8_t masks[12]{};
    const bool bitfields = header_bytes[28] == 16 && ReadLe32(header_bytes + 30) == 3;
    const size_t mask_count = bitfields ? fread(masks, 1, sizeof(masks), file.get()) : 0;
    const bool read_error = ferror(file.get()) != 0;
    const int seek_result = fseek(file.get(), 0, SEEK_END);
    const long file_size = seek_result == 0 ? ftell(file.get()) : -1;
    if (read_error || file_size < 0) return {ImageLoadStatus::kReadFailed, {}};
    if (count != sizeof(header_bytes) || header_bytes[0] != 'B' || header_bytes[1] != 'M')
        return {ImageLoadStatus::kDecodeFailed, {}};
    const uint32_t dib_size = ReadLe32(header_bytes + 14);
    const uint32_t width = ReadLe32(header_bytes + 18);
    const uint32_t height = ReadLe32(header_bytes + 22);
    const uint16_t planes = header_bytes[26] | static_cast<uint16_t>(header_bytes[27]) << 8;
    const uint16_t bits = header_bytes[28] | static_cast<uint16_t>(header_bytes[29]) << 8;
    const uint32_t compression = ReadLe32(header_bytes + 30);
    const uint32_t offset = ReadLe32(header_bytes + 10);
    const uint32_t declared_size = ReadLe32(header_bytes + 2);
    if (dib_size < 40 || width == 0 || height == 0 || width > INT16_MAX || height > INT16_MAX ||
        planes != 1 || offset < 14ULL + dib_size) return {ImageLoadStatus::kDecodeFailed, {}};
    // LVGL's streaming decoder supports direct pixels, not palettes, RLE or top-down rows.
    const bool rgb565 = bits == 16 && compression == 3 && mask_count == sizeof(masks) &&
        offset >= 66 && ReadLe32(masks) == 0xf800 && ReadLe32(masks + 4) == 0x7e0 &&
        ReadLe32(masks + 8) == 0x1f;
    if (!rgb565 && ((bits != 24 && bits != 32) || compression != 0))
        return {ImageLoadStatus::kUnsupported, {}};
    const uint64_t row_bytes = ((static_cast<uint64_t>(width) * bits + 31) / 32) * 4;
    const uint64_t required = offset + row_bytes * height;
    if (required > static_cast<uint64_t>(file_size) || declared_size < required ||
        declared_size > static_cast<uint64_t>(file_size)) return {ImageLoadStatus::kDecodeFailed, {}};
    if (fseek(file.get(), static_cast<long>(offset), SEEK_SET) != 0)
        return {ImageLoadStatus::kReadFailed, {}};
    uint8_t pixels[512];
    for (uint64_t remaining = required - offset; remaining > 0;) {
        const size_t expected = static_cast<size_t>(std::min<uint64_t>(remaining, sizeof(pixels)));
        if (fread(pixels, 1, expected, file.get()) != expected || ferror(file.get()) != 0)
            return {ImageLoadStatus::kReadFailed, {}};
        remaining -= expected;
    }
    if (fclose(file.release()) != 0) return {ImageLoadStatus::kReadFailed, {}};
    try {
        auto image = std::make_shared<LvglFileImage>(ToLvglStdioPath(path));
        lv_image_header_cache_drop(image->GetImageSource());
        lv_image_header_t header = {};
        if (lv_image_decoder_get_info(image->GetImageSource(), &header) != LV_RESULT_OK ||
            header.w != width || header.h != height) return {ImageLoadStatus::kDecodeFailed, {}};
        return {ImageLoadStatus::kLoaded, std::move(image)};
    } catch (const std::bad_alloc&) {
        return {ImageLoadStatus::kInsufficientMemory, {}};
    }
}
ImageLoadResult LoadThumbnailDetailed(const std::string& path, int width, int height) {
    if (!IsJpeg(path) || width <= 0 || height <= 0) return {ImageLoadStatus::kUnsupported, {}};
    return LoadMemoryImage(path, width, height);
}
const char* ImageLoadErrorText(ImageLoadStatus status) {
    switch (status) {
        case ImageLoadStatus::kLoaded: return "";
        case ImageLoadStatus::kUnsupported: return "Unsupported image format";
        case ImageLoadStatus::kReadFailed: return "Could not read image";
        case ImageLoadStatus::kInsufficientMemory: return "Not enough image memory";
        default: return "Could not load image";
    }
}
std::shared_ptr<LvglImage> LoadImage(const std::string& path) {
    return LoadMemoryImage(path, kDisplayWidth, kDisplayHeight).image;
}
std::shared_ptr<LvglImage> LoadImageForDisplay(const std::string& path) {
    return LoadImageForDisplayDetailed(path).image;
}
std::shared_ptr<LvglImage> LoadThumbnail(const std::string& path, int width, int height) {
    return LoadThumbnailDetailed(path, width, height).image;
}
std::vector<std::string> ScanImages(const std::string&, int) {
    return {};
}
ImageScanResult ScanImagesWithFileServiceDetailed(FileService* fs, const std::string& directory, int max_depth) {
    if (fs == nullptr) return {ImageScanStatus::kServiceUnavailable, {}, directory};
    if (!fs->IsMounted()) return {ImageScanStatus::kStorageUnavailable, {}, directory};
    if (max_depth < 0) return {ImageScanStatus::kReadFailed, {}, directory};
    ImageScanResult result;
    if (!ScanDirectory(fs, directory, 0, max_depth, result)) result.paths.clear();
    else SortPaths(result);
    return result;
}
ImageScanResult ScanPhotoLibrary(FileService* fs) {
    if (fs == nullptr) return {ImageScanStatus::kServiceUnavailable, {}, {}};
    if ((!fs->IsMounted() && !fs->Init()) || !fs->IsMounted())
        return {ImageScanStatus::kStorageUnavailable, {}, {}};
    // A successful root listing distinguishes absent optional albums from I/O failures.
    std::vector<FileEntry> entries;
    errno = 0;
    if (!fs->ListDirectory("/", entries)) {
        const int failure = errno;
        return {failure == ENODEV ? ImageScanStatus::kStorageUnavailable
                : failure == ENOENT ? ImageScanStatus::kDirectoryMissing
                                    : ImageScanStatus::kReadFailed, {}, "/"};
    }
    ImageScanResult result;
    for (const auto& entry : entries) {
        const auto name = ToLower(entry.name);
        if (!entry.is_directory || (name != "photos" && name != "dcim")) continue;
        auto album = ScanImagesWithFileServiceDetailed(fs, entry.path, 3);
        if (album.status != ImageScanStatus::kReady) return album;
        result.paths.insert(result.paths.end(), album.paths.begin(), album.paths.end());
    }
    if (result.paths.empty()) return ScanImagesWithFileServiceDetailed(fs, "/", 2);
    SortPaths(result);
    return result;
}
std::vector<std::string> ScanImagesWithFileService(FileService* fs, const std::string& directory, int max_depth) {
    return ScanImagesWithFileServiceDetailed(fs, directory, max_depth).paths;
}

}  // namespace ImageLibrary
}  // namespace rodakos
