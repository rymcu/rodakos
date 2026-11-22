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
#include "phone_ui/phone_fonts.h"
#include "phone_ui/phone_ui.h"
#include "rodakos_adapters/file_service.h"
#include "settings.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
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
    // The player shows its state on two lines: the title line and the detail line under it.
    std::string Title() { return lv_label_get_text(app.track_title_label_); }
    std::string Detail() { return lv_label_get_text(app.detail_label_); }
    std::string Status() {
        if (app.page_ == MusicApp::Page::kSongs) return lv_label_get_text(app.list_message_);
        return Title() + "|" + Detail();
    }
    bool Shows(const char* text) { return Status().find(text) != std::string::npos; }
    static std::string Icon(lv_obj_t* button) {
        return lv_label_get_text(lv_obj_get_child(button, 0));
    }
    lv_obj_t* HeaderButton(int index) {
        return lv_obj_get_child(lv_obj_get_child(app.root_, 0), index);
    }
    static void Click(lv_obj_t* button) { lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr); }
    void OpenSongs() {
        RODAK_CHECK_EQ(Icon(app.songs_button_), ICON_MUSIC);
        Click(app.songs_button_);
        RODAK_CHECK_EQ(app.page_, MusicApp::Page::kSongs);
    }
    void OpenVolume() {
        Click(app.volume_button_);
        RODAK_CHECK_EQ(app.page_, MusicApp::Page::kVolume);
    }
    lv_obj_t* RetryButton() {
        return app.page_ == MusicApp::Page::kSongs ? app.list_retry_button_ : app.play_button_;
    }
    void Retry() {
        const auto revision=player.GetState().library_revision;
        Click(RetryButton());
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
    RODAK_CHECK_EQ(f.Title(),"文件服务不可用");
}
RODAK_TEST("missing card retries to genuine empty library through the real UI") {
    Fixture f;f.files.mounted=false;f.files.mount_ok=false;f.Create();
    RODAK_CHECK_EQ(f.player.GetState().library_status,MusicLibraryStatus::kStorageUnavailable);
    RODAK_CHECK_EQ(f.Title(), "读不到 SD 卡");
    RODAK_CHECK_EQ(f.Icon(f.app.play_button_), ICON_REFRESH);
    f.OpenSongs();
    RODAK_CHECK(f.Shows("读不到 SD 卡"));
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.list_retry_button_, LV_OBJ_FLAG_HIDDEN));
    Screenshot("music-missing-card.ppm");
    f.files.mount_ok=true;f.Retry();
    RODAK_CHECK_EQ(f.player.GetState().library_status,MusicLibraryStatus::kEmpty);
    RODAK_CHECK(f.Shows("暂无歌曲"));
    RODAK_CHECK(lv_obj_has_flag(f.app.list_retry_button_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(f.app.OnBackRequested());
    f.Refresh();
    RODAK_CHECK_EQ(f.Title(), "暂无歌曲");
    RODAK_CHECK_EQ(f.Detail(), "放进 SD 卡的 music 文件夹");
    Screenshot("music-empty.ppm");
}
RODAK_TEST("root read failure is not a successful empty scan") {
    Fixture f;f.files.Fail("/");f.Create();
    RODAK_CHECK_EQ(f.player.GetState().library_status,MusicLibraryStatus::kReadError);
    RODAK_CHECK_FALSE(f.player.ScanLibrary());
    RODAK_CHECK_EQ(f.Title(), "无法读取歌曲目录");
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
    f.OpenSongs();
    auto* row = f.app.song_rows_[0].button;
    f.files.Tracks({"/replacement.wav"});RODAK_CHECK(f.player.ScanLibrary());
    lv_obj_send_event(row, LV_EVENT_PRESSED, nullptr);
    f.Click(row);
    RODAK_CHECK(f.audio.GetState().file_path.empty());
    RODAK_CHECK_EQ(f.app.page_, MusicApp::Page::kSongs);
}
RODAK_TEST("blocked next and toggle show the ownership error rather than no tracks") {
    Fixture f;f.files.Tracks({"/track.wav"});f.Create();
    RODAK_CHECK(f.player.SetPlaybackBlocked(true));
    f.Click(f.app.next_button_);RODAK_CHECK_EQ(f.Detail(), "音频被其他应用占用");
    f.Click(f.app.play_button_);RODAK_CHECK_EQ(f.Detail(), "音频被其他应用占用");
    RODAK_CHECK(f.audio.GetState().file_path.empty());
}
RODAK_TEST("worker creation failure can retry and recover") {
    Fixture f;music_test::fail_monitor=true;f.Create();
    RODAK_CHECK_EQ(f.Detail(), "曲库服务无法启动");
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
    Fixture f;f.Create();f.OpenSongs();f.files.Hold();
    f.Click(f.app.refresh_library_button_);Await([&]{return f.files.Entered();});
    f.Refresh();RODAK_CHECK(f.Shows("正在读取歌曲"));
    RODAK_CHECK(lv_obj_has_state(f.app.refresh_library_button_, LV_STATE_DISABLED));
    RODAK_CHECK(f.app.OnBackRequested());
    RODAK_CHECK(lv_obj_has_state(f.app.play_button_, LV_STATE_DISABLED));
    const auto stop_start = std::chrono::steady_clock::now();
    f.player.Stop();
    RODAK_CHECK(std::chrono::steady_clock::now()-stop_start < std::chrono::milliseconds(100));
    f.files.Release();Await([&]{return f.player.GetState().library_status==MusicLibraryStatus::kEmpty;});
    f.Refresh();RODAK_CHECK_EQ(f.Title(), "暂无歌曲");
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
    f.Refresh();RODAK_CHECK_EQ(f.Detail(),"无法打开歌曲");
    RODAK_CHECK_EQ(f.Icon(f.app.play_button_), ICON_REFRESH);
    RODAK_CHECK(f.player.PlayTrack(valid_index));
    Await([&]{return f.audio.GetState().status==AudioPlaybackStatus::kCompleted;});
    f.Refresh();RODAK_CHECK_EQ(f.Detail(), "valid");
    RODAK_CHECK_EQ(f.Icon(f.app.play_button_), ICON_PLAY_FILLED);
    std::filesystem::remove(valid);
}
RODAK_TEST("asynchronous codec write error after format discovery outranks progress in the real UI") {
    Fixture f;const auto broken=Wav("rodakos-music-output-failure.wav",128,128);
    fake_codec::fail_write=true;
    f.files.Tracks({broken});f.Create();RODAK_CHECK(f.player.PlayTrack(0));
    Await([&]{return f.audio.GetState().status==AudioPlaybackStatus::kError;});
    RODAK_CHECK(f.audio.GetState().sample_rate>0);f.Refresh();
    RODAK_CHECK_EQ(f.Detail(),"音频输出失败");
    RODAK_CHECK_EQ(f.Icon(f.app.play_button_), ICON_REFRESH);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t title_area{}, detail_area{};
    lv_obj_get_coords(f.app.track_title_label_, &title_area);
    lv_obj_get_coords(f.app.detail_label_, &detail_area);
    RODAK_CHECK(title_area.y2 < detail_area.y1);
    Screenshot("music-playback-error.ppm");
    fake_codec::fail_write=false;
    f.Click(f.app.play_button_);
    Await([&]{return f.audio.GetState().status==AudioPlaybackStatus::kCompleted;});
    f.Refresh();RODAK_CHECK_EQ(f.Icon(f.app.play_button_), ICON_PLAY_FILLED);
    std::filesystem::remove(broken);
}
RODAK_TEST("rejected codec volume change immediately restores slider and label") {
    Fixture f;f.Create();RODAK_CHECK(f.output.OpenForOwner("test",16000,1,16));
    fake_codec::fail_volume_write=true;f.OpenVolume();
    lv_slider_set_value(f.app.volume_slider_,90,LV_ANIM_OFF);
    lv_obj_send_event(f.app.volume_slider_,LV_EVENT_VALUE_CHANGED,nullptr);
    RODAK_CHECK_EQ(lv_slider_get_value(f.app.volume_slider_),60);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.volume_value_label_)),"60%");
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.volume_chip_label_)),"60");
    RODAK_CHECK_EQ(f.output.volume(),60);f.output.CloseForOwner("test");
}
RODAK_TEST("stop freezes completed-track auto advance") {
    Fixture f;const auto valid=Wav("rodakos-music-stop.wav",64,64);
    f.files.Tracks({valid,"/zz-second.wav"});f.Create();
    auto tracks=f.player.GetTracks();size_t index=tracks[0].path==valid?0:1;
    RODAK_CHECK(f.player.PlayTrack(index));
    Await([&]{return f.audio.GetState().status==AudioPlaybackStatus::kCompleted;});
    f.Refresh();
    RODAK_CHECK_EQ(f.Icon(f.app.stop_button_), ICON_STOP);
    f.Click(f.app.stop_button_);RODAK_CHECK_FALSE(f.player.Refresh());
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
    f.OpenSongs();
    RODAK_CHECK(lv_obj_get_child_count(f.app.track_list_) <= 8U);
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
    f.Click(f.app.next_button_);
    RODAK_CHECK(f.Shows("正在读取歌曲"));
    f.files.Release();
    Await([&] { return f.player.GetState().library_status == MusicLibraryStatus::kReady; });
    f.Refresh();
    RODAK_CHECK_EQ(f.Status(), "请选择歌曲|");
    f.files.Hold();
    f.files.Fail("/");
    f.app.RefreshLibrary();
    Await([&] { return f.files.Entered(); });
    f.Click(f.app.next_button_);
    f.files.Release();
    Await([&] { return f.player.GetState().library_status == MusicLibraryStatus::kReadError; });
    f.Refresh();
    RODAK_CHECK_EQ(f.Title(), "无法读取歌曲目录");
}

RODAK_TEST("the last track remains selectable with a fixed number of rows") {
    Fixture f;
    std::vector<std::string> paths;
    for (int i = 1000; i < 1250; ++i) paths.push_back("/song" + std::to_string(i) + ".wav");
    f.files.Tracks(paths);
    f.Create();
    f.OpenSongs();
    RODAK_CHECK_EQ(f.Icon(f.app.refresh_library_button_), ICON_REFRESH);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.songs_title_)), "歌曲 · 250");
    lv_obj_update_layout(f.app.track_list_);
    const auto slots = lv_obj_get_child_count(f.app.track_list_);
    RODAK_CHECK(slots <= 8U);
    lv_obj_scroll_to_y(f.app.track_list_, 250 * 40, LV_ANIM_OFF);
    lv_obj_update_layout(f.app.track_list_);
    RODAK_CHECK_EQ(lv_obj_get_child_count(f.app.track_list_), slots);
    const auto last = std::find_if(f.app.song_rows_.begin(), f.app.song_rows_.end(),
        [](const auto& row) { return row.index == 249 && row.revision != 0; });
    RODAK_CHECK(last != f.app.song_rows_.end());
    RODAK_CHECK_EQ(std::string(lv_label_get_text(last->title)), "song1249");
    lv_area_t list_area{}, last_area{};
    lv_obj_get_coords(f.app.track_list_, &list_area);
    lv_obj_get_coords(last->button, &last_area);
    RODAK_CHECK(last_area.y1 >= list_area.y1 && last_area.y2 <= list_area.y2);
    lv_test_mouse_click_at(last_area.x1 + 150, last_area.y1 + 20);
    RODAK_CHECK_EQ(f.app.page_, MusicApp::Page::kPlayer);
    RODAK_CHECK_EQ(f.audio.GetState().file_path, paths.back());
}

RODAK_TEST("revision change invalidates an existing press even after the row is rebound") {
    Fixture f;
    f.files.Tracks({"/old.wav"});
    f.Create();
    f.OpenSongs();
    auto* row = f.app.song_rows_[0].button;
    lv_obj_send_event(row, LV_EVENT_PRESSED, nullptr);
    RODAK_CHECK(f.app.song_rows_[0].pressed);
    f.files.Tracks({"/new.wav"});
    RODAK_CHECK(f.player.ScanLibrary());
    f.Refresh();
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.song_rows_[0].title)), "new");
    f.Click(row);
    RODAK_CHECK(f.audio.GetState().file_path.empty());
    RODAK_CHECK_EQ(f.app.page_, MusicApp::Page::kSongs);
}

RODAK_TEST("player loading and playing show wake pause while paused only shows playback pause") {
    std::atomic<bool> loading{false}, allow_open{false};
    Fixture f;
    // 256 writes at 10 ms each keep the track playing for ~2.5 s, well past the checks below.
    const auto path = Wav("rodakos-music-status.wav", 1024 * 1024, 1024 * 1024);
    // The hook runs on the playback task outside every audio mutex, so blocking it here cannot
    // deadlock Refresh(). Blocking a codec write would: it holds the output-service mutex that
    // GetState() takes to read the volume.
    f.audio.SetBeforeOutputOpen([&] {
        loading = true;
        while (!allow_open) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    struct ReleaseGate {
        std::atomic<bool>& open;
        ~ReleaseGate() { open = true; }
    } release{allow_open};
    f.files.Tracks({path});
    f.Create();
    RODAK_CHECK_EQ(f.Icon(f.app.back_button_), ICON_ARROW_LEFT);
    RODAK_CHECK_EQ(f.Icon(f.app.play_button_), ICON_PLAY_FILLED);
    RODAK_CHECK(lv_obj_has_flag(f.app.wake_hint_, LV_OBJ_FLAG_HIDDEN));
    // Each codec write holds the output mutex briefly; a bounded delay slows playback without
    // blocking GetState() indefinitely.
    fake_codec::write_hook = [] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); };
    f.Click(f.app.play_button_);
    Await([&] { return loading.load(); });
    f.Refresh();
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.wake_hint_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK_EQ(f.Icon(f.app.play_button_), ICON_PAUSE_FILLED);
    allow_open = true;
    Await([&] { return f.audio.GetState().status == AudioPlaybackStatus::kPlaying; });
    f.Refresh();
    RODAK_CHECK_FALSE(lv_obj_has_flag(f.app.wake_hint_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK(f.audio.GetState().duration_ms >= 32000);
    Screenshot("music-playing.ppm");
    // The player's monitor briefly holds its operation lock, and a tap that lands then reports
    // busy without toggling. Retrying only after a busy result cannot double-toggle.
    for (int i = 0; i < 50; ++i) {
        f.Click(f.app.play_button_);
        f.Refresh();
        if (f.Detail() != "播放器忙 · 请重试") break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    RODAK_CHECK_EQ(f.audio.GetState().status, AudioPlaybackStatus::kPaused);
    RODAK_CHECK(lv_obj_has_flag(f.app.wake_hint_, LV_OBJ_FLAG_HIDDEN));
    RODAK_CHECK_EQ(f.Icon(f.app.play_button_), ICON_PLAY_FILLED);

    f.OpenSongs();
    RODAK_CHECK(f.app.song_rows_[0].current);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.song_rows_[0].lead)), ICON_PLAY);
    Screenshot("music-songs.ppm");
    f.Click(f.HeaderButton(0));
    RODAK_CHECK_EQ(f.app.page_, MusicApp::Page::kPlayer);
    RODAK_CHECK(f.app.tracks_.empty());
    RODAK_CHECK_EQ(f.audio.GetState().file_path, path);
    RODAK_CHECK_EQ(f.audio.GetState().status, AudioPlaybackStatus::kPaused);
    f.player.Stop();
    Await([&] { return f.audio.GetState().status != AudioPlaybackStatus::kPaused; });
    std::filesystem::remove(path);
}

RODAK_TEST("playback retry matches the failed path after the library changes") {
    Fixture f;
    const auto path = Wav("rodakos-music-retry.wav", 64, 64);
    std::filesystem::remove(path);
    f.files.Tracks({path});
    f.Create();
    RODAK_CHECK(f.player.PlayTrack(0));
    Await([&] { return f.audio.GetState().status == AudioPlaybackStatus::kError; });
    // Titles sort before the temp file's "<pid>-rodakos-..." name, so the failed track moves.
    f.files.Tracks({"/0-added.wav", path});
    RODAK_CHECK(f.player.ScanLibrary());
    RODAK_CHECK_EQ(f.player.GetTracks()[1].path, path);
    RODAK_CHECK_EQ(Wav("rodakos-music-retry.wav", 64, 64), path);
    f.Refresh();
    f.Click(f.app.play_button_);
    Await([&] { return f.audio.GetState().status == AudioPlaybackStatus::kCompleted; });
    RODAK_CHECK_EQ(f.audio.GetState().file_path, path);

    std::filesystem::remove(path);
    RODAK_CHECK(f.player.PlayTrack(1));
    Await([&] { return f.audio.GetState().status == AudioPlaybackStatus::kError; });
    f.files.Tracks({"/replacement.wav"});
    RODAK_CHECK(f.player.ScanLibrary());
    f.Refresh();
    f.Click(f.app.play_button_);
    RODAK_CHECK_EQ(f.audio.GetState().file_path, path);
    RODAK_CHECK_EQ(f.Detail(), "曲库已更新 · 请重新选歌");
}

RODAK_TEST("system back closes the song list and volume sheet before returning home") {
    Fixture f;
    // The host font has no CJK glyphs, so an ASCII name exercises the "title-artist" split.
    f.files.Tracks({"/Stay Here-Lin.wav"});
    f.Create();
    lv_obj_update_layout(f.app.root_);
    RODAK_CHECK_EQ(f.Title(), "请选择歌曲");
    RODAK_CHECK_EQ(lv_obj_get_width(f.app.play_button_), 52);
    RODAK_CHECK(lv_obj_get_height(f.app.previous_button_) >= 44);
    RODAK_CHECK(lv_obj_get_height(f.app.stop_button_) >= 36);
    const auto screen_children = lv_obj_get_child_count(lv_screen_active());
    f.OpenSongs();
    lv_obj_update_layout(f.app.root_);
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.song_rows_[0].title)), "Stay Here");
    RODAK_CHECK_EQ(std::string(lv_label_get_text(f.app.song_rows_[0].artist)), "Lin");
    RODAK_CHECK_EQ(lv_obj_get_child_count(lv_screen_active()), screen_children);
    RODAK_CHECK(f.app.OnBackRequested());
    RODAK_CHECK_EQ(f.app.page_, MusicApp::Page::kPlayer);
    f.OpenVolume();
    RODAK_CHECK(f.app.volume_scrim_ != nullptr);
    lv_obj_update_layout(f.app.root_);
    lv_area_t slider{};
    lv_obj_get_coords(f.app.volume_slider_, &slider);
    const lv_point_t touch{slider.x1 + 5, slider.y1 - 15};
    RODAK_CHECK(lv_obj_hit_test(f.app.volume_slider_, &touch));
    Screenshot("music-volume.ppm");
    RODAK_CHECK_EQ(lv_obj_get_child_count(lv_screen_active()), screen_children);
    RODAK_CHECK(f.app.OnBackRequested());
    RODAK_CHECK_EQ(f.app.page_, MusicApp::Page::kPlayer);
    RODAK_CHECK(f.app.volume_scrim_ == nullptr);
    RODAK_CHECK_FALSE(f.app.OnBackRequested());
    f.OpenVolume();
    lv_test_mouse_click_at(160, 60); lv_test_wait(5);
    RODAK_CHECK_EQ(f.app.page_, MusicApp::Page::kPlayer);
}
