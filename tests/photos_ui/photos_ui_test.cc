#include "test_framework.h"
#include "host_images.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_common.h"
#include "apps/photos/photos_app.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_ui.h"
#include "settings.h"
#include <src/others/test/lv_test.h>
#include <cstdio>

namespace {
void Pump(uint32_t ms=250) { lv_test_wait(ms); lv_obj_update_layout(lv_screen_active()); }
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
void Click(lv_obj_t* button) {
    lv_obj_update_layout(button); lv_area_t area;
    lv_obj_get_coords(button,&area);
    RODAK_CHECK(area.x1>=0 && area.x2<320 && area.y1>=0 && area.y2<240);
    lv_test_mouse_click_at((area.x1+area.x2)/2,(area.y1+area.y2)/2);
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
        fs.Image(files.root.string(),corrupt ? files.Save("broken.jpg",{1,2,3}) : files.Jpeg("one.jpg"));
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
    RODAK_CHECK(Label(lv_screen_active(),"File service unavailable")!=nullptr);
    f.services.SetFileService(&f.fs); f.fs.mounted=false; f.fs.mount_ok=false;
    Click(f.app.refresh_button_);
    RODAK_CHECK(Label(lv_screen_active(),"SD card unavailable")!=nullptr);
    f.fs.mount_ok=true; f.fs.failures["/"]=EIO;
    Click(f.app.refresh_button_);
    RODAK_CHECK(Label(lv_screen_active(),"Could not read photos")!=nullptr);
    RODAK_CHECK(Label(lv_screen_active(),"No supported photos")==nullptr);
    SaveScreenshot("photos-read-error.ppm");
    f.fs.failures.clear(); Click(f.app.refresh_button_);
    RODAK_CHECK(Label(lv_screen_active(),"No supported photos")!=nullptr);
    RODAK_CHECK(Label(lv_screen_active(),"Photos")!=nullptr);
}

RODAK_TEST("Photos failed rescan clears stale tiles and nested I/O errors never look empty") {
    Fixture f; f.Album(); f.Start();
    RODAK_CHECK_EQ(f.app.photos_.size(),1U);
    f.fs.directories[f.files.root.string()].push_back({"sub","/failed",true,0,0});
    f.fs.failures["/failed"]=EIO;
    Click(f.app.refresh_button_);
    RODAK_CHECK(f.app.photos_.empty());
    RODAK_CHECK(f.app.thumbnail_items_.empty());
    RODAK_CHECK(Label(lv_screen_active(),"Could not read photos")!=nullptr);
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
    RODAK_CHECK_EQ(f.app.thumbnail_items_.size(), 6U);
    RODAK_CHECK(Label(lv_screen_active(), "41 photos  1/7") != nullptr);
    RODAK_CHECK(lv_obj_has_state(f.app.previous_page_button_, LV_STATE_DISABLED));
    RODAK_CHECK(!lv_obj_has_state(f.app.next_page_button_, LV_STATE_DISABLED));

    Click(f.app.next_page_button_);
    RODAK_CHECK_EQ(f.app.current_page_, 1U);
    RODAK_CHECK_EQ(f.app.thumbnail_items_.size(), 6U);
    RODAK_CHECK_EQ(f.app.thumbnail_items_[0].photo_index, 6U);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.thumbnail_items_[0].label)), "photo-06.jpg");
    RODAK_CHECK(Label(lv_screen_active(), "41 photos  2/7") != nullptr);

    for (size_t page = 2; page < 7; ++page) Click(f.app.next_page_button_);
    RODAK_CHECK_EQ(f.app.current_page_, 6U);
    RODAK_CHECK_EQ(f.app.thumbnail_items_.size(), 5U);
    RODAK_CHECK_EQ(f.app.thumbnail_items_[0].photo_index, 36U);
    RODAK_CHECK(lv_obj_has_state(f.app.next_page_button_, LV_STATE_DISABLED));
    RODAK_CHECK(Label(lv_screen_active(), "41 photos  7/7") != nullptr);

    Click(f.app.thumbnail_items_[0].button);
    RODAK_CHECK_EQ(f.app.current_photo_index_, 36U);
    RODAK_CHECK(Label(lv_screen_active(), "photo-36.jpg") != nullptr);
}

RODAK_TEST("Photos thumbnail failures stay visible and explicit retry clears them after recovery") {
    Fixture f; f.Album(true); f.Start();
    RODAK_CHECK(Label(lv_screen_active(),"Preview failed - tap Retry")!=nullptr);
    RODAK_CHECK(f.app.thumbnail_items_[0].thumbnail_unavailable);
    f.files.Jpeg("broken.jpg");
    Click(f.app.refresh_button_);
    RODAK_CHECK(f.app.thumbnail_items_[0].thumbnail!=nullptr);
    RODAK_CHECK(Label(lv_screen_active(),"Preview failed - tap Retry")==nullptr);
    RODAK_CHECK_EQ(photo_test::decoded_buffers,1);
}

RODAK_TEST("Photos timer allocation failure keeps navigation and retry usable without an automatic loop") {
    Fixture f; f.Album(); photo_test::fail_timer=true; f.Start();
    RODAK_CHECK(f.app.thumbnail_timer_==nullptr);
    RODAK_CHECK(Label(lv_screen_active(),"Preview failed - tap Retry")!=nullptr);
    SaveScreenshot("photos-timer-failure.ppm");
    Pump(3000);
    RODAK_CHECK_EQ(photo_test::decoded_buffers,0);
    Click(f.app.refresh_button_);
    RODAK_CHECK(f.app.thumbnail_timer_!=nullptr);
    RODAK_CHECK(f.app.thumbnail_items_[0].thumbnail!=nullptr);
}

RODAK_TEST("Photo preview retry replaces failures and back or destruction release every image") {
    Fixture f; f.Album(); f.Start();
    auto& tile = f.app.thumbnail_items_[0];
    RODAK_CHECK(tile.thumbnail != nullptr);
    RODAK_CHECK(lv_obj_has_flag(tile.label, LV_OBJ_FLAG_HIDDEN));
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
    RODAK_CHECK(Label(lv_screen_active(),"Could not read image")!=nullptr);
    SaveScreenshot("photos-preview-error.ppm");
    RODAK_CHECK(f.app.current_image_==nullptr);
    RODAK_CHECK_EQ(photo_test::decoded_buffers,0);
    f.files.Jpeg("one.jpg"); Click(f.app.preview_retry_button_);
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
    RODAK_CHECK(Label(lv_screen_active(),"Not enough image memory")!=nullptr);
    RODAK_CHECK_EQ(photo_test::decoded_buffers,0);
    Click(f.app.preview_retry_button_);
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

RODAK_TEST("Returning from a repaired fullscreen image clears old thumbnail failure guidance") {
    Fixture f; f.Album(true); f.Start();
    RODAK_CHECK(Label(lv_screen_active(),"Preview failed - tap Retry")!=nullptr);
    Click(f.app.thumbnail_items_[0].button);
    f.files.Jpeg("broken.jpg"); Click(f.app.preview_retry_button_);
    f.app.BackToGrid(); Pump();
    RODAK_CHECK(f.app.thumbnail_items_[0].thumbnail!=nullptr);
    RODAK_CHECK(Label(lv_screen_active(),"Preview failed - tap Retry")==nullptr);
}

RODAK_TEST("Photos failed Home scheduling is visible and can be explicitly retried") {
    Fixture f; f.Start();
    photo_test::fail_async=true; f.app.NavigateHome(); Pump();
    RODAK_CHECK_EQ(f.navigation.home_count,0);
    RODAK_CHECK(Label(lv_screen_active(),"Navigation unavailable")!=nullptr);
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
        RODAK_CHECK(Label(lv_screen_active(),index==0?"a.png":"b.bmp")!=nullptr);
        f.app.BackToGrid(); Pump();
        RODAK_CHECK(f.app.current_image_==nullptr);
        RODAK_CHECK(photo_test::image_buffers.empty());
    }
    Click(f.app.refresh_button_);
    RODAK_CHECK(photo_test::image_buffers.empty());
}
