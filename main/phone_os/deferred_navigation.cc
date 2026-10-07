#include "phone_os/deferred_navigation.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <cstdlib>
#include <cstring>

DeferredNavigation::~DeferredNavigation() {
    // Deleting the navigation owner from its own dispatch/completion is invalid.
    if (dispatching_ || callbacks_ != 0) std::abort();
    Close();
    heap_caps_free(requests_);
}

bool DeferredNavigation::Initialize(Dispatch dispatch, void* context) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_ || closed_ || dispatch == nullptr) return false;
    initialized_ = true;
    requests_ = static_cast<Request*>(heap_caps_calloc(
        kCapacity, sizeof(Request), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (requests_ == nullptr) return false;
    dispatch_ = dispatch;
    context_ = context;
    // LVGL's configured malloc assertion still applies at startup. This removes
    // request-time async allocations; it is not arbitrary LVGL OOM recovery.
    timer_ = lv_timer_create(OnTimer, 30, this);
    return timer_ != nullptr;
}

bool DeferredNavigation::Enqueue(std::string_view canonical_id, bool home,
                                 Completion completion, void* context) {
    if ((!home && canonical_id.empty()) || canonical_id.size() > kMaxAppIdBytes ||
        canonical_id.find('\0') != std::string_view::npos) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || timer_ == nullptr || count_ == kCapacity) return false;
    auto& request = requests_[(head_ + count_) % kCapacity];
    request = {};
    if (!canonical_id.empty()) {
        std::memcpy(request.app_id, canonical_id.data(), canonical_id.size());
    }
    request.home = home;
    request.completion = completion;
    request.context = context;
    ++count_;
    return true;
}

void DeferredNavigation::Close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        if (timer_ != nullptr) {
            lv_timer_delete(timer_);
            timer_ = nullptr;
        }
    }
    // Run callbacks outside the mailbox mutex, including reentrant rejection.
    // An already executing request finishes normally; Close cannot undo its UI.
    while (true) {
        Request request{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (count_ == 0) return;
            request = requests_[head_];
            head_ = (head_ + 1) % kCapacity;
            --count_;
        }
        Complete(request, false);
    }
}

void DeferredNavigation::Complete(const Request& request, bool ok) {
    if (request.completion == nullptr) return;
    ++callbacks_;
    try {
        request.completion(request.context, ok);
    } catch (...) {
        ESP_LOGE("PhoneNavigation", "Navigation completion callback threw");
    }
    --callbacks_;
}

void DeferredNavigation::OnTimer(lv_timer_t* timer) {
    static_cast<DeferredNavigation*>(lv_timer_get_user_data(timer))->Pump();
}

void DeferredNavigation::Pump() {
    Request request{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ || dispatching_ || count_ == 0) return;
        request = requests_[head_];
        head_ = (head_ + 1) % kCapacity;
        --count_;
        dispatching_ = true;
    }
    bool ok = false;
    try {
        ok = dispatch_(context_, request.app_id, request.home);
    } catch (...) {
        // Recoverable creation failures are handled by PhoneAppHost. An unknown
        // lifecycle exception may have left timer userdata partially torn down.
        ESP_LOGE("PhoneNavigation", "Unexpected navigation lifecycle exception");
        std::abort();
    }
    Complete(request, ok);
    std::lock_guard<std::mutex> lock(mutex_);
    dispatching_ = false;
}
