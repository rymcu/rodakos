#include "phone_os/voice_overlay_model.h"

namespace rodakos {

const char* VoiceOverlayModel::StageText(VoiceTurnStage stage) {
    switch (stage) {
        case VoiceTurnStage::kThinking:
            return "正在思考";
        case VoiceTurnStage::kTool:
            return "正在处理";
        case VoiceTurnStage::kComposing:
            return "正在组织回答";
        case VoiceTurnStage::kNone:
        default:
            return "已收到";
    }
}

VoiceOverlayFrame VoiceOverlayModel::Update(const VoiceTurnProgressSnapshot& snapshot,
                                            const VoiceOverlayContext& context, int64_t now_ms) {
    if (snapshot.awaiting_reply && !awaiting_) {
        awaiting_since_ms_ = now_ms;
    }
    awaiting_ = snapshot.awaiting_reply;

    wanted_ = {};
    if (!snapshot.caption.empty()) {
        wanted_.mode = VoiceOverlayMode::kCaption;
        wanted_.text = snapshot.caption;
    } else if (awaiting_ && now_ms - awaiting_since_ms_ >= kProgressDelayMs) {
        wanted_.mode = VoiceOverlayMode::kProgress;
        wanted_.text = snapshot.label.empty() ? StageText(snapshot.stage) : snapshot.label;
    }

    if (dismissed_ && (wanted_.mode == VoiceOverlayMode::kHidden ||
                       wanted_.text != dismissed_text_)) {
        dismissed_ = false;
        dismissed_text_.clear();
    }

    const bool blocked = context.shell_modal || context.keyboard_visible ||
                         context.app_suppresses || context.toast_visible ||
                         context.screen_share_active;
    if (blocked || dismissed_) {
        return {};
    }
    return wanted_;
}

void VoiceOverlayModel::Dismiss() {
    if (wanted_.mode == VoiceOverlayMode::kHidden) {
        return;
    }
    dismissed_ = true;
    dismissed_text_ = wanted_.text;
}

void VoiceOverlayModel::Reset() {
    awaiting_ = false;
    awaiting_since_ms_ = 0;
    wanted_ = {};
    dismissed_ = false;
    dismissed_text_.clear();
}

}  // namespace rodakos
