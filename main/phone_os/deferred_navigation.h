#pragma once

#include <lvgl.h>

#include <cstddef>
#include <mutex>
#include <string_view>

// Serial producers never enter LVGL. A permanent UI timer consumes one admitted
// request per tick, after the current event/lifecycle callback has returned.
class DeferredNavigation {
public:
    using Dispatch = bool (*)(void*, std::string_view, bool);
    using Completion = void (*)(void*, bool);
    static constexpr size_t kCapacity = 4;
    static constexpr size_t kMaxAppIdBytes = 63;

    DeferredNavigation() = default;
    ~DeferredNavigation();
    DeferredNavigation(const DeferredNavigation&) = delete;
    DeferredNavigation& operator=(const DeferredNavigation&) = delete;

    // Initialize/Close/destruction require the LVGL lock. Initialize is one-shot;
    // it reserves the PSRAM ring and LVGL timer before media tasks are admitted.
    bool Initialize(Dispatch dispatch, void* context);
    void Close();
    // Thread-safe, bounded, with no per-request allocation. False admits nothing.
    // Completion context must outlive execution or Close's cancellation callback.
    bool Enqueue(std::string_view canonical_id, bool home,
                 Completion completion = nullptr, void* context = nullptr);

private:
    struct Request {
        char app_id[kMaxAppIdBytes + 1];
        bool home;
        Completion completion;
        void* context;
    };
    static void OnTimer(lv_timer_t* timer);
    void Pump();
    void Complete(const Request& request, bool ok);

    std::mutex mutex_;
    Request* requests_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    Dispatch dispatch_ = nullptr;
    void* context_ = nullptr;
    size_t head_ = 0;
    size_t count_ = 0;
    size_t callbacks_ = 0;
    bool initialized_ = false;
    bool closed_ = false;
    bool dispatching_ = false;
};
