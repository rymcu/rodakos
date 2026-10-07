#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "phone_os/stream_lease.h"

namespace rodakos {

struct RemoteInputResult {
    bool accepted = false;
    const char* reason = "text_target_unavailable";
};

struct RemotePointerSample {
    bool enabled = false;
    bool pressed = false;
    int x = 0;
    int y = 0;
    // A cancelled held gesture must reset LVGL instead of synthesizing an up.
    uint64_t cancel_generation = 0;
};

class RemoteInputController {
public:
    using Reply = std::function<void(bool, const char*)>;
    struct Executor {
        std::function<RemoteInputResult(const std::string&, const std::string&)> text;
        std::function<bool(const std::string&)> navigate;
        std::function<bool(std::function<void()>)> defer_navigation;
        std::function<void()> wake;
    };

    explicit RemoteInputController(Executor executor);
    ~RemoteInputController();
    RemoteInputController(const RemoteInputController&) = delete;
    RemoteInputController& operator=(const RemoteInputController&) = delete;
    RemoteInputController(RemoteInputController&&) = delete;
    RemoteInputController& operator=(RemoteInputController&&) = delete;
    void Handle(const StreamLeasePtr& lease, const std::string& payload, Reply reply);
    // Called by LVGL. Deferred navigation obtains its own final admission.
    void ProcessActions();
    void ReadPointer(const std::function<void(const RemotePointerSample&)>& consume);
    void ResetForPageTransition();
    void OnLocalTouch();
    bool IsEnabled() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

}  // namespace rodakos
