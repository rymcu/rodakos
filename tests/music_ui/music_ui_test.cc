#include "test_framework.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#define private public
#include "apps/music/music_app.h"
#undef private
#include "phone_os/music_player_service.h"
#include "phone_os/audio_output_service.h"
#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_ui/phone_ui.h"
#include "rodakos_adapters/file_service.h"
#include "settings.h"
#include "esp_codec_dev.h"
#include <src/others/test/lv_test.h>

namespace {
using namespace rodakos;
void Await(const std::function<bool()>& condition) {
    for (int i=0; i<400; ++i) {
        if (condition()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    RODAK_CHECK(condition());
}
class Files : public FileService {
public:
    std::atomic<bool> mounted{true}, mount_ok{true};
    std::mutex mutex;
    std::condition_variable cv;
    bool hold=false, entered=false;
    std::string failed_path;
    std::map<std::string, std::vector<FileEntry>> directories{{"/", {}}, {"/music", {}}};
    bool Init() override { mounted=mount_ok.load(); return mounted; }
    void Deinit() override { mounted=false; }
    bool IsMounted() const override { return mounted; }
    const char* GetMountPoint() const override { return "/host-music"; }
    FileSystemType GetFileSystemType() const override { return FileSystemType::FATFS; }
    MediumType GetMediumType() const override { return MediumType::SDCard; }
    bool GetCapacity(Capacity&) override { return false; }
    bool ListDirectory(const std::string& path, std::vector<FileEntry>& entries) override {
        std::unique_lock<std::mutex> lock(mutex);
        entered=true; cv.notify_all();
        cv.wait(lock,[&]{return !hold;});
        if (path==failed_path) return false;
        const auto found=directories.find(path);
        if (found==directories.end()) return false;
        entries=found->second; return true;
    }
    void Fail(const std::string& path) { std::lock_guard<std::mutex> lock(mutex); failed_path=path; }
    void Hold() { std::lock_guard<std::mutex> lock(mutex); entered=false; hold=true; }
    void Release() { std::lock_guard<std::mutex> lock(mutex); hold=false; cv.notify_all(); }
    bool Entered() { std::lock_guard<std::mutex> lock(mutex); return entered; }
    bool ReadFile(const std::string&, std::vector<uint8_t>&) override { return false; }
    bool WriteFile(const std::string&, const std::vector<uint8_t>&, bool) override { return false; }
    bool DeleteFile(const std::string&) override { return false; }
    bool DeleteDirectory(const std::string&) override { return false; }
    bool CreateDirectory(const std::string&) override { return false; }
    bool Rename(const std::string&, const std::string&) override { return false; }
    bool Exists(const std::string&) override { return false; }
    size_t GetFileSize(const std::string&) override { return 0; }
    void Tracks(const std::vector<std::string>& paths) {
        std::lock_guard<std::mutex> lock(mutex);
        directories["/"]={{"music", "/music", true, 0, 0}};
        directories["/music"].clear();
        for(const auto& path:paths) directories["/music"].push_back(
            {std::filesystem::path(path).filename().string(),path,false,128,0});
    }
};
struct Fixture {
    Files files;
    AudioOutputService output;
    AudioService audio{output};
    MusicPlayerService player;
    PhoneUi ui{320,240};
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context{ui,navigation,registry,services,settings};
    MusicApp app;
    bool created=false;
    explicit Fixture(bool storage=true):player(audio,storage?&files:nullptr) {
        fake_codec::Reset(); music_test::fail_monitor=false;
        services.SetMusicPlayer(&player);
    }
    ~Fixture() {
        files.Release();
        if(created) app.OnDestroy();
        player.Deinit(); audio.Deinit(); music_test::JoinTasks();
        lv_test_wait(5000);
        lv_obj_clean(lv_screen_active()); lv_obj_clean(lv_layer_top());
        music_test::fail_monitor=false;
    }
    void Create() { RODAK_CHECK(app.OnCreate(context)); created=true; lv_test_wait(2); }
    void Refresh() { app.RefreshState(); lv_test_wait(2); }
    std::string Status() { return lv_label_get_text(app.status_label_); }
    void Retry() {
        const auto revision=player.GetState().library_revision;
        lv_obj_send_event(app.refresh_library_button_, LV_EVENT_CLICKED, nullptr);
        Await([&]{return player.GetState().library_revision>revision &&
            player.GetState().library_status!=MusicLibraryStatus::kScanning;});
        Refresh();
    }
};
std::string Wav(const std::string& name, uint32_t claimed, size_t actual) {
    auto path=std::filesystem::temp_directory_path()/(std::to_string(getpid())+"-"+name);
    std::array<uint8_t,44> h{};
    std::copy_n("RIFF",4,h.begin()); std::copy_n("WAVEfmt ",8,h.begin()+8);
    auto u16=[&](int offset,uint16_t value){h[offset]=value;h[offset+1]=value>>8;};
    auto u32=[&](int offset,uint32_t value){for(int i=0;i<4;++i)h[offset+i]=value>>(8*i);};
    u32(4,36+claimed); u32(16,16); u16(20,1);u16(22,1);u32(24,16000);
    u32(28,32000);u16(32,2);u16(34,16);std::copy_n("data",4,h.begin()+36);u32(40,claimed);
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(h.data()),h.size());
    std::vector<char> pcm(actual);out.write(pcm.data(),pcm.size());out.close();return path.string();
}
void Screenshot(const char* name) {
    lv_obj_update_layout(lv_screen_active());lv_test_wait(10);
    const auto* frame=lv_display_get_buf_active(lv_display_get_default());
    std::ofstream out(name,std::ios::binary);out<<"P6\n320 240\n255\n";
    for(int y=0;y<240;++y) for(int x=0;x<320;++x) {
        const auto* p=frame->data+y*frame->header.stride+x*4;
        const char rgb[]={static_cast<char>(p[2]),static_cast<char>(p[1]),static_cast<char>(p[0])};
        out.write(rgb,3);
    }
}
}

RODAK_TEST("missing storage service differs from an empty library") {
    Fixture f(false);f.Create();
    RODAK_CHECK_EQ(f.player.GetState().library_status,MusicLibraryStatus::kServiceUnavailable);
    RODAK_CHECK_EQ(f.Status(),"File service unavailable");
}
RODAK_TEST("missing card retries to genuine empty library through the real UI") {
    Fixture f;f.files.mounted=false;f.files.mount_ok=false;f.Create();
    RODAK_CHECK_EQ(f.player.GetState().library_status,MusicLibraryStatus::kStorageUnavailable);
    RODAK_CHECK(f.Status().find("SD card unavailable")!=std::string::npos);
    f.app.ShowTrackPicker();Screenshot("music-missing-card.ppm");
    f.files.mount_ok=true;f.Retry();
    RODAK_CHECK_EQ(f.player.GetState().library_status,MusicLibraryStatus::kEmpty);
    RODAK_CHECK(f.Status().find("No supported audio files")!=std::string::npos);
    Screenshot("music-empty.ppm");
}
RODAK_TEST("root read failure is not a successful empty scan") {
    Fixture f;f.files.Fail("/");f.Create();
    RODAK_CHECK_EQ(f.player.GetState().library_status,MusicLibraryStatus::kReadError);
    RODAK_CHECK_FALSE(f.player.ScanLibrary());
    RODAK_CHECK(f.Status().find("Cannot read music folders")!=std::string::npos);
}
RODAK_TEST("nested read failure discards partial and previous tracks") {
    Fixture f;f.files.Tracks({"/missing-old.wav"});f.Create();
    const auto before=f.player.GetState().library_revision;
    {
        std::lock_guard<std::mutex> lock(f.files.mutex);
        f.files.directories["/music"].push_back({"bad","/music/bad",true,0,0});
        f.files.failed_path="/music/bad";
    }
    RODAK_CHECK_FALSE(f.player.ScanLibrary());f.Refresh();
    RODAK_CHECK_EQ(f.player.track_count(),0U);
    RODAK_CHECK(f.player.GetTracks().empty());
    RODAK_CHECK_FALSE(f.player.PlayTrack(0,before));
    RODAK_CHECK_EQ(f.audio.GetState().status,AudioPlaybackStatus::kIdle);
}
RODAK_TEST("stale song row cannot play a replacement at the same index") {
    Fixture f;f.files.Tracks({"/old.wav"});f.Create();
    f.files.Tracks({"/replacement.wav"});RODAK_CHECK(f.player.ScanLibrary());
    f.app.PlayTrack(0);
    RODAK_CHECK(f.audio.GetState().file_path.empty());
    RODAK_CHECK(f.Status().find("Music library changed")!=std::string::npos);
}
RODAK_TEST("blocked next and toggle show the ownership error rather than no tracks") {
    Fixture f;f.files.Tracks({"/track.wav"});f.Create();
    RODAK_CHECK(f.player.SetPlaybackBlocked(true));
    f.app.PlayNext();RODAK_CHECK(f.Status().find("another app")!=std::string::npos);
    f.app.TogglePlayPause();RODAK_CHECK(f.Status().find("another app")!=std::string::npos);
    RODAK_CHECK(f.audio.GetState().file_path.empty());
}
RODAK_TEST("worker creation failure can retry and recover") {
    Fixture f;music_test::fail_monitor=true;f.Create();
    RODAK_CHECK(f.Status().find("worker unavailable")!=std::string::npos);
    music_test::fail_monitor=false;f.files.Tracks({"/recovered.wav"});f.Retry();
    RODAK_CHECK_EQ(f.player.GetState().library_status,MusicLibraryStatus::kReady);
    RODAK_CHECK_EQ(f.player.track_count(),1U);
}
RODAK_TEST("requested library scan publishes changed tracks") {
    Fixture f;f.files.Tracks({"/before.wav"});f.Create();
    const auto before = f.player.GetState().library_revision;
    f.files.Tracks({"/after.wav"});
    RODAK_CHECK(f.player.RequestLibraryScan());
    Await([&] {
        const auto state = f.player.GetState();
        return state.library_revision > before && state.library_status != MusicLibraryStatus::kScanning;
    });
    const auto tracks = f.player.GetTracks();
    RODAK_CHECK_EQ(tracks.size(), 1U);
    RODAK_CHECK_EQ(tracks.front().path, "/after.wav");
}
RODAK_TEST("retry does not hold LVGL during a slow directory read") {
    Fixture f;f.Create();f.files.Hold();f.app.RefreshLibrary();Await([&]{return f.files.Entered();});
    f.Refresh();RODAK_CHECK_EQ(f.Status(),"Scanning music...");
    f.app.PlayTrack(0);RODAK_CHECK(f.Status().find("Scanning")!=std::string::npos);
    const auto stop_start = std::chrono::steady_clock::now();
    f.player.Stop();
    RODAK_CHECK(std::chrono::steady_clock::now()-stop_start < std::chrono::milliseconds(100));
    f.files.Release();Await([&]{return f.player.GetState().library_status==MusicLibraryStatus::kEmpty;});
    f.Refresh();RODAK_CHECK(f.Status().find("No supported audio files")!=std::string::npos);
}
RODAK_TEST("deinit waits for an active scan and never publishes its late tracks") {
    Fixture f;f.Create();f.files.Hold();f.app.RefreshLibrary();Await([&]{return f.files.Entered();});
    std::atomic<bool> done{false};std::thread stop([&]{f.player.Deinit();done=true;});
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const bool stopped_early=done.load();f.files.Release();stop.join();
    RODAK_CHECK_FALSE(stopped_early);RODAK_CHECK(done.load());
    RODAK_CHECK_FALSE(f.player.RequestLibraryScan());
}
RODAK_TEST("asynchronous missing-file error is visible and a subsequent valid track recovers") {
    Fixture f;const auto valid=Wav("rodakos-music-valid.wav",64,64);
    f.files.Tracks({"/missing.wav",valid});f.Create();
    const auto tracks = f.player.GetTracks();
    const size_t valid_index = tracks[0].path == valid ? 0 : 1;
    const size_t missing_index = 1 - valid_index;
    RODAK_CHECK(f.player.PlayTrack(missing_index));
    Await([&]{return f.audio.GetState().status==AudioPlaybackStatus::kError;});
    f.Refresh();RODAK_CHECK_EQ(f.Status(),f.audio.GetState().message);
    RODAK_CHECK(f.player.PlayTrack(valid_index));
    Await([&]{return f.audio.GetState().status==AudioPlaybackStatus::kCompleted;});
    f.Refresh();RODAK_CHECK(f.Status().find("Completed")!=std::string::npos);
    std::filesystem::remove(valid);
}
RODAK_TEST("asynchronous codec write error after format discovery outranks progress in the real UI") {
    Fixture f;const auto broken=Wav("rodakos-music-output-failure.wav",128,128);
    fake_codec::fail_write=true;
    f.files.Tracks({broken});f.Create();RODAK_CHECK(f.player.PlayTrack(0));
    Await([&]{return f.audio.GetState().status==AudioPlaybackStatus::kError;});
    RODAK_CHECK(f.audio.GetState().sample_rate>0);f.Refresh();
    RODAK_CHECK_EQ(f.Status(),f.audio.GetState().message);
    RODAK_CHECK(f.Status().find("100%") == std::string::npos);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t title_area{}, status_area{}, card_area{};
    lv_obj_get_coords(f.app.track_title_label_, &title_area);
    lv_obj_get_coords(f.app.status_label_, &status_area);
    lv_obj_get_coords(lv_obj_get_parent(f.app.status_label_), &card_area);
    RODAK_CHECK(title_area.y2 < status_area.y1);
    RODAK_CHECK(status_area.y2 <= card_area.y2);
    const auto* status_font = lv_obj_get_style_text_font(f.app.status_label_, 0);
    RODAK_CHECK(2 * status_font->line_height + lv_obj_get_style_text_line_space(f.app.status_label_, 0)
                <= lv_obj_get_height(f.app.status_label_));
    Screenshot("music-playback-error.ppm");
    fake_codec::fail_write=false;
    RODAK_CHECK(f.player.PlayTrack(0));
    Await([&]{return f.audio.GetState().status==AudioPlaybackStatus::kCompleted;});
    f.Refresh();RODAK_CHECK(f.Status().find("Completed")!=std::string::npos);
    std::filesystem::remove(broken);
}
RODAK_TEST("rejected codec volume change immediately restores slider and label") {
    Fixture f;f.Create();RODAK_CHECK(f.output.OpenForOwner("test",16000,1,16));
    fake_codec::fail_volume_write=true;f.app.ShowVolumePanel();
    lv_slider_set_value(f.app.volume_slider_,90,LV_ANIM_OFF);
    lv_obj_send_event(f.app.volume_slider_,LV_EVENT_VALUE_CHANGED,nullptr);
    RODAK_CHECK_EQ(lv_slider_get_value(f.app.volume_slider_),60);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.volume_value_label_)),"60%");
    RODAK_CHECK_EQ(f.output.volume(),60);f.output.CloseForOwner("test");
}
RODAK_TEST("stop freezes completed-track auto advance") {
    Fixture f;const auto valid=Wav("rodakos-music-stop.wav",64,64);
    f.files.Tracks({valid,"/second.wav"});f.Create();
    auto tracks=f.player.GetTracks();size_t index=tracks[0].path==valid?0:1;
    RODAK_CHECK(f.player.PlayTrack(index));
    Await([&]{return f.audio.GetState().status==AudioPlaybackStatus::kCompleted;});
    f.player.Stop();RODAK_CHECK_FALSE(f.player.Refresh());
    f.player.TogglePlaybackMode();RODAK_CHECK_FALSE(f.player.Refresh());
    RODAK_CHECK_EQ(f.audio.GetState().file_path,valid);std::filesystem::remove(valid);
}

RODAK_TEST("only unsupported file names are a genuine empty library") {
    Fixture f;
    f.files.Tracks({"/song.flac", "/readme.txt"});
    f.Create();
    RODAK_CHECK_EQ(f.player.GetState().library_status, MusicLibraryStatus::kEmpty);
    RODAK_CHECK_FALSE(f.player.PlayNext());
    RODAK_CHECK(f.player.GetState().operation_error.find("WAV or MP3") != std::string::npos);
}

RODAK_TEST("folder read failure can recover through the retry button") {
    Fixture f;
    f.files.Fail("/");
    f.Create();
    f.files.Fail("");
    f.files.Tracks({"/recovered.wav"});
    f.Retry();
    RODAK_CHECK_EQ(f.player.GetState().library_status, MusicLibraryStatus::kReady);
    RODAK_CHECK_EQ(f.player.GetTracks().front().title, "recovered");
    RODAK_CHECK_EQ(lv_obj_get_child_count(f.app.track_list_), 1U);
}

RODAK_TEST("exclusive focus waits for an earlier resume then prevents a later resume") {
    struct Gate {
        std::mutex mutex;
        std::condition_variable cv;
        bool entered = false;
        bool released = false;
        void Enter() {
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            cv.notify_all();
            cv.wait(lock, [&] { return released; });
        }
        bool Wait() {
            std::unique_lock<std::mutex> lock(mutex);
            return cv.wait_for(lock, std::chrono::seconds(2), [&] { return entered; });
        }
        void Release() {
            std::lock_guard<std::mutex> lock(mutex);
            released = true;
            cv.notify_all();
        }
    } write_gate, reopen_gate;
    Fixture f;
    const auto path = Wav("rodakos-music-focus.wav", 128*1024, 128*1024);
    fake_codec::write_hook = [&] { write_gate.Enter(); };
    f.files.Tracks({path});
    f.Create();
    const bool started = f.player.PlayTrack(0);
    const bool wrote = write_gate.Wait();
    f.player.Pause();
    write_gate.Release();
    const bool suspended = f.player.SuspendPlaybackHardware();
    RODAK_CHECK(started && wrote && suspended);
    fake_codec::open_hook = [&] { reopen_gate.Enter(); };
    std::atomic<bool> resumed{false}, focus_done{false}, focus_ok{false};
    std::thread resume([&] { resumed = f.player.TogglePlayPause(); });
    const bool reopening = reopen_gate.Wait();
    std::thread focus([&] {
        focus_ok = f.player.SetPlaybackBlocked(true) && f.player.SuspendPlaybackHardware();
        focus_done = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const bool granted_early = focus_done.load();
    reopen_gate.Release();
    resume.join();
    focus.join();
    RODAK_CHECK(reopening && resumed.load() && focus_ok.load());
    RODAK_CHECK_FALSE(granted_early);
    RODAK_CHECK_FALSE(f.player.TogglePlayPause());
    RODAK_CHECK_FALSE(f.output.IsOpen());
    f.player.Stop();
    f.audio.ReleasePlaybackHardware();
    fake_codec::open_hook = {};
    fake_codec::write_hook = {};
    std::filesystem::remove(path);
}

RODAK_TEST("play during a scan cannot leave a stale busy message after success or failure") {
    Fixture f;
    f.Create();
    f.files.Tracks({"/song.wav"});
    f.files.Hold();
    f.app.RefreshLibrary();
    Await([&] { return f.files.Entered(); });
    f.app.PlayNext();
    RODAK_CHECK(f.Status().find("Scanning") != std::string::npos);
    f.files.Release();
    Await([&] { return f.player.GetState().library_status == MusicLibraryStatus::kReady; });
    f.Refresh();
    RODAK_CHECK_EQ(f.Status(), "Ready");
    f.files.Hold();
    f.files.Fail("/");
    f.app.RefreshLibrary();
    Await([&] { return f.files.Entered(); });
    f.app.PlayNext();
    f.files.Release();
    Await([&] { return f.player.GetState().library_status == MusicLibraryStatus::kReadError; });
    f.Refresh();
    RODAK_CHECK(f.Status().find("Cannot read music folders") != std::string::npos);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.track_title_label_)), "No track");
}
