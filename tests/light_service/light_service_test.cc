#include "test_framework.h"
#include "host_driver.h"
#include "phone_os/light_service.h"
#include <chrono>
#include <future>
#include <thread>

using namespace rodakos;
using namespace std::chrono_literals;

namespace {
LightState Read(LightService& service, size_t index = 0) {
    LightState state;
    RODAK_CHECK(service.GetLight(index, state));
    return state;
}
LightPatch Brightness(int value) {
    LightPatch patch;
    patch.brightness_percent = value;
    return patch;
}
}

RODAK_TEST("Light discovery uses production board descriptors and distinct logical identities") {
    fake_light::Reset();
    LightService service;
    RODAK_CHECK(service.Init());
    const auto lights = service.ListLights();
    RODAK_CHECK_EQ(lights.size(), 2u);
    RODAK_CHECK_EQ(lights[0].id, "board_rgb");
    RODAK_CHECK_EQ(lights[0].led_count, 3u);
    RODAK_CHECK_EQ(lights[1].id, "accent");
    RODAK_CHECK_EQ(lights[1].first_led, 3u);
    RODAK_CHECK_EQ(lights[0].configuration_revision, 0u);
    RODAK_CHECK_EQ(lights[0].application, LightApplication::kDriverApplied);
    RODAK_CHECK_FALSE(lights[0].enabled);
    RODAK_CHECK_EQ(fake_light::clear_calls.load(), 1u);
    service.Init();
    RODAK_CHECK_EQ(fake_light::clear_calls.load(), 1u);
}

RODAK_TEST("Light patch captures previous state and applies production RGB scaling") {
    fake_light::Reset();
    LightService service;
    LightPatch patch;
    patch.enabled = true;
    patch.brightness_percent = 50;
    patch.color = RgbColor{255, 101, 0};
    const auto result = service.ApplyLightPatch("board_rgb", patch);
    RODAK_CHECK(result.accepted);
    RODAK_CHECK_FALSE(result.previous.enabled);
    RODAK_CHECK_EQ(result.previous.brightness_percent, 60);
    RODAK_CHECK_EQ(result.state.brightness_percent, 50);
    RODAK_CHECK_EQ(result.id, "board_rgb");
    RODAK_CHECK_EQ(result.configuration_revision, 1u);
    RODAK_CHECK_EQ(result.application, LightApplication::kDriverApplied);
    RODAK_CHECK(result.error_code.empty());
    for (size_t i = 0; i < 3; ++i) {
        const auto pixel = fake_light::Pixels()[i];
        RODAK_CHECK_EQ(pixel.red, 128u);
        RODAK_CHECK_EQ(pixel.green, 51u);
        RODAK_CHECK_EQ(pixel.blue, 0u);
    }
    RODAK_CHECK_EQ(fake_light::Pixels()[3].red, 0u);
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 1u);
}

RODAK_TEST("Light patch by identity writes the selected range only") {
    fake_light::Reset();
    LightService service;
    LightPatch patch;
    patch.enabled = true;
    patch.color = RgbColor{100, 50, 25};
    RODAK_CHECK(service.ApplyLightPatch("accent", patch).accepted);
    RODAK_CHECK_EQ(fake_light::pixel_calls.load(), 1u);
    RODAK_CHECK_EQ(fake_light::Pixels()[0].red, 0u);
    RODAK_CHECK_EQ(fake_light::Pixels()[3].red, 60u);
    RODAK_CHECK_EQ(Read(service).configuration_revision, 0u);
    RODAK_CHECK_EQ(Read(service, 1).configuration_revision, 1u);
}

RODAK_TEST("Light native setters preserve color enable and brightness disabled semantics") {
    fake_light::Reset();
    LightService service;
    RODAK_CHECK(service.SetBrightness(0, 25));
    RODAK_CHECK_FALSE(Read(service).enabled);
    RODAK_CHECK_EQ(fake_light::Pixels()[0].blue, 0u);
    RODAK_CHECK(service.SetColor(0, {100, 80, 60}));
    RODAK_CHECK(Read(service).enabled);
    RODAK_CHECK_EQ(Read(service).brightness_percent, 25);
    RODAK_CHECK_EQ(fake_light::Pixels()[0].red, 25u);
    RODAK_CHECK(service.Toggle(0));
    RODAK_CHECK_FALSE(Read(service).enabled);
    RODAK_CHECK(service.SetEnabled(0, true));
    RODAK_CHECK(service.SetState(0, false, 250, {2, 4, 6}));
    RODAK_CHECK_EQ(Read(service).brightness_percent, 100);
    RODAK_CHECK_FALSE(Read(service).enabled);
    RODAK_CHECK(service.Apply(0));
    RODAK_CHECK_EQ(Read(service).configuration_revision, 6u);
}

RODAK_TEST("Light brightness zero commits enabled configuration but writes black") {
    fake_light::Reset();
    LightService service;
    RODAK_CHECK(service.SetState(0, true, 0, {255, 255, 255}));
    RODAK_CHECK(Read(service).enabled);
    for (const auto& pixel : fake_light::Pixels()) RODAK_CHECK_EQ(pixel.red, 0u);
}

RODAK_TEST("Light invalid patches and missing identities perform no driver writes") {
    fake_light::Reset();
    LightService service;
    service.Init();
    RODAK_CHECK_EQ(service.ApplyLightPatch(0, {}).error_code, "invalid-patch");
    for (int value : {-1, 101, 1000}) {
        const auto result = service.ApplyLightPatch(0, Brightness(value));
        RODAK_CHECK_FALSE(result.accepted);
        RODAK_CHECK_EQ(result.error_code, "invalid-patch");
        RODAK_CHECK_EQ(result.configuration_revision, 0u);
    }
    RODAK_CHECK_EQ(service.ApplyLightPatch("missing", Brightness(40)).error_code, "unknown-light");
    RODAK_CHECK_EQ(service.ApplyLightPatch(999, Brightness(40)).error_code, "unknown-light");
    RODAK_CHECK_FALSE(service.Toggle(999));
    RODAK_CHECK_FALSE(service.Apply(999));
    RODAK_CHECK_EQ(fake_light::pixel_calls.load(), 0u);
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 0u);
}

RODAK_TEST("Light pixel failure keeps accepted state and marks hardware unverified") {
    fake_light::Reset();
    LightService service;
    RODAK_CHECK(service.SetState(0, true, 50, {100, 80, 60}));
    fake_light::fail_pixel_call = fake_light::pixel_calls + 1;
    const auto result = service.ApplyLightPatch(0, Brightness(80));
    RODAK_CHECK_FALSE(result.accepted);
    RODAK_CHECK_EQ(result.error_code, "driver-write-failed");
    RODAK_CHECK_EQ(result.previous.brightness_percent, 50);
    RODAK_CHECK_EQ(result.state.brightness_percent, 50);
    RODAK_CHECK_EQ(result.configuration_revision, 1u);
    RODAK_CHECK_EQ(result.application, LightApplication::kUnverified);
    RODAK_CHECK_FALSE(Read(service).available);
    RODAK_CHECK_EQ(Read(service).brightness_percent, 50);
    RODAK_CHECK_EQ(Read(service).application, LightApplication::kUnverified);
    RODAK_CHECK_NE(Read(service).last_error, ESP_OK);
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 1u);
}

RODAK_TEST("Light partial driver-buffer failure is not reported as physical rollback") {
    fake_light::Reset();
    LightService service;
    RODAK_CHECK(service.SetState(0, true, 50, {100, 80, 60}));
    fake_light::fail_pixel_call = fake_light::pixel_calls + 2;
    const auto result = service.ApplyLightPatch(0, Brightness(80));
    RODAK_CHECK_FALSE(result.accepted);
    RODAK_CHECK_EQ(result.state.brightness_percent, 50);
    RODAK_CHECK_EQ(fake_light::Pixels()[0].red, 80u);
    RODAK_CHECK_EQ(fake_light::Pixels()[1].red, 50u);
    RODAK_CHECK_EQ(fake_light::refresh_calls.load(), 1u);
}

RODAK_TEST("Light refresh failure retains revision and next success can recover") {
    fake_light::Reset();
    LightService service;
    RODAK_CHECK(service.SetState(0, true, 50, {100, 80, 60}));
    fake_light::fail_refresh = true;
    RODAK_CHECK_FALSE(service.SetColor(0, {200, 150, 100}));
    RODAK_CHECK_EQ(Read(service).color.red, 100);
    RODAK_CHECK_EQ(Read(service).configuration_revision, 1u);
    RODAK_CHECK_FALSE(Read(service).available);
    fake_light::fail_refresh = false;
    const auto result = service.ApplyLightPatch(0, Brightness(75));
    RODAK_CHECK(result.accepted);
    RODAK_CHECK_EQ(result.previous.color.red, 100);
    RODAK_CHECK_EQ(result.state.color.red, 100);
    RODAK_CHECK_EQ(result.configuration_revision, 2u);
    RODAK_CHECK(Read(service).available);
    RODAK_CHECK_EQ(Read(service).last_error, ESP_OK);
}

RODAK_TEST("Light missing board handle rejects without accepting software state") {
    fake_light::Reset();
    fake_light::unavailable = true;
    LightService service;
    const auto result = service.ApplyLightPatch("board_rgb", Brightness(10));
    RODAK_CHECK_FALSE(result.accepted);
    RODAK_CHECK_EQ(result.error_code, "driver-unavailable");
    RODAK_CHECK_EQ(result.state.brightness_percent, 60);
    RODAK_CHECK_EQ(result.configuration_revision, 0u);
    RODAK_CHECK_EQ(fake_light::pixel_calls.load(), 0u);
}

RODAK_TEST("Light discovery clear failure remains unverified until successful application") {
    fake_light::Reset();
    fake_light::fail_clear = true;
    LightService service;
    service.Init();
    RODAK_CHECK_FALSE(Read(service).available);
    RODAK_CHECK_EQ(Read(service).application, LightApplication::kUnverified);
    RODAK_CHECK(service.SetEnabled(0, true));
    RODAK_CHECK(Read(service).available);
}

RODAK_TEST("Light snapshot and subsequent patch serialize across blocked driver refresh") {
    fake_light::Reset();
    LightService service;
    service.Init();
    std::promise<void> entered, release;
    auto entered_future = entered.get_future();
    auto release_future = release.get_future().share();
    std::atomic<bool> first{true};
    fake_light::before_refresh = [&]() {
        if (first.exchange(false)) {
            entered.set_value();
            release_future.wait();
        }
    };
    LightPatch patch;
    patch.enabled = true;
    patch.color = RgbColor{101, 102, 103};
    auto writer = std::async(std::launch::async, [&]() { return service.ApplyLightPatch(0, patch); });
    entered_future.wait();
    auto next_writer = std::async(std::launch::async, [&]() {
        return service.ApplyLightPatch(0, Brightness(25));
    });
    auto reader = std::async(std::launch::async, [&]() { return Read(service); });
    const bool writer_blocked = next_writer.wait_for(20ms) == std::future_status::timeout;
    const bool reader_blocked = reader.wait_for(20ms) == std::future_status::timeout;
    release.set_value();
    const auto first_result = writer.get();
    const auto second_result = next_writer.get();
    reader.get();
    fake_light::before_refresh = {};
    RODAK_CHECK(writer_blocked);
    RODAK_CHECK(reader_blocked);
    RODAK_CHECK(first_result.accepted);
    RODAK_CHECK_EQ(first_result.configuration_revision, 1u);
    RODAK_CHECK_EQ(first_result.state.brightness_percent, 60);
    RODAK_CHECK(second_result.accepted);
    RODAK_CHECK(second_result.previous.enabled);
    RODAK_CHECK_EQ(second_result.previous.color.red, 101);
    RODAK_CHECK_EQ(second_result.state.brightness_percent, 25);
    RODAK_CHECK_EQ(second_result.configuration_revision, 2u);
    RODAK_CHECK_EQ(Read(service).color.red, 101);
}

RODAK_TEST("Light native toggles never lose concurrent updates") {
    fake_light::Reset();
    LightService service;
    service.Init();
    std::atomic<unsigned> accepted{0};
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            for (unsigned j = 0; j < 25; ++j) if (service.Toggle(0)) ++accepted;
        });
    }
    for (auto& thread : threads) thread.join();
    RODAK_CHECK_EQ(accepted.load(), 100u);
    RODAK_CHECK_FALSE(Read(service).enabled);
    RODAK_CHECK_EQ(Read(service).configuration_revision, 100u);
}
