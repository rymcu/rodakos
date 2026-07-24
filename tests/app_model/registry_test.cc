#include "test_framework.h"

#include "phone_os/phone_app_registry.h"

#include <memory>
#include <string>
#include <utility>

namespace {

class NoopApp final : public PhoneApp {
public:
    bool OnCreate(PhoneAppContext&) override { return true; }
    void OnResume() override {}
    void OnPause() override {}
    void OnDestroy() override {}
};

PhoneAppDescriptor MakeDescriptor(std::string id,
                                  std::string title,
                                  PhoneAppRole role = PhoneAppRole::kRegular,
                                  bool show_on_home = true) {
    PhoneAppDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.title = std::move(title);
    descriptor.role = role;
    descriptor.show_on_home = show_on_home;
    descriptor.create = []() -> std::unique_ptr<PhoneApp> {
        return std::make_unique<NoopApp>();
    };
    return descriptor;
}

PhoneAppDescriptor MakeHome() {
    return MakeDescriptor("home", "Home", PhoneAppRole::kHome, false);
}

bool FinalizeWithRegular(PhoneAppDescriptor descriptor) {
    PhoneAppRegistry registry;
    registry.Register(MakeHome());
    registry.Register(std::move(descriptor));
    return registry.Finalize();
}

}  // namespace

RODAK_TEST("registry finalizes valid apps and resolves normalized identities") {
    PhoneAppRegistry registry;
    registry.Register(MakeHome());

    auto settings = MakeDescriptor("system-settings", "System Settings");
    settings.aliases = {"Wi-Fi", "preferences"};
    registry.Register(std::move(settings));

    RODAK_CHECK(registry.Finalize());
    RODAK_CHECK(registry.finalized());
    RODAK_CHECK(registry.valid());
    RODAK_CHECK_EQ(registry.Finalize(), true);

    const auto* home = registry.FindHome();
    RODAK_CHECK_NE(home, nullptr);
    RODAK_CHECK_EQ(home->id, std::string("home"));
    RODAK_CHECK_EQ(registry.FindById("SYSTEM settings")->id, std::string("system-settings"));
    RODAK_CHECK_EQ(registry.ResolveAlias("wi_fi")->id, std::string("system-settings"));
    RODAK_CHECK_EQ(registry.ResolveAlias("PREFERENCES")->id, std::string("system-settings"));

    const auto home_apps = registry.ListHomeApps();
    RODAK_CHECK_EQ(home_apps.size(), size_t{1});
    RODAK_CHECK_EQ(home_apps.front()->id, std::string("system-settings"));
}

RODAK_TEST("registry rejects malformed descriptors and invalid Home roles") {
    RODAK_CHECK_FALSE(FinalizeWithRegular(MakeDescriptor("", "Empty id")));
    RODAK_CHECK_FALSE(FinalizeWithRegular(MakeDescriptor("---", "Normalized empty id")));
    RODAK_CHECK_FALSE(FinalizeWithRegular(MakeDescriptor("untitled", "")));

    auto missing_factory = MakeDescriptor("missing-factory", "Missing factory");
    missing_factory.create = {};
    RODAK_CHECK_FALSE(FinalizeWithRegular(std::move(missing_factory)));

    PhoneAppRegistry without_home;
    without_home.Register(MakeDescriptor("settings", "Settings"));
    RODAK_CHECK_FALSE(without_home.Finalize());

    PhoneAppRegistry visible_home;
    visible_home.Register(MakeDescriptor("home", "Home", PhoneAppRole::kHome, true));
    RODAK_CHECK_FALSE(visible_home.Finalize());

    PhoneAppRegistry multiple_home;
    multiple_home.Register(MakeHome());
    multiple_home.Register(MakeDescriptor("launcher", "Launcher", PhoneAppRole::kHome, false));
    RODAK_CHECK_FALSE(multiple_home.Finalize());
}

RODAK_TEST("registry rejects exact and normalized cross-app identity conflicts") {
    PhoneAppRegistry duplicate_id;
    duplicate_id.Register(MakeHome());
    duplicate_id.Register(MakeDescriptor("duplicate", "First"));
    duplicate_id.Register(MakeDescriptor("duplicate", "Second"));
    RODAK_CHECK_FALSE(duplicate_id.Finalize());

    PhoneAppRegistry normalized_id;
    normalized_id.Register(MakeHome());
    normalized_id.Register(MakeDescriptor("file-manager", "Files"));
    normalized_id.Register(MakeDescriptor("FILE manager", "Documents"));
    RODAK_CHECK_FALSE(normalized_id.Finalize());

    PhoneAppRegistry alias_title;
    alias_title.Register(MakeHome());
    auto wifi = MakeDescriptor("wifi", "Wireless");
    wifi.aliases = {"Network Settings"};
    alias_title.Register(std::move(wifi));
    alias_title.Register(MakeDescriptor("network", "network-settings"));
    RODAK_CHECK_FALSE(alias_title.Finalize());
}

RODAK_TEST("registry keeps Recorder and Assistant voice identities distinct") {
    PhoneAppRegistry conflicted;
    conflicted.Register(MakeHome());
    auto conflicted_recorder = MakeDescriptor("recorder", "Recorder");
    conflicted_recorder.aliases = {"record", "voice"};
    conflicted.Register(std::move(conflicted_recorder));
    auto conflicted_assistant = MakeDescriptor("assistant", "Assistant");
    conflicted_assistant.aliases = {"voice", "siri"};
    conflicted.Register(std::move(conflicted_assistant));
    RODAK_CHECK_FALSE(conflicted.Finalize());

    PhoneAppRegistry registry;
    registry.Register(MakeHome());
    auto recorder = MakeDescriptor("recorder", "Recorder");
    recorder.aliases = {"record", "recorder", "recording", "voice memo", "memo"};
    registry.Register(std::move(recorder));
    auto assistant = MakeDescriptor("assistant", "Assistant");
    assistant.aliases = {"voice", "assistant", "siri"};
    registry.Register(std::move(assistant));

    RODAK_CHECK(registry.Finalize());
    RODAK_CHECK_EQ(registry.ResolveAlias("voice")->id, std::string("assistant"));
    RODAK_CHECK_EQ(registry.ResolveAlias("voice memo")->id, std::string("recorder"));
}

RODAK_TEST("registry freezes after finalization") {
    PhoneAppRegistry registry;
    registry.Register(MakeHome());
    registry.Register(MakeDescriptor("settings", "Settings"));
    RODAK_CHECK(registry.Finalize());

    const size_t original_size = registry.apps().size();
    const auto* original_home = registry.FindHome();
    registry.Register(MakeDescriptor("late", "Late app"));

    RODAK_CHECK_EQ(registry.apps().size(), original_size);
    RODAK_CHECK_EQ(registry.FindHome(), original_home);
    RODAK_CHECK_FALSE(registry.valid());
    RODAK_CHECK_FALSE(registry.Finalize());
}
