#include "test_framework.h"
#include "host_images.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_common.h"
#include "apps/photos/photos_app.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "settings.h"
#include <src/others/test/lv_test.h>
#include <cstdio>
#include <memory>

namespace {
void Pump(uint32_t ms=750) { lv_test_wait(ms); lv_obj_update_layout(lv_screen_active()); }
void SaveScreenshot(const char* name) {
    Pump(10);
    const auto* buffer=lv_display_get_buf_active(lv_display_get_default());
    RODAK_CHECK_EQ(buffer->header.cf,LV_COLOR_FORMAT_XRGB8888);
    std::ofstream output(name,std::ios::binary);
    output << "P6\n320 240\n255\n";
    for (int y=0;y<240;++y) for (int x=0;x<320;++x) {
        const auto* pixel=buffer->data+y*buffer->header.stride+x*4;
        const char rgb[]={static_cast<char>(pixel[2]),static_cast<char>(pixel[1]),static_cast<char>(pixel[0])};
        output.write(rgb,3);
    }
}
lv_obj_t* Label(lv_obj_t* parent,const char* text) {
    if (lv_obj_has_flag(parent,LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(parent,&lv_label_class) && std::string(lv_label_get_text(parent))==text) return parent;
    for (uint32_t i=0;i<lv_obj_get_child_count(parent);++i) {
        if (auto* found=Label(lv_obj_get_child(parent,i),text)) return found;
    }
    return nullptr;
}
size_t ObjectCount(lv_obj_t* root) {
    size_t count = 1;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        count += ObjectCount(lv_obj_get_child(root, i));
    }
    return count;
}
void AssertIconButton(lv_obj_t* button, const char* icon) {
    RODAK_CHECK(lv_obj_check_type(button, &lv_button_class));
    auto* label = lv_obj_get_child(button, 0);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(label)), icon);
    RODAK_CHECK(lv_obj_get_style_text_font(label, LV_PART_MAIN) == PhoneIconFont());
}
void AssertHeaderHitArea(lv_obj_t* button) {
    RODAK_CHECK_EQ(lv_obj_get_width(button), 40);
    RODAK_CHECK_EQ(lv_obj_get_height(button), 26);
    lv_area_t area;
    lv_obj_get_coords(button, &area);
    const lv_point_t top{area.x1, area.y1 - 7};
    const lv_point_t bottom{area.x2, area.y2 + 7};
    RODAK_CHECK(lv_obj_hit_test(button, &top));
    RODAK_CHECK(lv_obj_hit_test(button, &bottom));
}
void Click(lv_obj_t* button) {
    lv_obj_update_layout(button); lv_area_t area;
    lv_obj_get_coords(button,&area);
    RODAK_CHECK(area.x1>=0 && area.x2<320 && area.y1>=0 && area.y2<240);
    lv_test_mouse_click_at((area.x1+area.x2)/2,(area.y1+area.y2)/2);
    Pump();
}
void Swipe(int x, int y, int dx, int dy = 0, bool return_to_origin = false) {
    lv_test_mouse_move_to(x, y);
    lv_test_mouse_press();
    Pump(50);
    lv_test_mouse_move_by(dx, dy);
    Pump(50);
    if (return_to_origin) {
        lv_test_mouse_move_to(x, y);
        Pump(50);
    }
    lv_test_mouse_release();
    Pump();
}
struct Fixture {
    photo_test::TestFiles files;
    photo_test::Files fs;
    PhoneUi ui{320,240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui,navigation,registry,services,settings};
    PhotosApp app;
    bool started=false;
    Fixture() {
        photo_test::ResetFailures(); services.SetFileService(&fs);
        ui.SetThemeName("dark");
    }
    void Start() { RODAK_CHECK(app.OnCreate(context)); started=true; Pump(); }
    void Album(bool corrupt=false) {
        fs.Album(files.root.string());
        fs.Image(files.root.string(),corrupt ? files.Save("broken.jpg",{1,2,3}) : files.Jpeg("one.jpg"), 1);
    }
    ~Fixture() {
        photo_test::ResetFailures();
        if (started) app.OnDestroy();
        Pump(2000); lv_obj_clean(lv_screen_active());
    }
};
}

RODAK_TEST("Photos retains distinct service mount read and empty states with an on-screen retry") {
    Fixture f;
    f.services.SetFileService(nullptr); f.Start();
    RODAK_CHECK(Label(lv_screen_active(),"存储服务不可用")!=nullptr);
    f.services.SetFileService(&f.fs); f.fs.mounted=false; f.fs.mount_ok=false;
    Click(f.app.retry_button_);
    RODAK_CHECK(Label(lv_screen_active(),"读不到 SD 卡")!=nullptr);
    f.fs.mount_ok=true; f.fs.failures["/"]=EIO;
    Click(f.app.refresh_button_);
    RODAK_CHECK(Label(lv_screen_active(),"无法读取相册")!=nullptr);
    RODAK_CHECK(Label(lv_screen_active(),"还没有照片")==nullptr);
    SaveScreenshot("photos-read-error.ppm");
    f.fs.failures.clear(); Click(f.app.refresh_button_);
    RODAK_CHECK(Label(lv_screen_active(),"还没有照片")!=nullptr);
    RODAK_CHECK(lv_obj_has_flag(f.app.retry_button_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(Label(lv_screen_active(),"相册")!=nullptr);
    SaveScreenshot("photos-empty.ppm");
}

RODAK_TEST("Photos failed rescan clears stale tiles and nested I/O errors never look empty") {
    Fixture f; f.Album(); f.Start();
    RODAK_CHECK_EQ(f.app.photos_.size(),1U);
    f.fs.directories[f.files.root.string()].push_back({"sub","/failed",true,0,0});
    f.fs.failures["/failed"]=EIO;
    Click(f.app.refresh_button_);
    RODAK_CHECK(f.app.photos_.empty());
    RODAK_CHECK(f.app.thumbnail_items_.empty());
    RODAK_CHECK(Label(lv_screen_active(),"无法读取相册")!=nullptr);
    RODAK_CHECK_EQ(photo_test::decoded_buffers,0);
    f.fs.failures.clear(); f.fs.directories["/failed"]={};
    Click(f.app.refresh_button_);
    RODAK_CHECK_EQ(f.app.photos_.size(),1U);
}

RODAK_TEST("Photos large libraries keep a bounded tile page and retain original photo indexes") {
    Fixture f;
    f.fs.Album(f.files.root.string());
    for (size_t i = 0; i < 41; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "photo-%02zu.jpg", i);
        f.fs.Image(f.files.root.string(), f.files.Jpeg(name));
    }
    f.Start();

    RODAK_CHECK_EQ(f.app.photos_.size(), 41U);
    RODAK_CHECK_EQ(f.app.thumbnail_items_.size(), 8U);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.title_label_)), "相册 · 41");
    // Six pages fit the dot indicator; the numeric label stays hidden.
    RODAK_CHECK(!lv_obj_has_flag(f.app.page_indicator_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(lv_obj_has_flag(f.app.page_label_, LV_OBJ_FLAG_HIDDEN));
    // Header 5, grid area 1 + 8 tiles x 2, page bar 1 + 2 buttons x 2 + dots + label.
    RODAK_CHECK(ObjectCount(f.app.root_) <= 31U);
    RODAK_CHECK(!lv_obj_has_flag(f.app.image_area_, LV_OBJ_FLAG_SCROLLABLE));
    AssertIconButton(f.app.back_button_, ICON_ARROW_LEFT);
    AssertIconButton(f.app.refresh_button_, ICON_REFRESH);
    AssertHeaderHitArea(f.app.back_button_);
    AssertHeaderHitArea(f.app.refresh_button_);
    AssertIconButton(f.app.previous_button_, ICON_CHEVRON_LEFT);
    AssertIconButton(f.app.next_button_, ICON_CHEVRON_RIGHT);
    for (auto* button : {f.app.previous_button_, f.app.next_button_}) {
        RODAK_CHECK(lv_obj_get_width(button) >= 64 && lv_obj_get_height(button) >= 40);
    }
    for (const auto& tile : f.app.thumbnail_items_) {
        RODAK_CHECK_EQ(lv_obj_get_width(tile.button), 78);
        RODAK_CHECK_EQ(lv_obj_get_height(tile.button), 78);
    }
    RODAK_CHECK_EQ(lv_obj_get_x(f.app.thumbnail_items_[1].button) -
                   lv_obj_get_x(f.app.thumbnail_items_[0].button), 80);
    RODAK_CHECK_EQ(lv_obj_get_y(f.app.thumbnail_items_[4].button) -
                   lv_obj_get_y(f.app.thumbnail_items_[0].button), 80);
    RODAK_CHECK(lv_obj_has_state(f.app.previous_button_, LV_STATE_DISABLED));
    RODAK_CHECK(!lv_obj_has_state(f.app.next_button_, LV_STATE_DISABLED));

    Click(f.app.next_button_);
    RODAK_CHECK_EQ(f.app.current_page_, 1U);
    RODAK_CHECK_EQ(f.app.thumbnail_items_.size(), 8U);
    RODAK_CHECK_EQ(f.app.thumbnail_items_[0].photo_index, 8U);

    for (size_t page = 2; page < 6; ++page) Click(f.app.next_button_);
    RODAK_CHECK_EQ(f.app.current_page_, 5U);
    RODAK_CHECK_EQ(f.app.thumbnail_items_.size(), 1U);
    RODAK_CHECK_EQ(f.app.thumbnail_items_[0].photo_index, 40U);
    RODAK_CHECK(lv_obj_has_state(f.app.next_button_, LV_STATE_DISABLED));

    Click(f.app.previous_button_);
    Click(f.app.thumbnail_items_[0].image);
    RODAK_CHECK_EQ(f.app.current_photo_index_, 32U);
    RODAK_CHECK(Label(lv_screen_active(), "photo-08.jpg") != nullptr);
    RODAK_CHECK(Label(lv_screen_active(), "33 / 41") != nullptr);
    RODAK_CHECK(f.app.refresh_button_ == nullptr);
    RODAK_CHECK(lv_label_get_long_mode(f.app.filename_label_) == LV_LABEL_LONG_DOT);
    AssertIconButton(f.app.back_button_, ICON_ARROW_LEFT);
    AssertIconButton(f.app.previous_button_, ICON_CHEVRON_LEFT);
    AssertIconButton(f.app.next_button_, ICON_CHEVRON_RIGHT);
    RODAK_CHECK(ObjectCount(f.app.root_) <= 16U);
    RODAK_CHECK(f.app.thumbnail_items_.empty());
    RODAK_CHECK(f.app.thumbnail_timer_ == nullptr);
    RODAK_CHECK_EQ(photo_test::decoded_buffers, 1);
    Swipe(160, 120, 40);
    RODAK_CHECK_EQ(f.app.current_photo_index_, 31U);
    RODAK_CHECK(f.app.OnBackRequested());
    RODAK_CHECK(f.app.current_image_ == nullptr);
    RODAK_CHECK_EQ(photo_test::decoded_buffers, 0);
    Pump();
    RODAK_CHECK_EQ(f.app.current_page_, 3U);
    RODAK_CHECK(!f.app.OnBackRequested());
}

RODAK_TEST("Photos uses a numeric page label beyond eight pages") {
    Fixture f;
    f.fs.Album(f.files.root.string());
    for (size_t i = 0; i < 73; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "p-%02zu.jpg", i);
        f.fs.Image(f.files.root.string(), f.files.Save(name, {1, 2, 3}));
    }
    f.Start();
    RODAK_CHECK(lv_obj_has_flag(f.app.page_indicator_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(Label(lv_screen_active(), "1 / 10") != nullptr);
}

RODAK_TEST("Photos fullscreen tap hides and restores the overlay without leaving the photo") {
    Fixture f; f.Album(); f.Start();
    Click(f.app.thumbnail_items_[0].button);
    RODAK_CHECK(f.app.current_view_ == PhotosApp::ViewMode::kFullScreen);
    RODAK_CHECK(!lv_obj_has_flag(f.app.top_bar_, LV_OBJ_FLAG_HIDDEN));
    lv_test_mouse_click_at(160, 120); Pump();
    RODAK_CHECK(lv_obj_has_flag(f.app.top_bar_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(lv_obj_has_flag(f.app.bottom_bar_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(lv_obj_has_flag(f.app.previous_button_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(f.app.current_image_ != nullptr);
    SaveScreenshot("photos-fullscreen-clean.ppm");
    lv_test_mouse_click_at(160, 120); Pump();
    RODAK_CHECK(!lv_obj_has_flag(f.app.top_bar_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK_EQ(photo_test::decoded_buffers, 1);
}

RODAK_TEST("Photos thumbnail failures show the format and keep successful tiles") {
    Fixture f; f.Album(true);
    f.fs.Image(f.files.root.string(), f.files.Jpeg("good.jpg"));
    f.Start();
    RODAK_CHECK(f.app.thumbnail_items_[0].state == PhotosApp::ThumbnailState::kFailed);
    RODAK_CHECK(f.app.thumbnail_items_[0].placeholder != nullptr);
    RODAK_CHECK(Label(f.app.thumbnail_items_[0].button, "JPG") != nullptr);
    RODAK_CHECK(f.app.thumbnail_items_[1].thumbnail != nullptr);
    RODAK_CHECK(f.app.thumbnail_items_[1].placeholder == nullptr);
    SaveScreenshot("photos-grid.ppm");
    // A failed tile still opens; the viewer explains the failure and repairs it on retry.
    Click(f.app.thumbnail_items_[0].button);
    RODAK_CHECK(Label(lv_screen_active(), "打不开这张照片") != nullptr);
    RODAK_CHECK(Label(lv_screen_active(), "文件已损坏") != nullptr);
    f.files.Jpeg("broken.jpg");
    Click(f.app.retry_button_);
    RODAK_CHECK(f.app.current_image_ != nullptr);
    f.app.BackToGrid(); Pump();
    RODAK_CHECK(f.app.thumbnail_items_[0].thumbnail != nullptr);
    RODAK_CHECK(f.app.thumbnail_items_[0].placeholder == nullptr);
}

RODAK_TEST("Photos swipes turn once on release without opening a tile or wrapping") {
    Fixture f;
    f.fs.Album(f.files.root.string());
    for (int i = 0; i < 9; ++i) {
        f.fs.Image(f.files.root.string(), f.files.Jpeg("photo-" + std::to_string(i) + ".jpg"));
    }
    f.Start();

    Swipe(84, 80, 40, 0, true);
    RODAK_CHECK(f.app.current_view_ == PhotosApp::ViewMode::kGrid);
    RODAK_CHECK_EQ(f.app.current_page_, 0U);
    Swipe(84, 80, -40, 28);
    RODAK_CHECK_EQ(f.app.current_page_, 0U);
    Swipe(84, 80, -40, 0, true);
    RODAK_CHECK(f.app.current_view_ == PhotosApp::ViewMode::kGrid);
    RODAK_CHECK_EQ(f.app.current_page_, 1U);
    Swipe(84, 80, -40, 0, true);
    RODAK_CHECK(f.app.current_view_ == PhotosApp::ViewMode::kGrid);
    RODAK_CHECK_EQ(f.app.current_page_, 1U);
    Click(f.app.thumbnail_items_[0].image);
    RODAK_CHECK_EQ(f.app.current_photo_index_, 8U);
    RODAK_CHECK(lv_obj_has_state(f.app.next_button_, LV_STATE_DISABLED));
    Swipe(160, 115, -40);
    RODAK_CHECK_EQ(f.app.current_photo_index_, 8U);
    Click(f.app.back_button_);
    RODAK_CHECK_EQ(f.app.current_page_, 1U);
    Click(f.app.back_button_);
    RODAK_CHECK_EQ(f.navigation.home_count, 1);
}

RODAK_TEST("Photos draws the viewer before decoding and releases thumbnails first") {
    Fixture f;
    f.Album();
    struct Observation {
        Fixture* fixture;
        bool preview_drawn = false;
    } observation{&f};
    const auto observe = [](lv_event_t* event) {
        auto& state = *static_cast<Observation*>(lv_event_get_user_data(event));
        const auto& fixture = *state.fixture;
        if (fixture.app.current_view_ == PhotosApp::ViewMode::kFullScreen &&
            Label(lv_screen_active(), "1 / 1") != nullptr &&
            fixture.app.thumbnail_items_.empty() && photo_test::decoded_buffers == 0) {
            state.preview_drawn = true;
        }
    };
    auto* display = lv_display_get_default();
    lv_display_add_event_cb(display, observe, LV_EVENT_REFR_READY, &observation);
    const auto remove_observer = [&](Observation* state) {
        lv_display_remove_event_cb_with_user_data(display, observe, state);
    };
    std::unique_ptr<Observation, decltype(remove_observer)> observer(&observation, remove_observer);
    f.Start();
    RODAK_CHECK(!f.app.thumbnail_items_.empty());
    Click(f.app.thumbnail_items_[0].button);
    RODAK_CHECK(observation.preview_drawn);
    RODAK_CHECK_EQ(photo_test::decoded_buffers, 1);
}

RODAK_TEST("Photos timer allocation failure keeps navigation and retry usable without an automatic loop") {
    Fixture f; f.Album(); photo_test::fail_timer=true; f.Start();
    RODAK_CHECK(f.app.thumbnail_timer_==nullptr);
    RODAK_CHECK(Label(lv_screen_active(),"缩略图没有加载")!=nullptr);
    SaveScreenshot("photos-timer-failure.ppm");
    Pump(3000);
    RODAK_CHECK_EQ(photo_test::decoded_buffers,0);
    Click(f.app.retry_button_);
    RODAK_CHECK(f.app.thumbnail_timer_!=nullptr);
    RODAK_CHECK(f.app.thumbnail_items_[0].thumbnail!=nullptr);
}

RODAK_TEST("Photo preview retry replaces failures and back or destruction release every image") {
    Fixture f; f.Album(); f.Start();
    auto& tile = f.app.thumbnail_items_[0];
    RODAK_CHECK(tile.thumbnail != nullptr);
    RODAK_CHECK(tile.placeholder == nullptr);
    lv_image_set_inner_align(tile.image, LV_IMAGE_ALIGN_STRETCH);
    Pump();
    lv_area_t pixels;
    lv_obj_get_coords(tile.image, &pixels);
    lv_point_t center{(pixels.x1 + pixels.x2) / 2, (pixels.y1 + pixels.y2) / 2};
    RODAK_CHECK(lv_indev_search_obj(lv_screen_active(), &center) == tile.button);
    Click(tile.image);
    RODAK_CHECK(f.app.current_view_ == PhotosApp::ViewMode::kFullScreen);
    RODAK_CHECK(f.app.current_image_ != nullptr);
    f.app.BackToGrid(); Pump();
    const auto path=f.app.photos_[0].path;
    std::filesystem::remove(path);
    Click(f.app.thumbnail_items_[0].button);
    RODAK_CHECK(Label(lv_screen_active(),"无法读取照片")!=nullptr);
    RODAK_CHECK(Label(lv_screen_active(),"1 / 1")!=nullptr);
    // A failed photo keeps the bars visible even after a tap.
    lv_test_mouse_click_at(160, 200); Pump();
    RODAK_CHECK(!lv_obj_has_flag(f.app.top_bar_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(ObjectCount(f.app.root_) <= 21U);
    SaveScreenshot("photos-preview-error.ppm");
    RODAK_CHECK(f.app.current_image_==nullptr);
    RODAK_CHECK_EQ(photo_test::decoded_buffers,0);
    f.files.Jpeg("one.jpg"); Click(f.app.retry_button_);
    RODAK_CHECK(f.app.current_image_!=nullptr);
    RODAK_CHECK_EQ(photo_test::decoded_buffers,1);
    SaveScreenshot("photos-preview-recovered.ppm");
    f.app.BackToGrid(); Pump();
    RODAK_CHECK(f.app.current_image_==nullptr);
    f.app.ShowFullScreen(0); Pump();
    f.app.OnDestroy(); f.started=false; Pump(3000);
    RODAK_CHECK_EQ(photo_test::decoded_buffers,0);
    RODAK_CHECK(photo_test::image_buffers.empty());
}

RODAK_TEST("Photo preview allocation failure exposes the precise resource status and can retry") {
    Fixture f; f.Album(); f.Start();
    photo_test::fail_allocations=2;
    Click(f.app.thumbnail_items_[0].button);
    RODAK_CHECK(Label(lv_screen_active(),"图片太大，内存不够")!=nullptr);
    RODAK_CHECK_EQ(photo_test::decoded_buffers,0);
    Click(f.app.retry_button_);
    RODAK_CHECK(f.app.current_image_!=nullptr);
}

RODAK_TEST("Photos cancels queued navigation and thumbnail work when destroyed") {
    Fixture f; f.Album(); f.Start();
    f.app.NavigateHome();
    f.app.OnDestroy(); f.started=false; Pump(3000);
    RODAK_CHECK_EQ(f.navigation.home_count,0);
    RODAK_CHECK_EQ(photo_test::decoded_buffers,0);
    RODAK_CHECK(photo_test::image_buffers.empty());
}

RODAK_TEST("Photos failed Home scheduling is visible and can be explicitly retried") {
    Fixture f; f.Start();
    photo_test::fail_async=true; f.app.NavigateHome(); Pump();
    RODAK_CHECK_EQ(f.navigation.home_count,0);
    RODAK_CHECK(Label(lv_screen_active(),"暂时无法返回桌面")!=nullptr);
    f.app.NavigateHome(); Pump();
    RODAK_CHECK_EQ(f.navigation.home_count,1);
}

RODAK_TEST("Photos renders real PNG and BMP sources then frees them on back and rescan") {
    Fixture f;
    f.fs.Album(f.files.root.string());
    f.fs.Image(f.files.root.string(),f.files.Png("a.png"));
    f.fs.Image(f.files.root.string(),f.files.Bmp("b.bmp"));
    f.Start();
    for (size_t index=0;index<2;++index) {
        f.app.ShowFullScreen(index); Pump();
        RODAK_CHECK(f.app.current_image_!=nullptr);
        RODAK_CHECK(Label(lv_screen_active(),index==0?"b.bmp":"a.png")!=nullptr);
        f.app.BackToGrid(); Pump();
        RODAK_CHECK(f.app.current_image_==nullptr);
        RODAK_CHECK(photo_test::image_buffers.empty());
    }
    Click(f.app.refresh_button_);
    RODAK_CHECK(photo_test::image_buffers.empty());
}
