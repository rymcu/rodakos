#pragma once

#include <string>
#include <vector>
#include <memory>
#include <lvgl.h>

namespace rodakos {

class FileService;  // Forward declaration

/**
 * @brief LVGL 图片封装基类
 */
class LvglImage {
public:
    virtual ~LvglImage() = default;
    virtual const lv_image_dsc_t* GetImageDescriptor() const = 0;
    virtual const void* GetImageSource() const { return GetImageDescriptor(); }
};

/**
 * @brief 从 SPIRAM 分配的图片
 */
class LvglAllocatedImage : public LvglImage {
public:
    LvglAllocatedImage(void* data, size_t size);
    LvglAllocatedImage(void* data, size_t size, int width, int height, int stride, lv_color_format_t format);
    ~LvglAllocatedImage() override;

    const lv_image_dsc_t* GetImageDescriptor() const override { return &image_dsc_; }

private:
    lv_image_dsc_t image_dsc_;
};

/**
 * @brief LVGL image backed by a filesystem path.
 */
class LvglFileImage : public LvglImage {
public:
    explicit LvglFileImage(std::string lvgl_path);

    const lv_image_dsc_t* GetImageDescriptor() const override { return nullptr; }
    const void* GetImageSource() const override { return lvgl_path_.c_str(); }

private:
    std::string lvgl_path_;
};

/**
 * @brief 图片库工具函数
 */
namespace ImageLibrary {

/**
 * @brief 获取文件名（不含路径）
 */
std::string Basename(const std::string& path);

/**
 * @brief 检查是否是支持的图片格式
 */
bool IsSupportedImage(const std::string& filename);

/**
 * @brief Check whether the file can be shown from the in-memory LVGL source used by Photos.
 */
bool IsMemoryRenderableImage(const std::string& filename);

/**
 * @brief Check whether the file should be sent to LVGL as a filesystem path.
 */
bool IsFileRenderableImage(const std::string& filename);

/**
 * @brief 扫描目录中的图片文件
 * @param directory 目录路径
 * @param max_depth 最大递归深度
 * @return 图片文件路径列表
 */
std::vector<std::string> ScanImages(const std::string& directory, int max_depth);

/**
 * @brief 使用 FileService 扫描图片（推荐）
 * @param fs FileService 实例
 * @param directory 目录路径
 * @param max_depth 最大递归深度
 * @return 图片文件路径列表
 */
std::vector<std::string> ScanImagesWithFileService(FileService* fs, const std::string& directory, int max_depth);

/**
 * @brief 加载图片文件
 * @param path 图片路径
 * @return 图片对象，失败返回 nullptr
 */
std::shared_ptr<LvglImage> LoadImage(const std::string& path);

/**
 * @brief Load an image using the best LVGL source for its format.
 */
std::shared_ptr<LvglImage> LoadImageForDisplay(const std::string& path);

}  // namespace ImageLibrary

}  // namespace rodakos
