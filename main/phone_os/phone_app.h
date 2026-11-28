#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class PhoneAppContext;

enum class PhoneAppCategory {
    kSystem,
    kMedia,
    kTools,
    kGames,
};

// Home icon color. kDefault follows the category; the others are deliberate
// per-app identity choices that spread crowded categories apart.
enum class PhoneAppIconTint {
    kDefault,
    kNeutral,
    kWarm,
    kCool,
    kAlert,
};

enum class PhoneAppRole {
    kRegular,
    kHome,
};

enum class PhoneCapability : uint32_t {
    kNone = 0,
    kStorage = 1 << 0,
    kNetwork = 1 << 1,
    kAudioPlayback = 1 << 2,
    kCamera = 1 << 3,
    kMotion = 1 << 5,
    kAudioRecording = 1 << 6,
};

inline PhoneCapability operator|(PhoneCapability lhs, PhoneCapability rhs) {
    return static_cast<PhoneCapability>(
        static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

inline bool HasCapability(PhoneCapability value, PhoneCapability flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

class PhoneApp {
public:
    virtual ~PhoneApp() = default;

    virtual bool OnCreate(PhoneAppContext& context) = 0;
    virtual void OnResume() = 0;
    virtual void OnPause() = 0;
    virtual void OnDestroy() = 0;
    virtual bool OnThemeChanged(PhoneAppContext&) { return false; }
    virtual bool OnHomeRequested() { return false; }
    virtual bool OnBackRequested() { return false; }
    // The Shell voice overlay covers the bottom of the screen. An app returns true while that
    // area must stay visible and reachable, for example during editing or a camera preview.
    virtual bool WantsBottomAreaClear() const { return false; }
    // A display share captures its first frame next (LVGL task, lock held). An app that shows
    // private text hides it now instead of on its next poll.
    virtual void OnScreenShareStarting() {}
};

struct PhoneAppDescriptor {
    std::string id;
    std::string title;
    std::string icon;
    PhoneAppCategory category = PhoneAppCategory::kTools;
    PhoneAppIconTint icon_tint = PhoneAppIconTint::kDefault;
    PhoneAppRole role = PhoneAppRole::kRegular;
    // Native app requirements are descriptive metadata, not a security boundary.
    PhoneCapability capabilities = PhoneCapability::kNone;
    bool show_on_home = true;
    std::vector<std::string> aliases;
    std::function<std::unique_ptr<PhoneApp>()> create;
};
