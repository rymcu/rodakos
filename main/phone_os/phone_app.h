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

enum class PhoneAppLaunchMode {
    kReplaceCurrent,
    kHome,
};

enum class PhoneCapability : uint32_t {
    kNone = 0,
    kStorage = 1 << 0,
    kNetwork = 1 << 1,
    kAudioPlayback = 1 << 2,
    kCamera = 1 << 3,
    kBackgroundTick = 1 << 4,
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

    virtual const char* id() const = 0;
    virtual bool OnCreate(PhoneAppContext& context) = 0;
    virtual void OnShow() = 0;
    virtual void OnHide() = 0;
    virtual void OnDestroy() = 0;
    virtual bool OnThemeChanged(PhoneAppContext&) { return false; }
    virtual void OnTick() {}
};

struct PhoneAppDescriptor {
    std::string id;
    std::string title;
    std::string icon;
    PhoneAppCategory category = PhoneAppCategory::kTools;
    PhoneAppLaunchMode launch_mode = PhoneAppLaunchMode::kReplaceCurrent;
    PhoneCapability capabilities = PhoneCapability::kNone;
    bool show_on_home = true;
    std::vector<std::string> aliases;
    std::function<std::unique_ptr<PhoneApp>()> create;
};
