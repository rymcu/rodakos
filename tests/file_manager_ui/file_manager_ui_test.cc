#include "test_framework.h"
#include "phone_os/phone_app.h"
#include "phone_ui/image_library.h"
#include "rodakos_adapters/file_service.h"
#include <array>
#include <cerrno>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define private public
#include "apps/file_manager/file_manager_app.h"
#undef private
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_host.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_ui.h"
#include "settings.h"
#include <src/others/test/lv_test.h>

namespace {
using rodakos::FileEntry;
using rodakos::ImageLibrary::ImageLoadStatus;
int live_images = 0;
int attached_on_release = 0;
lv_obj_t* image_target = nullptr;
bool bad_descriptor = false;
bool reject_async = false;
ImageLoadStatus image_status = ImageLoadStatus::kLoaded;
std::vector<std::string> image_requests;

class TestImage final : public rodakos::LvglImage {
public:
    TestImage() {
        ++live_images;
        descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
        descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
        descriptor.header.w = bad_descriptor ? 0 : 2;
        descriptor.header.h = 2;
        descriptor.header.stride = 4;
        descriptor.data_size = sizeof(pixels);
        descriptor.data = reinterpret_cast<const uint8_t*>(pixels.data());
    }
    ~TestImage() override {
        if (image_target != nullptr && lv_obj_is_valid(image_target) &&
            lv_image_get_src(image_target) == &descriptor) ++attached_on_release;
        --live_images;
    }
    const lv_image_dsc_t* GetImageDescriptor() const override { return &descriptor; }
private:
    std::array<uint16_t, 4> pixels{0xf800, 0x07e0, 0x001f, 0xffff};
    lv_image_dsc_t descriptor{};
};
}

// The UI consumes this interface; decoder and filesystem correctness have separate tests.
namespace rodakos::ImageLibrary {
bool IsSupportedImage(const std::string& path) { return path.ends_with(".jpg"); }
ImageLoadResult LoadImageForDisplayDetailed(const std::string& path) {
    image_requests.push_back(path);
    if (image_status != ImageLoadStatus::kLoaded) return {image_status, {}};
    return {image_status, std::make_shared<TestImage>()};
}
const char* ImageLoadErrorText(ImageLoadStatus status) {
    switch (status) {
        case ImageLoadStatus::kUnsupported: return "Unsupported image format";
        case ImageLoadStatus::kReadFailed: return "Could not read image";
        case ImageLoadStatus::kInsufficientMemory: return "Not enough image memory";
        default: return "Could not load image";
    }
}
}

extern "C" lv_result_t __real_lv_async_call(lv_async_cb_t, void*);
extern "C" lv_result_t __wrap_lv_async_call(lv_async_cb_t callback, void* data) {
    return reject_async ? LV_RESULT_INVALID : __real_lv_async_call(callback, data);
}

namespace {
struct Directory {
    std::vector<FileEntry> entries;
    bool fail = false;
    int error = EIO;
};
class Files final : public rodakos::FileService {
public:
    bool mounted = true;
    bool mount_succeeds = true;
    bool leave_errno = false;
    int writes = 0;
    int mounts = 0;
    std::vector<std::string> reads;
    std::map<std::string, Directory> directories{{"/", {}}};
    bool Init() override { ++mounts; mounted = mount_succeeds; return mounted; }
    void Deinit() override { mounted = false; }
    bool IsMounted() const override { return mounted; }
    const char* GetMountPoint() const override { return "/sdcard"; }
    FileSystemType GetFileSystemType() const override { return FileSystemType::FATFS; }
    MediumType GetMediumType() const override { return MediumType::SDCard; }
    bool GetCapacity(Capacity&) override { return false; }
    bool ListDirectory(const std::string& path, std::vector<FileEntry>& entries) override {
        reads.push_back(path);
        const auto it = directories.find(path);
        if (it == directories.end()) { errno = ENOENT; return false; }
        entries = it->second.entries;
        if (!leave_errno) errno = it->second.error;
        return !it->second.fail;
    }
    bool ReadFile(const std::string&, std::vector<uint8_t>&) override { return false; }
    bool WriteFile(const std::string&, const std::vector<uint8_t>&, bool) override { ++writes; return false; }
    bool DeleteFile(const std::string&) override { ++writes; return false; }
    bool DeleteDirectory(const std::string&) override { ++writes; return false; }
    bool CreateDirectory(const std::string&) override { ++writes; return false; }
    bool Rename(const std::string&, const std::string&) override { ++writes; return false; }
    bool Exists(const std::string&) override { return true; }
    size_t GetFileSize(const std::string&) override { return 0; }
};
FileEntry Entry(const char* name, const char* path, bool directory = false) {
    return {name, path, directory, 123, 0};
}
void Pump(uint32_t time = 50) {
    lv_test_wait(time);
    lv_obj_update_layout(lv_screen_active());
}
lv_obj_t* FindText(lv_obj_t* root, const std::string& text) {
    if (root == nullptr || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(root, &lv_label_class) && text == lv_label_get_text(root)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        if (auto* match = FindText(lv_obj_get_child(root, i), text)) return match;
    }
    return nullptr;
}
void Screenshot(const char* name) {
    Pump(20);
    const auto* buffer = lv_display_get_buf_active(lv_display_get_default());
    RODAK_CHECK_EQ(buffer->header.cf, LV_COLOR_FORMAT_XRGB8888);
    std::ofstream out(name, std::ios::binary);
    out << "P6\n320 240\n255\n";
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            const auto* pixel = buffer->data + y * buffer->header.stride + x * 4;
            const char rgb[] = {static_cast<char>(pixel[2]), static_cast<char>(pixel[1]),
                                static_cast<char>(pixel[0])};
            out.write(rgb, 3);
        }
    }
}
struct Fixture {
    Files files;
    PhoneUi ui{320, 240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui, navigation, registry, services, settings};
    PhoneAppHost host;
    PhoneAppDescriptor descriptor;
    Fixture() {
        image_status = ImageLoadStatus::kLoaded;
        image_requests.clear();
        bad_descriptor = false;
        reject_async = false;
        image_target = nullptr;
        attached_on_release = 0;
        services.SetFileService(&files);
        descriptor.id = "files";
        descriptor.title = "Files";
        descriptor.create = [] { return std::make_unique<FileManagerApp>(); };
    }
    ~Fixture() {
        host.CloseCurrent();
        image_target = nullptr;
        Pump(2000);
        lv_obj_clean(lv_screen_active());
        lv_obj_clean(lv_layer_top());
    }
    FileManagerApp& app() { return *static_cast<FileManagerApp*>(host.current_app()); }
    void Create() { RODAK_CHECK(host.Launch(descriptor, context)); Pump(); }
    bool Has(const char* text) {
        lv_obj_update_layout(lv_screen_active());
        return FindText(app().root_, text) != nullptr;
    }
    void Click(const char* text) {
        auto* label = FindText(app().root_, text);
        RODAK_CHECK(label != nullptr);
        lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, nullptr);
        Pump();
    }
    void Image() {
        files.directories["/"].entries = {Entry("photo.jpg", "/photo.jpg")};
        Create();
        Click("photo.jpg");
        image_target = app().preview_image_;
    }
};
}

RODAK_TEST("first directory failure is persistent and never shown as an empty folder") {
    Fixture f;
    f.files.directories["/"].fail = true;
    f.Create();
    RODAK_CHECK(f.Has("Could not read folder"));
    RODAK_CHECK_FALSE(f.Has("Empty folder"));
    Pump(2200);
    RODAK_CHECK(f.Has("Retry"));
    Screenshot("files-read-error.ppm");
    f.files.directories["/"].fail = false;
    f.Click("Retry");
    RODAK_CHECK(f.Has("Empty folder"));
    RODAK_CHECK_FALSE(f.Has("Could not read folder"));
    RODAK_CHECK(f.Has("Refresh"));
    RODAK_CHECK_EQ(f.files.writes, 0);
}

RODAK_TEST("missing storage service is distinct and can be registered before retry") {
    Fixture f;
    f.services.SetFileService(nullptr);
    f.Create();
    RODAK_CHECK(f.Has("Storage service unavailable"));
    RODAK_CHECK_EQ(f.files.mounts, 0);
    f.services.SetFileService(&f.files);
    f.Click("Retry");
    RODAK_CHECK(f.Has("Empty folder"));
}

RODAK_TEST("failed mount is retryable after card becomes available") {
    Fixture f;
    f.files.mounted = false;
    f.files.mount_succeeds = false;
    f.Create();
    RODAK_CHECK(f.Has("SD card unavailable"));
    RODAK_CHECK(f.files.reads.empty());
    f.files.mount_succeeds = true;
    f.Click("Retry");
    RODAK_CHECK(f.Has("Empty folder"));
    RODAK_CHECK_EQ(f.files.mounts, 2);
}

RODAK_TEST("refresh failure removes stale and partially returned directory entries") {
    Fixture f;
    f.files.directories["/"].entries = {Entry("old.txt", "/old.txt")};
    f.Create();
    RODAK_CHECK(f.Has("old.txt"));
    f.files.directories["/"].entries = {Entry("partial.txt", "/partial.txt")};
    f.files.directories["/"].fail = true;
    f.app().RefreshDirectory();
    Pump();
    RODAK_CHECK(f.Has("Could not read folder"));
    RODAK_CHECK_FALSE(f.Has("old.txt"));
    RODAK_CHECK_FALSE(f.Has("partial.txt"));
    f.app().OpenEntry(0);
    RODAK_CHECK(f.Has("Could not read folder"));
    f.files.directories["/"].fail = false;
    f.Click("Retry");
    RODAK_CHECK(f.Has("partial.txt"));
}

RODAK_TEST("missing child keeps attempted path for retry and permits back to parent") {
    Fixture f;
    const char* directory_name = "System Volume Information";
    const std::string directory_path = std::string("/") + directory_name;
    f.files.directories["/"].entries = {Entry(directory_name, directory_path.c_str(), true)};
    f.Create();
    auto* item = lv_obj_get_child(f.app().list_container_, 0);
    auto* name = lv_obj_get_child(item, 1);
    auto* meta = FindText(item, "Folder");
    RODAK_CHECK(meta != nullptr);
    lv_area_t name_area, meta_area;
    lv_obj_get_coords(name, &name_area);
    lv_obj_get_coords(meta, &meta_area);
    RODAK_CHECK_EQ(lv_area_get_height(&name_area),
                   lv_font_get_line_height(lv_obj_get_style_text_font(name, 0)));
    RODAK_CHECK(std::string(lv_label_get_text(name)).find("...") != std::string::npos);
    RODAK_CHECK(name_area.y2 < meta_area.y1);
    Screenshot("files-long-directory.ppm");
    lv_obj_send_event(item, LV_EVENT_CLICKED, nullptr); Pump();
    RODAK_CHECK(f.Has("Folder unavailable"));
    RODAK_CHECK(f.Has(("/sdcard" + directory_path).c_str()));
    f.files.directories[directory_path] = {{Entry("inside.txt", (directory_path + "/inside.txt").c_str())}};
    f.Click("Retry");
    RODAK_CHECK(f.Has("inside.txt"));
    RODAK_CHECK_EQ(f.files.reads.back(), directory_path);
    f.app().NavigateBack();
    RODAK_CHECK_EQ(f.app().entries_.front().name, std::string(directory_name));
    f.files.directories.erase(directory_path);
    lv_obj_send_event(lv_obj_get_child(f.app().list_container_, 0), LV_EVENT_CLICKED, nullptr); Pump();
    f.app().NavigateBack();
    RODAK_CHECK_EQ(f.app().entries_.front().name, std::string(directory_name));
}

RODAK_TEST("errno from an earlier operation cannot misclassify an unspecified failure") {
    Fixture f;
    f.files.directories["/"].fail = true;
    f.files.leave_errno = true;
    errno = ENOENT;
    f.Create();
    RODAK_CHECK(f.Has("Could not read folder"));
    RODAK_CHECK_FALSE(f.Has("Folder unavailable"));
}

RODAK_TEST("ENOTDIR and ENODEV remain distinct from generic I/O failure") {
    Fixture f;
    f.files.directories["/"].fail = true;
    f.files.directories["/"].error = ENOTDIR;
    f.Create();
    RODAK_CHECK(f.Has("Folder unavailable"));
    f.files.directories["/"].error = ENODEV;
    f.Click("Retry");
    RODAK_CHECK(f.Has("SD card unavailable"));
    f.files.directories["/"].error = EIO;
    f.Click("Retry");
    RODAK_CHECK(f.Has("Could not read folder"));
}

RODAK_TEST("preview failure releases the former image and retries the original file") {
    Fixture f;
    f.Image();
    RODAK_CHECK_EQ(live_images, 1);
    image_status = ImageLoadStatus::kReadFailed;
    f.app().ShowImagePreview(Entry("next.jpg", "/next.jpg"));
    Pump();
    RODAK_CHECK(f.Has("Could not read image"));
    RODAK_CHECK_EQ(live_images, 0);
    RODAK_CHECK_EQ(attached_on_release, 0);
    RODAK_CHECK(lv_image_get_src(f.app().preview_image_) == nullptr);
    Screenshot("files-preview-error.ppm");
    image_status = ImageLoadStatus::kLoaded;
    f.Click("Retry");
    RODAK_CHECK_EQ(image_requests.back(), "/next.jpg");
    RODAK_CHECK_EQ(live_images, 1);
    RODAK_CHECK_FALSE(f.Has("Retry"));
    f.app().NavigateBack();
    RODAK_CHECK_EQ(live_images, 0);
    RODAK_CHECK_EQ(attached_on_release, 0);
}

RODAK_TEST("unsupported and allocation failures retain a usable preview retry") {
    Fixture f;
    image_status = ImageLoadStatus::kUnsupported;
    f.Image();
    RODAK_CHECK(f.Has("Unsupported image format"));
    image_status = ImageLoadStatus::kInsufficientMemory;
    f.Click("Retry");
    RODAK_CHECK(f.Has("Not enough image memory"));
    image_status = ImageLoadStatus::kDecodeFailed;
    f.Click("Retry");
    RODAK_CHECK(f.Has("Could not load image"));
    image_status = ImageLoadStatus::kLoaded;
    f.Click("Retry");
    RODAK_CHECK_EQ(live_images, 1);
}

RODAK_TEST("decoder rejecting the loaded descriptor does not keep an invalid image") {
    Fixture f;
    bad_descriptor = true;
    f.Image();
    RODAK_CHECK(f.Has("Unsupported image"));
    RODAK_CHECK_EQ(live_images, 0);
    bad_descriptor = false;
    f.Click("Retry");
    RODAK_CHECK_EQ(live_images, 1);
}

RODAK_TEST("preview retry rechecks missing service and remounts storage") {
    Fixture f;
    f.Image();
    f.services.SetFileService(nullptr);
    f.app().ShowImagePreview(Entry("photo.jpg", "/photo.jpg"));
    RODAK_CHECK(f.Has("Storage service unavailable"));
    f.services.SetFileService(&f.files);
    f.files.mounted = false;
    f.files.mount_succeeds = false;
    f.Click("Retry");
    RODAK_CHECK(f.Has("SD card unavailable"));
    f.files.mount_succeeds = true;
    f.Click("Retry");
    RODAK_CHECK_EQ(live_images, 1);
}

RODAK_TEST("refreshing from preview releases image even when directory access fails") {
    Fixture f;
    f.Image();
    f.files.directories["/"].fail = true;
    f.app().RefreshDirectory();
    RODAK_CHECK_EQ(live_images, 0);
    RODAK_CHECK_EQ(attached_on_release, 0);
    RODAK_CHECK(f.Has("Could not read folder"));
    RODAK_CHECK_FALSE(f.Has("photo.jpg"));
}

RODAK_TEST("closing a preview cancels deferred home and releases all images before recreation") {
    Fixture f;
    f.Image();
    for (int i = 0; i < 4; ++i) {
        f.app().NavigateHome();
        f.host.CloseCurrent();
        RODAK_CHECK_EQ(live_images, 0);
        RODAK_CHECK_EQ(attached_on_release, 0);
        image_target = nullptr;
        f.Create();
        Pump(100);
        RODAK_CHECK_EQ(f.navigation.home_count, 0);
        f.Click("photo.jpg");
        image_target = f.app().preview_image_;
    }
}

RODAK_TEST("home requests are deduplicated and async allocation failure is visible") {
    Fixture f;
    f.Create();
    f.app().NavigateHome();
    f.app().NavigateHome();
    Pump();
    RODAK_CHECK_EQ(f.navigation.home_count, 1);
    reject_async = true;
    f.app().NavigateHome();
    RODAK_CHECK(FindText(lv_screen_active(), "Navigation unavailable") != nullptr);
    reject_async = false;
}

RODAK_TEST("ordinary non-image files keep the information view and back behavior") {
    Fixture f;
    const char* file_name = "A long ordinary document name with several words.txt";
    f.files.directories["/"].entries = {Entry(file_name, "/note.txt")};
    f.Create();
    auto* item = lv_obj_get_child(f.app().list_container_, 0);
    auto* name = lv_obj_get_child(item, 1);
    auto* meta = lv_obj_get_child(item, 2);
    lv_area_t name_area, meta_area;
    lv_obj_get_coords(name, &name_area);
    lv_obj_get_coords(meta, &meta_area);
    RODAK_CHECK_EQ(lv_area_get_height(&name_area),
                   lv_font_get_line_height(lv_obj_get_style_text_font(name, 0)));
    RODAK_CHECK(std::string(lv_label_get_text(name)).find("...") != std::string::npos);
    RODAK_CHECK(name_area.y2 < meta_area.y1);
    Screenshot("files-long-filename.ppm");
    lv_obj_send_event(item, LV_EVENT_CLICKED, nullptr); Pump();
    RODAK_CHECK(f.app().view_mode_ == FileManagerApp::ViewMode::kInfo);
    lv_area_t title_area, detail_area;
    lv_obj_get_coords(f.app().info_title_label_, &title_area);
    lv_obj_get_coords(f.app().info_detail_label_, &detail_area);
    RODAK_CHECK_EQ(lv_area_get_height(&title_area),
                   lv_font_get_line_height(lv_obj_get_style_text_font(f.app().info_title_label_, 0)));
    RODAK_CHECK(std::string(lv_label_get_text(f.app().info_title_label_)).find("...") != std::string::npos);
    Screenshot("files-long-info-title.ppm");
    RODAK_CHECK(title_area.y2 < detail_area.y1);
    RODAK_CHECK(image_requests.empty());
    f.app().NavigateBack();
    RODAK_CHECK_EQ(f.app().entries_.front().name, std::string(file_name));
    RODAK_CHECK_EQ(f.files.writes, 0);
}
