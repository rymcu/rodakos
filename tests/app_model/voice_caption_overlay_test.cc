#include "test_framework.h"

#include "phone_os/voice_caption_timeline.h"
#include "phone_os/voice_overlay_model.h"

#include <string>

namespace {
using rodakos::VoiceCaptionTimeline;
using rodakos::VoiceOverlayContext;
using rodakos::VoiceOverlayMode;
using rodakos::VoiceOverlayModel;
using rodakos::VoiceTurnProgressSnapshot;
using rodakos::VoiceTurnStage;
}  // namespace

RODAK_TEST("caption timeline follows written frames, not cue arrival") {
    VoiceCaptionTimeline timeline;
    timeline.Begin(1);
    // Both sentences arrive before any of their audio is heard.
    RODAK_CHECK(timeline.AddCue(1, 1, 32, "明天北京晴。"));
    RODAK_CHECK(timeline.AddCue(1, 33, 68, "气温 6 到 18 度。"));
    RODAK_CHECK(timeline.current_text().empty());

    RODAK_CHECK(timeline.Advance(1, 1));
    RODAK_CHECK_EQ(timeline.current_text(), std::string("明天北京晴。"));
    // Frames inside the first sentence and up to the next cue keep the first text.
    RODAK_CHECK_FALSE(timeline.Advance(1, 20));
    RODAK_CHECK_FALSE(timeline.Advance(1, 32));
    RODAK_CHECK_EQ(timeline.current_text(), std::string("明天北京晴。"));
    RODAK_CHECK(timeline.Advance(1, 33));
    RODAK_CHECK_EQ(timeline.current_text(), std::string("气温 6 到 18 度。"));
    RODAK_CHECK_EQ(timeline.pending_cues(), size_t{0});
}

RODAK_TEST("caption timeline skips a cue whose first frames were lost and drops late cues") {
    VoiceCaptionTimeline timeline;
    timeline.Begin(4);
    RODAK_CHECK(timeline.AddCue(4, 1, 10, "第一句"));
    RODAK_CHECK(timeline.AddCue(4, 11, 20, "第二句"));
    // Frame 1..4 lost: the first written frame inside the range still shows the sentence.
    RODAK_CHECK(timeline.Advance(4, 5));
    RODAK_CHECK_EQ(timeline.current_text(), std::string("第一句"));
    // The whole second sentence was lost; frame 25 belongs to no cue.
    RODAK_CHECK_FALSE(timeline.Advance(4, 25));
    RODAK_CHECK_EQ(timeline.pending_cues(), size_t{0});
    // A cue whose frames already played is useless and must not show later.
    RODAK_CHECK_FALSE(timeline.AddCue(4, 21, 24, "迟到"));
    // Overlapping and other-epoch cues are refused.
    RODAK_CHECK(timeline.AddCue(4, 30, 40, "第四句"));
    RODAK_CHECK_FALSE(timeline.AddCue(4, 35, 45, "重叠"));
    RODAK_CHECK_FALSE(timeline.AddCue(5, 50, 60, "别的回复"));
    RODAK_CHECK_FALSE(timeline.Advance(5, 30));
}

RODAK_TEST("caption timeline shows the output.start fallback until a cue arrives") {
    VoiceCaptionTimeline timeline;
    timeline.Begin(2, "好的。");
    RODAK_CHECK(timeline.current_text().empty());
    RODAK_CHECK(timeline.Advance(2, 1));
    RODAK_CHECK_EQ(timeline.current_text(), std::string("好的。"));
    timeline.Begin(3, "整段回复");
    // Once any cue arrives the reply is captioned; the whole-reply text never shows.
    RODAK_CHECK(timeline.AddCue(3, 5, 9, "分句"));
    RODAK_CHECK_FALSE(timeline.Advance(3, 1));
    RODAK_CHECK(timeline.current_text().empty());
    RODAK_CHECK(timeline.Advance(3, 5));
    RODAK_CHECK_EQ(timeline.current_text(), std::string("分句"));
}

RODAK_TEST("caption timeline clears at once on reset and bounds its queue") {
    VoiceCaptionTimeline timeline;
    timeline.Begin(1);
    RODAK_CHECK(timeline.AddCue(1, 1, 2, "a"));
    RODAK_CHECK(timeline.Advance(1, 1));
    timeline.Reset();
    RODAK_CHECK(timeline.current_text().empty());
    RODAK_CHECK_FALSE(timeline.active());
    RODAK_CHECK_FALSE(timeline.AddCue(1, 3, 4, "b"));

    timeline.Begin(9);
    for (uint32_t index = 0; index < VoiceCaptionTimeline::kMaxPendingCues + 3; ++index) {
        timeline.AddCue(9, index * 10 + 1, index * 10 + 5, "x");
    }
    RODAK_CHECK_EQ(timeline.pending_cues(), VoiceCaptionTimeline::kMaxPendingCues);
}

RODAK_TEST("voice overlay waits 450 ms before progress and shows captions at once") {
    VoiceOverlayModel model;
    VoiceTurnProgressSnapshot snapshot;
    VoiceOverlayContext context;
    snapshot.awaiting_reply = true;
    RODAK_CHECK(model.Update(snapshot, context, 1000).mode == VoiceOverlayMode::kHidden);
    RODAK_CHECK(model.Update(snapshot, context, 1449).mode == VoiceOverlayMode::kHidden);
    auto frame = model.Update(snapshot, context, 1450);
    RODAK_CHECK(frame.mode == VoiceOverlayMode::kProgress);
    RODAK_CHECK_EQ(frame.text, std::string("已收到"));
    snapshot.stage = VoiceTurnStage::kTool;
    RODAK_CHECK_EQ(model.Update(snapshot, context, 1500).text, std::string("正在处理"));
    snapshot.label = "正在查询天气";
    RODAK_CHECK_EQ(model.Update(snapshot, context, 1600).text, std::string("正在查询天气"));

    // A fast reply never shows progress.
    model.Reset();
    snapshot = {};
    snapshot.awaiting_reply = true;
    RODAK_CHECK(model.Update(snapshot, context, 0).mode == VoiceOverlayMode::kHidden);
    snapshot.awaiting_reply = false;
    snapshot.caption = "好的";
    frame = model.Update(snapshot, context, 120);
    RODAK_CHECK(frame.mode == VoiceOverlayMode::kCaption);
    RODAK_CHECK_EQ(frame.text, std::string("好的"));
    snapshot.caption.clear();
    RODAK_CHECK(model.Update(snapshot, context, 200).mode == VoiceOverlayMode::kHidden);
}

RODAK_TEST("voice overlay yields to Shell modals, keyboard, apps, toast and screen share") {
    VoiceOverlayModel model;
    VoiceTurnProgressSnapshot snapshot;
    snapshot.caption = "字幕";
    VoiceOverlayContext context;
    RODAK_CHECK(model.Update(snapshot, context, 0).mode == VoiceOverlayMode::kCaption);
    const auto blocked = [&](void (*set)(VoiceOverlayContext&)) {
        VoiceOverlayContext copy;
        set(copy);
        return model.Update(snapshot, copy, 0).mode == VoiceOverlayMode::kHidden;
    };
    RODAK_CHECK(blocked([](VoiceOverlayContext& c) { c.shell_modal = true; }));
    RODAK_CHECK(blocked([](VoiceOverlayContext& c) { c.keyboard_visible = true; }));
    RODAK_CHECK(blocked([](VoiceOverlayContext& c) { c.app_suppresses = true; }));
    RODAK_CHECK(blocked([](VoiceOverlayContext& c) { c.toast_visible = true; }));
    RODAK_CHECK(blocked([](VoiceOverlayContext& c) { c.screen_share_active = true; }));
    // After the blocker goes, the current sentence returns.
    RODAK_CHECK_EQ(model.Update(snapshot, context, 0).text, std::string("字幕"));
}

RODAK_TEST("voice overlay tap hides the bar until its text changes") {
    VoiceOverlayModel model;
    VoiceTurnProgressSnapshot snapshot;
    VoiceOverlayContext context;
    snapshot.caption = "第一句";
    RODAK_CHECK(model.Update(snapshot, context, 0).mode == VoiceOverlayMode::kCaption);
    model.Dismiss();
    RODAK_CHECK(model.Update(snapshot, context, 10).mode == VoiceOverlayMode::kHidden);
    snapshot.caption = "第二句";
    RODAK_CHECK_EQ(model.Update(snapshot, context, 20).text, std::string("第二句"));
}
