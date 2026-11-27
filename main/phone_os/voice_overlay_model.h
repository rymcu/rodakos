#pragma once

#include "phone_os/voice_turn_progress.h"

#include <cstdint>
#include <string>

namespace rodakos {

// What may cover the bottom of the screen or must not show assistant text. The Shell gathers it
// from its mode, the current app, the toast and the screen share each refresh.
struct VoiceOverlayContext {
    // Lock Screen or Control Center: full-screen Shell overlays win.
    bool shell_modal = false;
    // A soft keyboard covers the bottom half.
    bool keyboard_visible = false;
    // The current app asks for the bottom area (Camera preview, Home Arrange or an open folder).
    bool app_suppresses = false;
    bool toast_visible = false;
    // A WebRTC display share captures the composed screen, top layer included. Captions and
    // progress are hidden for any share, view-only or with control.
    bool screen_share_active = false;
};

enum class VoiceOverlayMode {
    kHidden,
    kProgress,
    kCaption,
};

struct VoiceOverlayFrame {
    VoiceOverlayMode mode = VoiceOverlayMode::kHidden;
    std::string text;

    bool operator==(const VoiceOverlayFrame& other) const {
        return mode == other.mode && text == other.text;
    }
    bool operator!=(const VoiceOverlayFrame& other) const { return !(*this == other); }
};

// Decides what the Shell voice overlay shows. Pure and clock-driven so host tests can step it.
class VoiceOverlayModel {
public:
    // A short command usually gets output.start within 20..200 ms of turn.accepted. Waiting this
    // long before showing progress keeps it from flashing for such replies.
    static constexpr int64_t kProgressDelayMs = 450;

    VoiceOverlayFrame Update(const VoiceTurnProgressSnapshot& snapshot,
                             const VoiceOverlayContext& context, int64_t now_ms);
    // The user tapped the bar: hide it until its text changes, so a covered icon can be reached.
    void Dismiss();
    void Reset();

    // Device text for a stage without a server label (contract section 6.1).
    static const char* StageText(VoiceTurnStage stage);

private:
    bool awaiting_ = false;
    int64_t awaiting_since_ms_ = 0;
    VoiceOverlayFrame wanted_;
    bool dismissed_ = false;
    std::string dismissed_text_;
};

}  // namespace rodakos
