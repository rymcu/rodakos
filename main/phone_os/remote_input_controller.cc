#include "phone_os/remote_input_controller.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

#include <cJSON.h>
#include <esp_log.h>

namespace rodakos {
namespace {
constexpr const char* TAG = "RemoteInput";
void LogControlInput(const cJSON* json, const std::string& kind) {
    const auto* action = cJSON_GetObjectItemCaseSensitive(json, "action");
    const char* action_name = "-";
    if (cJSON_IsString(action)) {
        if (std::strcmp(action->valuestring, "move") == 0) return;
        for (const auto* known : {"down", "up", "enable", "disable"}) {
            if (std::strcmp(action->valuestring, known) == 0) action_name = known;
        }
    }
    if (kind != "pointer" && kind != "control" && kind != "text" && kind != "shortcut") return;
    const auto* sequence = cJSON_GetObjectItemCaseSensitive(json, "seq");
    const uint32_t seq = cJSON_IsNumber(sequence) && sequence->valuedouble >= 1 &&
        sequence->valuedouble <= UINT32_MAX ? static_cast<uint32_t>(sequence->valuedouble) : 0;
    ESP_LOGI(TAG, "control input: seq=%u kind=%s action=%s",
             static_cast<unsigned>(seq), kind.c_str(), action_name);
}

bool IsValidUtf8(const char* text) {
    if (text == nullptr) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(text);
    while (*p != 0) {
        uint32_t codepoint = 0;
        size_t length = 0;
        if (*p < 0x80) { codepoint = *p; length = 1; }
        else if (*p >= 0xc2 && *p <= 0xdf) { codepoint = *p & 0x1f; length = 2; }
        else if (*p >= 0xe0 && *p <= 0xef) { codepoint = *p & 0x0f; length = 3; }
        else if (*p >= 0xf0 && *p <= 0xf4) { codepoint = *p & 0x07; length = 4; }
        else return false;
        for (size_t i = 1; i < length; ++i) {
            if ((p[i] & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (p[i] & 0x3f);
        }
        if ((length == 3 && codepoint < 0x800) || (length == 4 && codepoint < 0x10000) ||
            codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
        p += length;
    }
    return true;
}

struct ControlGrant {
    explicit ControlGrant(StreamLeasePtr stream) : lease(std::move(stream)) {}
    const StreamLeasePtr lease;
    std::atomic<bool> enabled{true};
    bool IsActive() const { return enabled.load() && lease != nullptr && lease->IsActive(); }
    template <typename F>
    bool TryApply(F&& action) const {
        if (!enabled.load() || lease == nullptr) return false;
        return lease->TryApply(std::forward<F>(action));
    }
};

using Grant = std::shared_ptr<ControlGrant>;
struct Action {
    Grant grant;
    std::string kind;
    std::string value;
    RemoteInputController::Reply reply;
    bool executing = false;
};
struct Pointer {
    Grant grant;
    RemotePointerSample sample;
    bool move = false;
    RemoteInputController::Reply reply;
};
void Reject(const RemoteInputController::Reply& reply, const char* reason) {
    if (reply) reply(false, reason);
}
// Match the bounded input queues (16 actions, 32 pointers, one navigation).
// Cancellation runs under memory pressure and must not allocate to deliver replies.
struct CancelledReplies {
    std::array<RemoteInputController::Reply, 49> items;
    size_t size = 0;
    void Add(RemoteInputController::Reply& reply) {
        if (reply) items[size++] = std::move(reply);
    }
    void ReplyAll(const char* reason) const {
        for (size_t i = 0; i < size; ++i) Reject(items[i], reason);
    }
};
}  // namespace

struct RemoteInputController::State {
    explicit State(Executor value) : executor(std::move(value)) {}
    std::mutex mutex;
    Executor executor;
    bool closed = false;
    Grant grant;
    std::deque<Action> actions;
    std::deque<Pointer> pointers;
    std::shared_ptr<Action> navigation;
    RemotePointerSample pointer;
    uint64_t pointer_cancel_generation = 0;
    uint64_t delivered_cancel_generation = 0;

    void CancelHeldPointerLocked() {
        // An up may already have been dequeued but not yet admitted. Every
        // cancellation advances the epoch; the LVGL bridge filters local input.
        ++pointer_cancel_generation;
        pointer.pressed = false;
    }

    void CancelPointersLocked(CancelledReplies& replies) {
        for (auto& item : pointers) replies.Add(item.reply);
        pointers.clear();
    }
    void CancelPendingLocked(CancelledReplies& replies) {
        for (auto& item : actions) replies.Add(item.reply);
        actions.clear();
        CancelPointersLocked(replies);
    }
    void ClearLocked(CancelledReplies* cancelled = nullptr) {
        CancelHeldPointerLocked();
        if (grant) grant->enabled.store(false);
        grant.reset();
        if (cancelled) {
            CancelPendingLocked(*cancelled);
            if (navigation && !navigation->executing) cancelled->Add(navigation->reply);
        }
        actions.clear();
        pointers.clear();
        navigation.reset();
        pointer = {};
    }
    void PruneLocked() {
        if (grant && !grant->IsActive()) ClearLocked();
    }
};

RemoteInputController::RemoteInputController(Executor executor)
    : state_(std::make_shared<State>(std::move(executor))) {}

RemoteInputController::~RemoteInputController() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->closed = true;
    state_->ClearLocked();
}

void RemoteInputController::Handle(const StreamLeasePtr& lease, const std::string& payload,
                                   Reply reply) {
    const auto state = state_;
    if (payload.empty()) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            // Revoked cleanup still owns its old grant, never the replacement's.
            if (state->grant && state->grant->lease == lease) state->ClearLocked();
        }
        if (state->executor.wake) state->executor.wake();
        if (reply) reply(true, nullptr);
        return;
    }
    if (!lease || !lease->IsActive()) { Reject(reply, "stale_control_lease"); return; }
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> json(
        cJSON_ParseWithLength(payload.data(), payload.size()), cJSON_Delete);
    if (!cJSON_IsObject(json.get())) { Reject(reply, "invalid_json"); return; }
    const auto* version = cJSON_GetObjectItemCaseSensitive(json.get(), "version");
    const auto* kind_json = cJSON_GetObjectItemCaseSensitive(json.get(), "kind");
    if (!cJSON_IsNumber(version) || version->valuedouble != 1 || !cJSON_IsString(kind_json)) {
        Reject(reply, "control_disabled_or_invalid"); return;
    }
    const std::string kind = kind_json->valuestring;
    LogControlInput(json.get(), kind);
    bool accepted = false;
    const char* reason = "control_disabled_or_invalid";
    bool immediate_reply = true;
    std::vector<Reply> coalesced;
    CancelledReplies cancelled;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->PruneLocked();
        if (!lease->IsActive()) { reason = "stale_control_lease"; }
        else if (kind == "control") {
            const auto* action = cJSON_GetObjectItemCaseSensitive(json.get(), "action");
            if (cJSON_IsString(action) && std::strcmp(action->valuestring, "enable") == 0) {
                if (!state->grant || state->grant->lease != lease) {
                    if (state->grant) state->ClearLocked();
                    state->grant = std::make_shared<ControlGrant>(lease);
                }
                state->pointer.enabled = true;
                accepted = true;
            } else if (cJSON_IsString(action) && std::strcmp(action->valuestring, "disable") == 0) {
                if (!state->grant || state->grant->lease == lease) {
                    state->ClearLocked(&cancelled);
                    accepted = true;
                }
            }
        } else if (state->grant && state->grant->lease == lease) {
            if (kind == "pointer") {
                const auto* action = cJSON_GetObjectItemCaseSensitive(json.get(), "action");
                const auto* x = cJSON_GetObjectItemCaseSensitive(json.get(), "x");
                const auto* y = cJSON_GetObjectItemCaseSensitive(json.get(), "y");
                const bool valid = cJSON_IsString(action) && cJSON_IsNumber(x) && cJSON_IsNumber(y) &&
                    x->valuedouble >= 0 && x->valuedouble < 320 &&
                    y->valuedouble >= 0 && y->valuedouble < 240 &&
                    x->valuedouble == x->valueint && y->valuedouble == y->valueint;
                const bool move = valid && std::strcmp(action->valuestring, "move") == 0;
                const bool down = valid && std::strcmp(action->valuestring, "down") == 0;
                const bool up = valid && std::strcmp(action->valuestring, "up") == 0;
                if (move || down || up) {
                    Pointer event{state->grant, {true, !up, x->valueint, y->valueint}, move, reply};
                    if (move && !state->pointers.empty() && state->pointers.back().move) {
                        coalesced.push_back(std::move(state->pointers.back().reply));
                        state->pointers.back() = std::move(event);
                        accepted = true;
                        immediate_reply = false;
                    } else {
                        if (!move) {
                            while (state->pointers.size() >= 32) {
                                auto old = std::find_if(state->pointers.begin(), state->pointers.end(),
                                    [](const Pointer& item) { return item.move; });
                                if (old == state->pointers.end()) break;
                                coalesced.push_back(std::move(old->reply));
                                state->pointers.erase(old);
                            }
                        }
                        if (state->pointers.size() < 32) {
                            state->pointers.push_back(std::move(event));
                            accepted = true;
                            immediate_reply = false;
                        } else if (move) {
                            accepted = true;
                            reason = "coalesced";
                        }
                    }
                }
            } else if (kind == "text" || kind == "shortcut") {
                const auto* value = cJSON_GetObjectItemCaseSensitive(json.get(),
                    kind == "text" ? "text" : "shortcut");
                bool valid = cJSON_IsString(value);
                if (kind == "text") {
                    valid = valid && std::strlen(value->valuestring) <= 1024;
                    reason = valid ? "invalid_utf8" : "invalid_text";
                    valid = valid && IsValidUtf8(value->valuestring);
                } else if (valid) {
                    const std::string shortcut = value->valuestring;
                    valid = shortcut == "back" || shortcut == "home" || shortcut == "enter" ||
                            shortcut == "escape" || shortcut == "backspace" || shortcut == "delete";
                }
                if (valid && state->actions.size() < 16) {
                    state->actions.push_back({state->grant, kind, value->valuestring, reply});
                    accepted = true;
                    immediate_reply = false;
                } else if (valid) reason = "input_queue_full";
            }
        }
    }
    cancelled.ReplyAll("control_disabled");
    for (const auto& old : coalesced) if (old) old(true, "coalesced");
    if (immediate_reply && reply) {
        reply(accepted, accepted && std::strcmp(reason, "coalesced") != 0 ? nullptr : reason);
    }
    if (state->executor.wake) state->executor.wake();
}

void RemoteInputController::ProcessActions() {
    const auto state = state_;
    while (true) {
        Action action;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->PruneLocked();
            if (state->navigation || state->actions.empty() || !state->pointers.empty()) return;
            action = std::move(state->actions.front());
            state->actions.pop_front();
        }
        if (action.kind == "shortcut" && (action.value == "home" || action.value == "back")) {
            auto request = std::make_shared<Action>(std::move(action));
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->grant != request->grant || !request->grant->IsActive()) continue;
                state->navigation = request;
            }
            const std::weak_ptr<State> owner = state;
            const auto apply = [owner, request]() {
                const auto state = owner.lock();
                if (!state) return;
                bool owns_navigation = false;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->closed) return;
                    owns_navigation = state->navigation == request && state->grant == request->grant;
                    request->executing = owns_navigation;
                }
                bool accepted = false;
                if (owns_navigation) request->grant->TryApply([&]() {
                    accepted = state->executor.navigate && state->executor.navigate(request->value);
                });
                Reply reply;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->closed) return;
                    if (state->navigation == request) state->navigation.reset();
                    reply = std::move(request->reply);
                }
                if (reply) reply(accepted, accepted ? nullptr : "navigation_rejected");
                if (state->executor.wake) state->executor.wake();
            };
            if (!state->executor.defer_navigation || !state->executor.defer_navigation(apply)) {
                Reply reply;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->navigation == request) state->navigation.reset();
                    reply = std::move(request->reply);
                }
                Reject(reply, "navigation_queue_full");
            }
            return;
        }
        RemoteInputResult result{false, "stale_control_lease"};
        action.grant->TryApply([&]() {
            if (state->executor.text) result = state->executor.text(action.kind, action.value);
        });
        if (action.reply) action.reply(result.accepted, result.reason);
    }
}

void RemoteInputController::ReadPointer(
    const std::function<void(const RemotePointerSample&)>& consume) {
    const auto state = state_;
    Pointer event;
    bool has_event = false;
    Grant grant;
    RemotePointerSample sample;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->PruneLocked();
        grant = state->grant;
        const bool cancelled = state->delivered_cancel_generation != state->pointer_cancel_generation;
        if (cancelled) {
            // LVGL reset does not clear prev_state. Deliver one release before
            // dequeuing a replacement down so it starts a distinct gesture.
            state->delivered_cancel_generation = state->pointer_cancel_generation;
        } else if (grant && !state->navigation && !state->pointers.empty()) {
            event = std::move(state->pointers.front());
            state->pointers.pop_front();
            has_event = true;
            if (event.grant == grant) state->pointer = event.sample;
        }
        sample = state->pointer;
        if (cancelled) sample.pressed = false;
        sample.cancel_generation = state->pointer_cancel_generation;
    }
    bool admitted = grant && grant->TryApply([&]() { consume(sample); });
    if (!admitted) {
        // Revocation may race the sample above. Publish its cancellation before
        // any fallback release, otherwise LVGL can interpret cleanup as a click.
        RemotePointerSample released;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->PruneLocked();
            released.cancel_generation = state->pointer_cancel_generation;
            state->delivered_cancel_generation = state->pointer_cancel_generation;
        }
        consume(released);
    }
    if (has_event && event.reply) event.reply(admitted, admitted ? nullptr : "stale_control_lease");
}

void RemoteInputController::ResetForPageTransition() {
    CancelledReplies cancelled;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->PruneLocked();
        state_->CancelHeldPointerLocked();
        if (!state_->navigation) state_->CancelPendingLocked(cancelled);
    }
    // Replies may synchronously revoke or replace this grant. Never call them
    // while holding the controller mutex or retain queued input after reset.
    cancelled.ReplyAll("page_transition");
}

void RemoteInputController::OnLocalTouch() {
    CancelledReplies cancelled;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->CancelHeldPointerLocked();
        state_->CancelPointersLocked(cancelled);
    }
    cancelled.ReplyAll("local_touch_active");
}

bool RemoteInputController::IsEnabled() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->grant && state_->grant->IsActive();
}

}  // namespace rodakos
