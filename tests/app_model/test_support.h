#pragma once

#include "test_framework.h"

#include "phone_os/phone_app_context.h"
#include "phone_os/phone_app_host.h"
#include "phone_os/phone_app_registry.h"
#include "phone_os/phone_navigation.h"
#include "phone_os/phone_services.h"
#include "phone_os/phone_system.h"
#include "phone_ui/phone_ui.h"
#include "settings.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rodakos_test {

struct EventTrace {
    void Add(const std::string& event) { events.push_back(event); }
    void Clear() { events.clear(); }

    std::vector<std::string> events;
};

struct AppProbe {
    AppProbe(std::string app_name, std::shared_ptr<EventTrace> event_trace)
        : name(std::move(app_name)), trace(std::move(event_trace)) {}

    std::string name;
    std::shared_ptr<EventTrace> trace;
    bool create_result = true;
    bool factory_returns_null = false;
    bool default_theme_result = false;
    bool home_request_result = false;
    std::vector<bool> theme_results;
    std::function<void(PhoneAppContext&)> on_create;
    std::function<void()> on_home_requested;
    int factory_calls = 0;
    int create_calls = 0;
    int resume_calls = 0;
    int pause_calls = 0;
    int destroy_calls = 0;
    int destructor_calls = 0;
    int theme_calls = 0;
    int home_request_calls = 0;
    size_t next_theme_result = 0;
};

class TracingApp final : public PhoneApp {
public:
    explicit TracingApp(std::shared_ptr<AppProbe> probe) : probe_(std::move(probe)) {}

    ~TracingApp() override { ++probe_->destructor_calls; }

    bool OnCreate(PhoneAppContext& context) override {
        ++probe_->create_calls;
        probe_->trace->Add(probe_->name + ".create");
        if (probe_->on_create) {
            probe_->on_create(context);
        }
        return probe_->create_result;
    }

    void OnResume() override {
        ++probe_->resume_calls;
        probe_->trace->Add(probe_->name + ".resume");
    }

    void OnPause() override {
        ++probe_->pause_calls;
        probe_->trace->Add(probe_->name + ".pause");
    }

    void OnDestroy() override {
        ++probe_->destroy_calls;
        probe_->trace->Add(probe_->name + ".destroy");
    }

    bool OnThemeChanged(PhoneAppContext&) override {
        ++probe_->theme_calls;
        probe_->trace->Add(probe_->name + ".theme");
        if (probe_->next_theme_result < probe_->theme_results.size()) {
            return probe_->theme_results[probe_->next_theme_result++];
        }
        return probe_->default_theme_result;
    }

    bool OnHomeRequested() override {
        ++probe_->home_request_calls;
        probe_->trace->Add(probe_->name + ".home");
        if (probe_->on_home_requested) {
            probe_->on_home_requested();
        }
        return probe_->home_request_result;
    }

private:
    std::shared_ptr<AppProbe> probe_;
};

inline PhoneAppDescriptor MakeTracingDescriptor(
    const std::string& id,
    const std::shared_ptr<AppProbe>& probe,
    PhoneCapability capabilities = PhoneCapability::kNone) {
    PhoneAppDescriptor descriptor;
    descriptor.id = id;
    descriptor.title = id;
    descriptor.capabilities = capabilities;
    descriptor.create = [probe]() -> std::unique_ptr<PhoneApp> {
        ++probe->factory_calls;
        if (probe->factory_returns_null) {
            return nullptr;
        }
        return std::make_unique<TracingApp>(probe);
    };
    return descriptor;
}

struct HostFixture {
    HostFixture()
        : navigation(system),
          context(ui, navigation, registry, services, settings) {}

    ~HostFixture() { host.CloseCurrent(); }

    PhoneUi ui;
    PhoneSystem system;
    PhoneNavigation navigation;
    PhoneAppRegistry registry;
    PhoneServices services;
    Settings settings;
    PhoneAppContext context;
    PhoneAppHost host;
};

inline void CheckEvents(const EventTrace& trace, std::initializer_list<const char*> expected) {
    RODAK_CHECK_EQ(trace.events.size(), expected.size());
    size_t index = 0;
    for (const char* event : expected) {
        RODAK_CHECK_EQ(trace.events[index], std::string(event));
        ++index;
    }
}

}  // namespace rodakos_test
