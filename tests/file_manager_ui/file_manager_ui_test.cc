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
#include "freertos/task.h"
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
        case ImageLoadStatus::kUnsupported: return "不支持这种图片格式";
        case ImageLoadStatus::kReadFailed: return "无法读取这张图片";
        case ImageLoadStatus::kInsufficientMemory: return "图片太大，内存不够";
        default: return "文件已损坏";
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
    bool capacity_known = true;
    int capacity_calls = 0;
    bool delete_succeeds = true;
    int delete_errno = EIO;
    std::vector<std::string> deletes;
    std::vector<std::string> reads;
    std::map<std::string, Directory> directories{{"/", {}}};
    bool Init() override { ++mounts; mounted = mount_succeeds; return mounted; }
    void Deinit() override { mounted = false; }
    bool IsMounted() const override { return mounted; }
    const char* GetMountPoint() const override { return "/sdcard"; }
    FileSystemType GetFileSystemType() const override { return FileSystemType::FATFS; }
    MediumType GetMediumType() const override { return MediumType::SDCard; }
    bool GetCapacity(Capacity& capacity) override {
        ++capacity_calls;
        if (!capacity_known) return false;
        capacity = {29700ULL * 1024 * 1024, 8300ULL * 1024 * 1024, 21400ULL * 1024 * 1024};
        return true;
    }
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
    bool DeleteFile(const std::string& path) override {
        ++writes;
        deletes.push_back(path);
        if (!delete_succeeds) { errno = delete_errno; return false; }
        for (auto& [dir, listing] : directories) {
            auto& entries = listing.entries;
            for (auto it = entries.begin(); it != entries.end(); ++it) {
                if ((dir == "/" ? "/" : dir + "/") + it->name == path) { entries.erase(it); return true; }
            }
        }
        return true;
    }
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
        file_manager_test::hold_tasks = false;
        file_manager_test::reject_tasks = false;
        file_manager_test::held_task = nullptr;
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
        file_manager_test::held_task = nullptr;
    }
    FileManagerApp& app() { return *static_cast<FileManagerApp*>(host.current_app()); }
    void Create() { RODAK_CHECK(host.Launch(descriptor, context)); Pump(); }
    bool Has(const char* text) {
        lv_obj_update_layout(lv_screen_active());
        return FindText(app().root_, text) != nullptr || FindText(lv_layer_top(), text) != nullptr;
    }
    void Click(const char* text) {
        auto* label = FindText(app().root_, text);
        if (label == nullptr) label = FindText(lv_layer_top(), text);
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
    RODAK_CHECK(f.Has("无法读取目录"));
    RODAK_CHECK_FALSE(f.Has("空文件夹"));
    Pump(2200);
    RODAK_CHECK(f.Has("重试"));
    Screenshot("files-read-error.ppm");
    f.files.directories["/"].fail = false;
    f.Click("重试");
    RODAK_CHECK(f.Has("空文件夹"));
    RODAK_CHECK_FALSE(f.Has("无法读取目录"));
    RODAK_CHECK(f.Has("刷新"));
    RODAK_CHECK_EQ(f.files.writes, 0);
}

RODAK_TEST("missing storage service is distinct and can be registered before retry") {
    Fixture f;
    f.services.SetFileService(nullptr);
    f.Create();
    RODAK_CHECK(f.Has("存储服务不可用"));
    RODAK_CHECK_EQ(f.files.mounts, 0);
    f.services.SetFileService(&f.files);
    f.Click("重试");
    RODAK_CHECK(f.Has("空文件夹"));
}

RODAK_TEST("failed mount is retryable after card becomes available") {
    Fixture f;
    f.files.mounted = false;
    f.files.mount_succeeds = false;
    f.Create();
    RODAK_CHECK(f.Has("读不到 SD 卡"));
    RODAK_CHECK(f.files.reads.empty());
    f.files.mount_succeeds = true;
    f.Click("重试");
    RODAK_CHECK(f.Has("空文件夹"));
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
    RODAK_CHECK(f.Has("无法读取目录"));
    RODAK_CHECK_FALSE(f.Has("old.txt"));
    RODAK_CHECK_FALSE(f.Has("partial.txt"));
    f.app().OpenEntry(0);
    RODAK_CHECK(f.Has("无法读取目录"));
    f.files.directories["/"].fail = false;
    f.Click("重试");
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
    lv_area_t name_area, item_area;
    lv_obj_get_coords(name, &name_area);
    lv_obj_get_coords(item, &item_area);
    RODAK_CHECK_EQ(lv_area_get_height(&item_area), 40);
    RODAK_CHECK_EQ(lv_area_get_height(&name_area),
                   lv_font_get_line_height(lv_obj_get_style_text_font(name, 0)));
    RODAK_CHECK(name_area.y1 >= item_area.y1 && name_area.y2 <= item_area.y2);
    RODAK_CHECK(name_area.x2 < 290);
    Screenshot("files-long-directory.ppm");
    lv_obj_send_event(item, LV_EVENT_CLICKED, nullptr); Pump();
    RODAK_CHECK(f.Has("目录不存在"));
    RODAK_CHECK(f.Has(directory_path.c_str()));
    f.files.directories[directory_path] = {{Entry("inside.txt", (directory_path + "/inside.txt").c_str())}};
    f.Click("重试");
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
    RODAK_CHECK(f.Has("无法读取目录"));
    RODAK_CHECK_FALSE(f.Has("目录不存在"));
}

RODAK_TEST("ENOTDIR and ENODEV remain distinct from generic I/O failure") {
    Fixture f;
    f.files.directories["/"].fail = true;
    f.files.directories["/"].error = ENOTDIR;
    f.Create();
    RODAK_CHECK(f.Has("目录不存在"));
    f.files.directories["/"].error = ENODEV;
    f.Click("重试");
    RODAK_CHECK(f.Has("读不到 SD 卡"));
    f.files.directories["/"].error = EIO;
    f.Click("重试");
    RODAK_CHECK(f.Has("无法读取目录"));
}

RODAK_TEST("preview failure releases the former image and retries the original file") {
    Fixture f;
    f.Image();
    RODAK_CHECK_EQ(live_images, 1);
    image_status = ImageLoadStatus::kReadFailed;
    f.app().ShowImagePreview(Entry("next.jpg", "/next.jpg"));
    Pump();
    RODAK_CHECK(f.Has("无法读取这张图片"));
    RODAK_CHECK_EQ(live_images, 0);
    RODAK_CHECK_EQ(attached_on_release, 0);
    RODAK_CHECK(lv_image_get_src(f.app().preview_image_) == nullptr);
    Screenshot("files-preview-error.ppm");
    image_status = ImageLoadStatus::kLoaded;
    f.Click("重试");
    RODAK_CHECK_EQ(image_requests.back(), "/next.jpg");
    RODAK_CHECK_EQ(live_images, 1);
    RODAK_CHECK_FALSE(f.Has("重试"));
    f.app().NavigateBack();
    RODAK_CHECK_EQ(live_images, 0);
    RODAK_CHECK_EQ(attached_on_release, 0);
}

RODAK_TEST("unsupported and allocation failures retain a usable preview retry") {
    Fixture f;
    image_status = ImageLoadStatus::kUnsupported;
    f.Image();
    RODAK_CHECK(f.Has("不支持这种图片格式"));
    image_status = ImageLoadStatus::kInsufficientMemory;
    f.Click("重试");
    RODAK_CHECK(f.Has("图片太大，内存不够"));
    image_status = ImageLoadStatus::kDecodeFailed;
    f.Click("重试");
    RODAK_CHECK(f.Has("文件已损坏"));
    image_status = ImageLoadStatus::kLoaded;
    f.Click("重试");
    RODAK_CHECK_EQ(live_images, 1);
}

RODAK_TEST("decoder rejecting the loaded descriptor does not keep an invalid image") {
    Fixture f;
    bad_descriptor = true;
    f.Image();
    RODAK_CHECK(f.Has("文件已损坏"));
    RODAK_CHECK_EQ(live_images, 0);
    bad_descriptor = false;
    f.Click("重试");
    RODAK_CHECK_EQ(live_images, 1);
}

RODAK_TEST("preview retry rechecks missing service and remounts storage") {
    Fixture f;
    f.Image();
    f.services.SetFileService(nullptr);
    f.app().ShowImagePreview(Entry("photo.jpg", "/photo.jpg"));
    RODAK_CHECK(f.Has("存储服务不可用"));
    f.services.SetFileService(&f.files);
    f.files.mounted = false;
    f.files.mount_succeeds = false;
    f.Click("重试");
    RODAK_CHECK(f.Has("读不到 SD 卡"));
    f.files.mount_succeeds = true;
    f.Click("重试");
    RODAK_CHECK_EQ(live_images, 1);
}

RODAK_TEST("refreshing from preview releases image even when directory access fails") {
    Fixture f;
    f.Image();
    f.files.directories["/"].fail = true;
    f.app().RefreshDirectory();
    RODAK_CHECK_EQ(live_images, 0);
    RODAK_CHECK_EQ(attached_on_release, 0);
    RODAK_CHECK(f.Has("无法读取目录"));
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
    RODAK_CHECK(FindText(lv_screen_active(), "返回桌面失败，请重试") != nullptr);
    reject_async = false;
}

RODAK_TEST("ordinary non-image files keep the information view and back behavior") {
    Fixture f;
    const char* file_name = "A long ordinary document name with several words.txt";
    f.files.directories["/"].entries = {Entry(file_name, "/note.txt")};
    f.Create();
    auto* item = lv_obj_get_child(f.app().list_container_, 0);
    auto* name = lv_obj_get_child(item, 1);
    auto* size = lv_obj_get_child(item, 2);
    lv_area_t name_area, size_area;
    lv_obj_get_coords(name, &name_area);
    lv_obj_get_coords(size, &size_area);
    RODAK_CHECK_EQ(lv_area_get_height(&name_area),
                   lv_font_get_line_height(lv_obj_get_style_text_font(name, 0)));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(size)), std::string("123 B"));
    RODAK_CHECK(name_area.x2 < size_area.x1);
    Screenshot("files-long-filename.ppm");
    lv_obj_send_event(item, LV_EVENT_CLICKED, nullptr); Pump();
    RODAK_CHECK(f.app().view_mode_ == FileManagerApp::ViewMode::kInfo);
    lv_area_t title_area, path_area;
    lv_obj_get_coords(f.app().info_title_label_, &title_area);
    lv_obj_get_coords(f.app().info_path_label_, &path_area);
    RODAK_CHECK_EQ(lv_area_get_height(&title_area),
                   lv_font_get_line_height(lv_obj_get_style_text_font(f.app().info_title_label_, 0)));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().title_label_)), std::string("文件详情"));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().info_type_label_)), std::string("TXT 文件"));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().info_size_label_)), std::string("123 B"));
    Screenshot("files-long-info-title.ppm");
    RODAK_CHECK(title_area.y2 < path_area.y1);
    RODAK_CHECK(image_requests.empty());
    f.app().NavigateBack();
    RODAK_CHECK_EQ(f.app().entries_.front().name, std::string(file_name));
    RODAK_CHECK_EQ(f.files.writes, 0);
}

RODAK_TEST("root strip shows free space from a background probe and the item count") {
    Fixture f;
    f.files.directories["/"].entries = {Entry("music", "/sdcard/music", true), Entry("notes.txt", "/sdcard/notes.txt")};
    f.Create();
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().title_label_)), std::string("文件"));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().strip_left_label_)), std::string("SD 卡 · 剩余 20.9 GB"));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().strip_right_label_)), std::string("2 项"));
    RODAK_CHECK_EQ(f.files.capacity_calls, 1);
    Screenshot("files-root.ppm");
}

RODAK_TEST("the list draws before the capacity probe finishes and updates afterwards") {
    Fixture f;
    file_manager_test::hold_tasks = true;
    f.files.directories["/"].entries = {Entry("notes.txt", "/sdcard/notes.txt")};
    f.Create();
    RODAK_CHECK(f.Has("notes.txt"));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().strip_left_label_)), std::string("SD 卡"));
    RODAK_CHECK_EQ(f.files.capacity_calls, 0);
    file_manager_test::held_task();
    file_manager_test::held_task = nullptr;
    Pump(400);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().strip_left_label_)), std::string("SD 卡 · 剩余 20.9 GB"));
    RODAK_CHECK(f.app().capacity_timer_ == nullptr);
}

RODAK_TEST("a failed capacity probe keeps the plain strip and the list usable") {
    Fixture f;
    f.files.capacity_known = false;
    f.files.directories["/"].entries = {Entry("notes.txt", "/sdcard/notes.txt")};
    f.Create();
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().strip_left_label_)), std::string("SD 卡"));
    file_manager_test::reject_tasks = true;
    f.app().RefreshDirectory();
    Pump();
    RODAK_CHECK(f.Has("notes.txt"));
}

RODAK_TEST("subfolder header shows the folder name and the strip shows its path") {
    Fixture f;
    f.files.directories["/"].entries = {Entry("music", "/sdcard/music", true)};
    f.files.directories["/music"].entries = {Entry("海边散步.mp3", "/sdcard/music/海边散步.mp3"),
                                             Entry("cover.jpg", "/sdcard/music/cover.jpg")};
    f.Create();
    f.Click("music");
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().title_label_)), std::string("music"));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().strip_left_label_)), std::string("/music"));
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().strip_right_label_)), std::string("2 项"));
    Screenshot("files-subfolder.ppm");
}

RODAK_TEST("preview fills the screen, hides chrome on tap and info returns to the preview") {
    Fixture f;
    f.Image();
    RODAK_CHECK(f.app().view_mode_ == FileManagerApp::ViewMode::kPreview);
    RODAK_CHECK(lv_obj_has_flag(f.app().header_, LV_OBJ_FLAG_HIDDEN));
    lv_area_t body;
    lv_obj_get_coords(f.app().preview_body_, &body);
    RODAK_CHECK_EQ(lv_area_get_width(&body), 320);
    RODAK_CHECK_EQ(lv_area_get_height(&body), 240);
    Screenshot("files-preview.ppm");
    lv_obj_send_event(f.app().preview_body_, LV_EVENT_CLICKED, nullptr);
    Pump();
    RODAK_CHECK(lv_obj_has_flag(f.app().preview_top_, LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(f.app().preview_body_, LV_EVENT_CLICKED, nullptr);
    Pump();
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app().preview_top_, LV_OBJ_FLAG_HIDDEN));

    auto* info_button = lv_obj_get_child(f.app().preview_top_, 1);
    lv_obj_send_event(info_button, LV_EVENT_CLICKED, nullptr);
    Pump();
    RODAK_CHECK(f.app().view_mode_ == FileManagerApp::ViewMode::kInfo);
    RODAK_CHECK_EQ(live_images, 0);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().info_type_label_)), std::string("JPG 图片"));
    RODAK_CHECK(f.app().OnBackRequested());
    RODAK_CHECK(f.app().view_mode_ == FileManagerApp::ViewMode::kPreview);
    RODAK_CHECK_EQ(live_images, 1);
    RODAK_CHECK(f.app().OnBackRequested());
    RODAK_CHECK(f.app().view_mode_ == FileManagerApp::ViewMode::kList);
    RODAK_CHECK_EQ(live_images, 0);
}

RODAK_TEST("delete asks first, calls DeleteFile directly and refreshes the folder") {
    Fixture f;
    f.files.directories["/"].entries = {Entry("song.mp3", "/sdcard/song.mp3"), Entry("notes.txt", "/sdcard/notes.txt")};
    f.Create();
    f.Click("song.mp3");
    RODAK_CHECK(f.app().view_mode_ == FileManagerApp::ViewMode::kInfo);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app().info_type_label_)), std::string("MP3 音频"));
    Screenshot("files-info.ppm");
    f.Click("删除文件");
    RODAK_CHECK(f.Has("删除这个文件？"));
    RODAK_CHECK(f.files.deletes.empty());
    Screenshot("files-delete-confirm.ppm");
    f.Click("取消");
    Pump();
    RODAK_CHECK_FALSE(f.Has("删除这个文件？"));
    RODAK_CHECK(f.files.deletes.empty());

    f.Click("删除文件");
    f.Click("删除");
    Pump();
    RODAK_CHECK_EQ(f.files.deletes.size(), 1u);
    RODAK_CHECK_EQ(f.files.deletes.back(), std::string("/song.mp3"));
    RODAK_CHECK(f.app().view_mode_ == FileManagerApp::ViewMode::kList);
    RODAK_CHECK_FALSE(f.Has("song.mp3"));
    RODAK_CHECK(f.Has("notes.txt"));
    RODAK_CHECK(FindText(lv_screen_active(), "已删除") != nullptr);
}

RODAK_TEST("a busy file is not deleted and the user is told why") {
    Fixture f;
    f.files.directories["/"].entries = {Entry("rec.wav", "/sdcard/rec.wav")};
    f.files.delete_succeeds = false;
    f.files.delete_errno = EBUSY;
    f.Create();
    f.Click("rec.wav");
    f.Click("删除文件");
    f.Click("删除");
    Pump();
    RODAK_CHECK(FindText(lv_screen_active(), "文件正在使用，稍后再删") != nullptr);
    RODAK_CHECK(f.app().view_mode_ == FileManagerApp::ViewMode::kInfo);
    RODAK_CHECK_EQ(f.app().entries_.size(), 1u);
}

RODAK_TEST("system files under /rodakos never offer delete") {
    Fixture f;
    f.files.directories["/"].entries = {Entry("rodakos", "/sdcard/rodakos", true)};
    f.files.directories["/rodakos"].entries = {Entry("theme.rap", "/sdcard/rodakos/theme.rap")};
    f.Create();
    f.Click("rodakos");
    f.Click("theme.rap");
    RODAK_CHECK(f.app().view_mode_ == FileManagerApp::ViewMode::kInfo);
    RODAK_CHECK(lv_obj_has_flag(f.app().info_delete_button_, LV_OBJ_FLAG_HIDDEN));
    f.app().AskDelete();
    f.app().ConfirmDelete();
    RODAK_CHECK(f.files.deletes.empty());
}

RODAK_TEST("back closes the delete dialog before leaving the info page") {
    Fixture f;
    f.files.directories["/"].entries = {Entry("notes.txt", "/sdcard/notes.txt")};
    f.Create();
    f.Click("notes.txt");
    f.Click("删除文件");
    RODAK_CHECK(f.app().OnBackRequested());
    Pump();
    RODAK_CHECK_FALSE(f.Has("删除这个文件？"));
    RODAK_CHECK(f.app().view_mode_ == FileManagerApp::ViewMode::kInfo);
}

RODAK_TEST("SD card error screen uses the storage icon and Chinese copy") {
    Fixture f;
    f.files.mounted = false;
    f.files.mount_succeeds = false;
    f.Create();
    RODAK_CHECK(f.Has("读不到 SD 卡"));
    RODAK_CHECK(f.Has("请插入或重新插拔 SD 卡"));
    Screenshot("files-sd-error.ppm");
}
