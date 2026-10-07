#pragma once
#include <cstddef>
#include <string_view>

// This suite only tests synchronous forwarding, with a fake PhoneSystem/UI.
// Real deferred admission/lifecycle/LVGL coverage lives in tests/navigation_ui.
class DeferredNavigation {
public:
    using Dispatch = bool (*)(void*, std::string_view, bool);
    using Completion = void (*)(void*, bool);
    static constexpr size_t kMaxAppIdBytes = 63;
    bool Initialize(Dispatch, void*) { return false; }
    void Close() {}
    bool Enqueue(std::string_view, bool, Completion = nullptr, void* = nullptr) { return false; }
};
