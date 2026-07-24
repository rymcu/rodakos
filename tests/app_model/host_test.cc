#include "test_framework.h"
#include "test_support.h"

#include <memory>

using rodakos_test::AppProbe;
using rodakos_test::CheckEvents;
using rodakos_test::EventTrace;
using rodakos_test::HostFixture;
using rodakos_test::MakeTracingDescriptor;

RODAK_TEST("host launches first app and publishes state") {
    HostFixture fixture;
    auto trace = std::make_shared<EventTrace>();
    auto probe = std::make_shared<AppProbe>("alpha", trace);
    const auto descriptor = MakeTracingDescriptor(
        "alpha", probe, PhoneCapability::kStorage | PhoneCapability::kNetwork);

    RODAK_CHECK(fixture.host.Launch(descriptor, fixture.context));
    CheckEvents(*trace, {"alpha.create", "alpha.resume"});
    RODAK_CHECK_EQ(probe->factory_calls, 1);
    RODAK_CHECK_EQ(probe->create_calls, 1);
    RODAK_CHECK_EQ(probe->resume_calls, 1);
    RODAK_CHECK_EQ(fixture.ui.input_reset_count(), 2);

    const auto state = fixture.host.GetState();
    RODAK_CHECK(state.has_current);
    RODAK_CHECK_FALSE(state.transition_in_progress);
    RODAK_CHECK_EQ(state.current_app_id, std::string("alpha"));
    RODAK_CHECK_EQ(
        state.current_capabilities,
        PhoneCapability::kStorage | PhoneCapability::kNetwork);
}

RODAK_TEST("host switches apps in transactional lifecycle order") {
    HostFixture fixture;
    auto trace = std::make_shared<EventTrace>();
    auto first = std::make_shared<AppProbe>("first", trace);
    auto second = std::make_shared<AppProbe>("second", trace);
    const auto first_descriptor = MakeTracingDescriptor("first", first);
    const auto second_descriptor = MakeTracingDescriptor("second", second);

    RODAK_CHECK(fixture.host.Launch(first_descriptor, fixture.context));
    trace->Clear();
    RODAK_CHECK(fixture.host.Launch(second_descriptor, fixture.context));

    CheckEvents(
        *trace,
        {"second.create", "first.pause", "first.destroy", "second.resume"});
    RODAK_CHECK_EQ(first->pause_calls, 1);
    RODAK_CHECK_EQ(first->destroy_calls, 1);
    RODAK_CHECK_EQ(second->resume_calls, 1);
    RODAK_CHECK_EQ(fixture.host.current_app_id(), std::string("second"));
}

RODAK_TEST("host preserves current app when candidate creation fails") {
    HostFixture fixture;
    auto trace = std::make_shared<EventTrace>();
    auto current = std::make_shared<AppProbe>("current", trace);
    auto failed = std::make_shared<AppProbe>("failed", trace);
    failed->create_result = false;
    const auto current_descriptor = MakeTracingDescriptor("current", current);
    const auto failed_descriptor = MakeTracingDescriptor("failed", failed);

    RODAK_CHECK(fixture.host.Launch(current_descriptor, fixture.context));
    trace->Clear();
    const int reset_count = fixture.ui.input_reset_count();
    RODAK_CHECK_FALSE(fixture.host.Launch(failed_descriptor, fixture.context));

    CheckEvents(*trace, {"failed.create", "failed.destroy"});
    RODAK_CHECK_EQ(current->pause_calls, 0);
    RODAK_CHECK_EQ(current->destroy_calls, 0);
    RODAK_CHECK_EQ(failed->resume_calls, 0);
    RODAK_CHECK_EQ(failed->destroy_calls, 1);
    RODAK_CHECK_EQ(fixture.ui.input_reset_count(), reset_count);
    RODAK_CHECK_EQ(fixture.host.current_app_id(), std::string("current"));
}

RODAK_TEST("host rejects missing and null factories without disturbing current app") {
    HostFixture fixture;
    auto trace = std::make_shared<EventTrace>();
    auto current = std::make_shared<AppProbe>("current", trace);
    const auto current_descriptor = MakeTracingDescriptor("current", current);
    RODAK_CHECK(fixture.host.Launch(current_descriptor, fixture.context));
    trace->Clear();

    PhoneAppDescriptor missing_factory;
    missing_factory.id = "missing";
    missing_factory.title = "Missing";
    RODAK_CHECK_FALSE(fixture.host.Launch(missing_factory, fixture.context));

    auto null_probe = std::make_shared<AppProbe>("null", trace);
    null_probe->factory_returns_null = true;
    const auto null_factory = MakeTracingDescriptor("null", null_probe);
    RODAK_CHECK_FALSE(fixture.host.Launch(null_factory, fixture.context));

    RODAK_CHECK_EQ(trace->events.size(), size_t{0});
    RODAK_CHECK_EQ(current->pause_calls, 0);
    RODAK_CHECK_EQ(current->destroy_calls, 0);
    RODAK_CHECK_EQ(null_probe->factory_calls, 1);
    RODAK_CHECK_EQ(fixture.host.current_app_id(), std::string("current"));
}

RODAK_TEST("host rejects reentrant launch and clears transition guard") {
    HostFixture fixture;
    auto trace = std::make_shared<EventTrace>();
    auto outer = std::make_shared<AppProbe>("outer", trace);
    auto nested = std::make_shared<AppProbe>("nested", trace);
    const auto nested_descriptor = MakeTracingDescriptor("nested", nested);
    bool nested_result = true;
    bool transition_seen = false;
    outer->on_create = [&](PhoneAppContext&) {
        transition_seen = fixture.host.GetState().transition_in_progress;
        nested_result = fixture.host.Launch(nested_descriptor, fixture.context);
    };
    const auto outer_descriptor = MakeTracingDescriptor("outer", outer);

    RODAK_CHECK(fixture.host.Launch(outer_descriptor, fixture.context));
    RODAK_CHECK(transition_seen);
    RODAK_CHECK_FALSE(nested_result);
    RODAK_CHECK_EQ(nested->factory_calls, 0);
    RODAK_CHECK_FALSE(fixture.host.GetState().transition_in_progress);

    trace->Clear();
    RODAK_CHECK(fixture.host.Launch(nested_descriptor, fixture.context));
    CheckEvents(
        *trace,
        {"nested.create", "outer.pause", "outer.destroy", "nested.resume"});
}

RODAK_TEST("host refreshes a supported theme without recreating app") {
    HostFixture fixture;
    auto trace = std::make_shared<EventTrace>();
    auto probe = std::make_shared<AppProbe>("theme", trace);
    probe->theme_results = {true};
    const auto descriptor = MakeTracingDescriptor("theme", probe);

    RODAK_CHECK(fixture.host.Launch(descriptor, fixture.context));
    trace->Clear();
    fixture.ui.AdvanceThemeRevision();
    RODAK_CHECK(fixture.host.RefreshCurrentTheme(fixture.context));
    RODAK_CHECK(fixture.host.RefreshCurrentTheme(fixture.context));

    CheckEvents(*trace, {"theme.theme"});
    RODAK_CHECK_EQ(probe->theme_calls, 1);
    RODAK_CHECK_EQ(probe->factory_calls, 1);
    RODAK_CHECK_EQ(probe->pause_calls, 0);
    RODAK_CHECK_EQ(probe->destroy_calls, 0);
}

RODAK_TEST("host force recreate does not repeat rejected theme callback") {
    HostFixture fixture;
    auto trace = std::make_shared<EventTrace>();
    auto probe = std::make_shared<AppProbe>("theme", trace);
    probe->theme_results = {false, true};
    const auto descriptor = MakeTracingDescriptor("theme", probe);

    RODAK_CHECK(fixture.host.Launch(descriptor, fixture.context));
    trace->Clear();
    fixture.ui.AdvanceThemeRevision();
    RODAK_CHECK_FALSE(fixture.host.RefreshCurrentTheme(fixture.context));
    RODAK_CHECK(fixture.host.RecreateCurrent(descriptor, fixture.context));

    CheckEvents(
        *trace,
        {"theme.theme", "theme.create", "theme.pause", "theme.destroy", "theme.resume"});
    RODAK_CHECK_EQ(probe->theme_calls, 1);
    RODAK_CHECK_EQ(probe->factory_calls, 2);
    RODAK_CHECK_EQ(probe->pause_calls, 1);
    RODAK_CHECK_EQ(probe->destroy_calls, 1);
    RODAK_CHECK_EQ(probe->resume_calls, 2);
}

RODAK_TEST("host closes current app exactly once and clears state") {
    HostFixture fixture;
    auto trace = std::make_shared<EventTrace>();
    auto probe = std::make_shared<AppProbe>("close", trace);
    const auto descriptor = MakeTracingDescriptor("close", probe);
    RODAK_CHECK(fixture.host.Launch(descriptor, fixture.context));
    trace->Clear();

    fixture.host.CloseCurrent();
    fixture.host.CloseCurrent();

    CheckEvents(*trace, {"close.pause", "close.destroy"});
    RODAK_CHECK_EQ(probe->pause_calls, 1);
    RODAK_CHECK_EQ(probe->destroy_calls, 1);
    const auto state = fixture.host.GetState();
    RODAK_CHECK_FALSE(state.has_current);
    RODAK_CHECK_FALSE(state.transition_in_progress);
    RODAK_CHECK_EQ(state.current_app_id, std::string());
    RODAK_CHECK_EQ(state.current_capabilities, PhoneCapability::kNone);
}

RODAK_TEST("host lets the current Home surface consume a repeated Home request") {
    rodakos_test::HostFixture fixture;
    auto trace = std::make_shared<rodakos_test::EventTrace>();
    auto probe = std::make_shared<rodakos_test::AppProbe>("home", trace);
    const auto descriptor = rodakos_test::MakeTracingDescriptor("home", probe);
    RODAK_CHECK(fixture.host.Launch(descriptor, fixture.context));
    trace->Clear();

    RODAK_CHECK_FALSE(fixture.host.HandleHomeRequest());
    probe->home_request_result = true;
    bool reentrant_result = true;
    probe->on_home_requested = [&]() {
        reentrant_result = fixture.host.HandleHomeRequest();
    };
    RODAK_CHECK(fixture.host.HandleHomeRequest());
    RODAK_CHECK_FALSE(reentrant_result);
    RODAK_CHECK_EQ(probe->home_request_calls, 2);
    rodakos_test::CheckEvents(*trace, {"home.home", "home.home"});
}
