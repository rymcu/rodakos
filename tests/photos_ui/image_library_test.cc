#include "test_framework.h"
#include "host_images.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_common.h"
#include "phone_ui/image_library.h"
#include "phone_os/resource_failure_injection.h"
#include <algorithm>
#include <iterator>
#include <src/draw/lv_draw_buf_private.h>
#include <src/draw/lv_image_decoder_private.h>
#include <src/misc/cache/instance/lv_image_header_cache.h>

using namespace rodakos::ImageLibrary;

namespace {
void* RejectDrawBufferAllocation(size_t, lv_color_format_t) { return nullptr; }

class ScopedImageBufferAllocationFailure {
public:
    ScopedImageBufferAllocationFailure()
        : handlers_(lv_draw_buf_get_image_handlers()), original_(handlers_->buf_malloc_cb) {
        handlers_->buf_malloc_cb = RejectDrawBufferAllocation;
    }
    ~ScopedImageBufferAllocationFailure() { handlers_->buf_malloc_cb = original_; }
private:
    lv_draw_buf_handlers_t* handlers_;
    lv_draw_buf_malloc_cb original_;
};
}  // namespace

RODAK_TEST("Photo scan distinguishes absent service, failed mount, and an empty mounted card") {
    photo_test::Files fs;
    RODAK_CHECK(ScanPhotoLibrary(nullptr).status == ImageScanStatus::kServiceUnavailable);
    fs.mounted=false; fs.mount_ok=false;
    RODAK_CHECK(ScanPhotoLibrary(&fs).status == ImageScanStatus::kStorageUnavailable);
    fs.mount_ok=true;
    auto result=ScanPhotoLibrary(&fs);
    RODAK_CHECK(result.status == ImageScanStatus::kReady);
    RODAK_CHECK(result.paths.empty());
}

RODAK_TEST("Absent optional albums fall back to root without treating unreadable root as empty") {
    photo_test::Files fs;
    fs.Image("/", "/root.jpg");
    auto result=ScanPhotoLibrary(&fs);
    RODAK_CHECK(result.status == ImageScanStatus::kReady);
    RODAK_CHECK_EQ(result.paths.size(), 1U);
    for (auto error : {EIO, ENOENT, ENODEV, 0}) {
        fs.failures["/"]=error;
        result=ScanPhotoLibrary(&fs);
        RODAK_CHECK(result.status != ImageScanStatus::kReady);
        RODAK_CHECK(result.paths.empty());
        RODAK_CHECK_EQ(result.failed_path, "/");
    }
}

RODAK_TEST("A failed preferred album or nested directory discards every partial photo") {
    photo_test::Files fs;
    fs.Album("/photos"); fs.Image("/photos", "/photos/first.jpg");
    fs.directories["/photos"].push_back({"nested", "/photos/nested", true, 0, 0});
    fs.failures["/photos/nested"]=EIO;
    auto result=ScanPhotoLibrary(&fs);
    RODAK_CHECK(result.status == ImageScanStatus::kReadFailed);
    RODAK_CHECK(result.paths.empty());
    RODAK_CHECK_EQ(result.failed_path, "/photos/nested");
    RODAK_CHECK(ScanImagesWithFileService(&fs,"/photos",3).empty());
    fs.failures.clear(); fs.directories["/photos/nested"]={};
    fs.directories["/"].push_back({"DCIM", "/DCIM", true, 0, 0});
    fs.failures["/DCIM"]=ENOENT;
    result=ScanPhotoLibrary(&fs);
    RODAK_CHECK(result.status == ImageScanStatus::kDirectoryMissing);
    RODAK_CHECK(result.paths.empty());
}

RODAK_TEST("Depth bounds skip deeper directories but never suppress a failure inside the scan") {
    photo_test::Files fs;
    fs.directories["/"]={{"child","/child",true,0,0}};
    fs.Image("/", "/z.JPG"); fs.Image("/", "/A.png"); fs.Image("/", "/skip.txt");
    fs.failures["/child"]=EIO;
    auto shallow=ScanImagesWithFileServiceDetailed(&fs,"/",0);
    RODAK_CHECK(shallow.status == ImageScanStatus::kReady);
    RODAK_CHECK_EQ(shallow.paths.size(), 2U);
    RODAK_CHECK_EQ(shallow.paths.front(), "/A.png");
    RODAK_CHECK_EQ(std::count(fs.reads.begin(),fs.reads.end(),"/child"),0);
    RODAK_CHECK(ScanImagesWithFileServiceDetailed(&fs,"/",1).status == ImageScanStatus::kReadFailed);
    RODAK_CHECK(ScanImagesWithFileServiceDetailed(&fs,"/",-1).status == ImageScanStatus::kReadFailed);
}

RODAK_TEST("JPEG display and thumbnail use the production loader and release native buffers") {
    photo_test::ResetFailures(); photo_test::TestFiles files;
    const auto path=files.Jpeg("valid.jpg");
    {
        auto result=LoadImageForDisplayDetailed(path);
        RODAK_CHECK(result.status == ImageLoadStatus::kLoaded);
        RODAK_CHECK(result.image != nullptr);
        RODAK_CHECK_EQ(result.image->GetImageDescriptor()->header.w,2U);
        RODAK_CHECK_EQ(photo_test::decoded_buffers,1);
        RODAK_CHECK(photo_test::image_buffers.empty());
        auto thumbnail=LoadThumbnail(path,84,84);
        RODAK_CHECK(thumbnail != nullptr);
        RODAK_CHECK_EQ(photo_test::decoded_buffers,2);
    }
    RODAK_CHECK_EQ(photo_test::decoded_buffers,0);
    RODAK_CHECK_EQ(photo_test::scoped_decodes,2);
    RODAK_CHECK_EQ(photo_test::unscoped_decodes,0);
}

RODAK_TEST("Image reads report actual open seek short-read and close failures without retaining buffers") {
    photo_test::ResetFailures(); photo_test::TestFiles files;
    RODAK_CHECK(LoadImageForDisplayDetailed((files.root/"missing.jpg").string()).status == ImageLoadStatus::kReadFailed);
    const auto path=files.Jpeg("valid.jpg");
    for (auto* failure : {&photo_test::fail_seek,&photo_test::short_read,&photo_test::fail_close}) {
        *failure=true;
        auto result=LoadImageForDisplayDetailed(path);
        RODAK_CHECK(result.status == ImageLoadStatus::kReadFailed);
        RODAK_CHECK(result.image == nullptr);
        RODAK_CHECK(photo_test::image_buffers.empty());
        RODAK_CHECK_EQ(photo_test::decoded_buffers,0);
    }
}

RODAK_TEST("Unsupported corrupt and allocation failures remain distinct and partial decode is released") {
    photo_test::ResetFailures(); photo_test::TestFiles files;
    const auto path=files.Jpeg("valid.jpg");
    RODAK_CHECK(LoadImageForDisplayDetailed("unsupported.gif").status == ImageLoadStatus::kUnsupported);
    RODAK_CHECK(LoadImageForDisplayDetailed(files.Save("bad.jpg",{1,2,3})).status == ImageLoadStatus::kDecodeFailed);
    RODAK_CHECK(LoadImageForDisplayDetailed(files.Save("empty.jpg",{})).status == ImageLoadStatus::kDecodeFailed);
    rodakos::ArmResourceFailure("image");
    RODAK_CHECK(LoadImageForDisplayDetailed(path).status == ImageLoadStatus::kInsufficientMemory);
    photo_test::fail_allocations=2;
    RODAK_CHECK(LoadThumbnailDetailed(path,84,84).status == ImageLoadStatus::kInsufficientMemory);
    photo_test::decode_partial_failure=true;
    for (int result : {ESP_FAIL,ESP_ERR_NO_MEM}) {
        photo_test::decode_result=result;
        const auto image=LoadImageForDisplayDetailed(path);
        RODAK_CHECK(image.status == (result==ESP_ERR_NO_MEM ? ImageLoadStatus::kInsufficientMemory : ImageLoadStatus::kDecodeFailed));
        RODAK_CHECK(image.image == nullptr);
        RODAK_CHECK(photo_test::image_buffers.empty());
        RODAK_CHECK_EQ(photo_test::decoded_buffers,0);
    }
    photo_test::ResetFailures();
}

RODAK_TEST("BMP stays a real LVGL filesystem source with readable and corrupt-file checks") {
    photo_test::ResetFailures(); photo_test::TestFiles files;
    const auto path=files.Bmp("good.bmp");
    auto result=LoadImageForDisplayDetailed(path);
    RODAK_CHECK(result.status == ImageLoadStatus::kLoaded);
    RODAK_CHECK(result.image->GetImageDescriptor() == nullptr);
    RODAK_CHECK_EQ(std::string(static_cast<const char*>(result.image->GetImageSource())),"S:"+path);
    lv_image_header_t header{};
    RODAK_CHECK(lv_image_decoder_get_info(result.image->GetImageSource(),&header)==LV_RESULT_OK);
    RODAK_CHECK_EQ(header.w,2U);
    RODAK_CHECK(LoadImageForDisplayDetailed(files.Save("bad.bmp",{1,2,3})).status == ImageLoadStatus::kDecodeFailed);
    RODAK_CHECK(LoadThumbnailDetailed(path,84,84).status == ImageLoadStatus::kUnsupported);
}

RODAK_TEST("BMP invalid magic truncated pixels palette compression and invalid offsets never load") {
    photo_test::TestFiles files;
    const auto good=files.Bmp("good.bmp");
    std::ifstream file(good,std::ios::binary);
    const std::vector<uint8_t> original{std::istreambuf_iterator<char>(file),{}};
    for (int scenario=0;scenario<6;++scenario) {
        auto bytes=original;
        if (scenario==0) bytes[0]='X';
        if (scenario==1) bytes.resize(54);
        if (scenario==2) bytes[28]=8;
        if (scenario==3) bytes[30]=1;
        if (scenario==4) bytes[10]=8;
        if (scenario==5) bytes[22]=0xff,bytes[23]=0xff,bytes[24]=0xff,bytes[25]=0x7f;
        auto result=LoadImageForDisplayDetailed(files.Save("bad"+std::to_string(scenario)+".bmp",bytes));
        RODAK_CHECK(result.status!=ImageLoadStatus::kLoaded);
        RODAK_CHECK(result.image==nullptr);
    }
}

RODAK_TEST("BMP keeps RGB565 bitfields and RGB32 while current pixel I/O failures stay visible") {
    photo_test::ResetFailures(); photo_test::TestFiles files;
    const auto good=files.Bmp("good.bmp");
    std::ifstream file(good,std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
    bytes[28]=32;
    RODAK_CHECK(LoadImageForDisplayDetailed(files.Save("rgb32.bmp",bytes)).status==ImageLoadStatus::kLoaded);
    bytes[10]=66; bytes[18]=1; bytes[22]=1; bytes[28]=16; bytes[30]=3; bytes[34]=4;
    std::fill(bytes.begin()+54,bytes.end(),0);
    bytes[55]=0xf8; bytes[58]=0xe0; bytes[59]=7; bytes[62]=0x1f;
    RODAK_CHECK(LoadImageForDisplayDetailed(files.Save("rgb565.bmp",bytes)).status==ImageLoadStatus::kLoaded);
    bytes[30]=0;
    RODAK_CHECK(LoadImageForDisplayDetailed(files.Save("rgb555.bmp",bytes)).status==ImageLoadStatus::kUnsupported);
    photo_test::short_read_after=1;
    RODAK_CHECK(LoadImageForDisplayDetailed(good).status==ImageLoadStatus::kReadFailed);
    photo_test::ResetFailures();
}

RODAK_TEST("BMP explicit reload replaces cached dimensions after the same file changes") {
    struct HeaderCache {
        HeaderCache() { lv_image_header_cache_resize(4,true); }
        ~HeaderCache() { lv_image_header_cache_resize(0,true); }
    } cache;
    RODAK_CHECK(lv_image_header_cache_is_enabled());
    photo_test::TestFiles files;
    const auto path=files.Bmp("same.bmp");
    auto before=LoadImageForDisplayDetailed(path);
    RODAK_CHECK(before.status==ImageLoadStatus::kLoaded);
    std::ifstream file(path,std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
    bytes[18]=1;
    files.Save("same.bmp",bytes);
    auto after=LoadImageForDisplayDetailed(path);
    RODAK_CHECK(after.status==ImageLoadStatus::kLoaded);
    lv_image_header_t header{};
    RODAK_CHECK(lv_image_decoder_get_info(after.image->GetImageSource(),&header)==LV_RESULT_OK);
    RODAK_CHECK_EQ(header.w,1U);
}

RODAK_TEST("PNG keeps the successful real decode and remains drawable without decoding again") {
    photo_test::ResetFailures(); photo_test::TestFiles files;
    const auto png=files.Png("good.png");
    {
        auto result=LoadImageForDisplayDetailed(png);
        RODAK_CHECK(result.status==ImageLoadStatus::kLoaded);
        RODAK_CHECK(result.image->GetImageDescriptor()->header.cf==LV_COLOR_FORMAT_ARGB8888);
        RODAK_CHECK_EQ(result.image->GetImageDescriptor()->data[0],0U);
        RODAK_CHECK_EQ(result.image->GetImageDescriptor()->data[1],128U);
        RODAK_CHECK_EQ(result.image->GetImageDescriptor()->data[2],255U);
        RODAK_CHECK_EQ(result.image->GetImageDescriptor()->data[3],255U);
        lv_image_decoder_dsc_t decoder{}; lv_image_decoder_args_t args{}; args.no_cache=true;
        RODAK_CHECK(lv_image_decoder_open(&decoder,result.image->GetImageSource(),&args)==LV_RESULT_OK);
        RODAK_CHECK(decoder.decoded!=nullptr);
        RODAK_CHECK_EQ(decoder.header.w,1U);
        lv_image_decoder_close(&decoder);
    }
    RODAK_CHECK(photo_test::image_buffers.empty());
    std::ifstream file(png,std::ios::binary);
    const std::vector<uint8_t> original{std::istreambuf_iterator<char>(file),{}};
    for (int scenario=0;scenario<12;++scenario) {
        auto bytes=original;
        if (scenario%2==0) bytes.resize(24);
        else bytes[45]^=0xff;
        auto result=LoadImageForDisplayDetailed(files.Save("retry.png",bytes));
        RODAK_CHECK(result.status==ImageLoadStatus::kDecodeFailed);
        RODAK_CHECK(result.image==nullptr);
        RODAK_CHECK(photo_test::image_buffers.empty());
        auto recovered=LoadImageForDisplayDetailed(files.Png("retry.png"));
        RODAK_CHECK(recovered.status==ImageLoadStatus::kLoaded);
    }
    RODAK_CHECK(photo_test::image_buffers.empty());
}

RODAK_TEST("PNG RGBA8 reuses scanlines while RGB8 allocation error 83 remains distinct") {
    photo_test::ResetFailures(); photo_test::TestFiles files;
    ImageLoadResult result;
    {
        ScopedImageBufferAllocationFailure failure;
        result=LoadImageForDisplayDetailed(files.Png("in-place.png"));
    }
    RODAK_CHECK(result.status==ImageLoadStatus::kLoaded);
    RODAK_CHECK(result.image!=nullptr);
    result.image.reset();
    RODAK_CHECK(photo_test::image_buffers.empty());
    {
        ScopedImageBufferAllocationFailure failure;
        result=LoadImageForDisplayDetailed(files.PngRgb("oom.png"));
    }
    RODAK_CHECK(result.status==ImageLoadStatus::kInsufficientMemory);
    RODAK_CHECK(result.image==nullptr);
    RODAK_CHECK(photo_test::image_buffers.empty());
}

RODAK_TEST("Image status text does not guess corruption or resource failure from generic decoder errors") {
    RODAK_CHECK_EQ(std::string(ImageLoadErrorText(ImageLoadStatus::kLoaded)), "");
    RODAK_CHECK_EQ(std::string(ImageLoadErrorText(ImageLoadStatus::kUnsupported)), "Unsupported image format");
    RODAK_CHECK_EQ(std::string(ImageLoadErrorText(ImageLoadStatus::kReadFailed)), "Could not read image");
    RODAK_CHECK_EQ(std::string(ImageLoadErrorText(ImageLoadStatus::kInsufficientMemory)), "Not enough image memory");
    RODAK_CHECK_EQ(std::string(ImageLoadErrorText(ImageLoadStatus::kDecodeFailed)), "Could not load image");
}
