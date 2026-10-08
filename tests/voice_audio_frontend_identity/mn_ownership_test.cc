#include "test_framework.h"
#include "host_runtime.h"
#include "phone_os/voice_audio_frontend.h"

#include <iostream>

namespace {
using rodakos::AudioCodecInput;
using rodakos::VoiceAudioFrontend;
using namespace rodakos_test::voice_frontend;

rodakos::VoiceIdentityConfig Identity() {
    auto config = rodakos::DefaultVoiceIdentityConfig();
    config.wake_command = "ni hao ce shi";
    config.revision = 2;
    return config;
}
}

RODAK_TEST("MultiNet create owns the single real SDK registry allocation") {
    Reset();
    AudioCodecInput input;
    VoiceAudioFrontend frontend(input);
    RODAK_CHECK(frontend.Init());
    RODAK_CHECK(RegistryMatchesModel());
    const auto stats = RegistryStats();
    std::cout << "MN_ALLOCATION_OBSERVED allocations=" << stats.allocations
              << " reallocations=" << stats.reallocations << std::endl;
    RODAK_CHECK_EQ(stats.allocations, 1U);
    RODAK_CHECK_EQ(stats.reallocations, 0U);
}

RODAK_TEST("MultiNet destroy frees the real SDK registry exactly once") {
    Reset();
    AudioCodecInput input;
    VoiceAudioFrontend frontend(input);
    RODAK_CHECK(frontend.Init());
    RODAK_CHECK(frontend.ConfigureWakeWord(Identity()));
    RODAK_CHECK(RegistryMatchesModel());
    frontend.Deinit();
    const auto stats = RegistryStats();
    std::cout << "MN_DESTRUCTION_OBSERVED frees=" << stats.frees
              << " empty_frees=" << stats.free_without_registry << std::endl;
    RODAK_CHECK_EQ(stats.frees, 1U);
    RODAK_CHECK_EQ(stats.free_without_registry, 0U);
    RODAK_CHECK_FALSE(RegistryPresent());
    frontend.Deinit();
    RODAK_CHECK_EQ(RegistryStats().frees, 1U);
    RODAK_CHECK(frontend.Init());
    RODAK_CHECK(RegistryMatchesModel());
    frontend.Deinit();
    RODAK_CHECK_EQ(RegistryStats().allocations, 2U);
    RODAK_CHECK_EQ(RegistryStats().frees, 2U);
    RODAK_CHECK_EQ(RegistryStats().free_without_registry, 0U);
}

RODAK_TEST("MultiNet post-create failures destroy the owned registry once and recover") {
    for (const auto failure : {0, 1, 2}) {
        Reset();
        AudioCodecInput input;
        VoiceAudioFrontend frontend(input);
        if (failure == 0) SetCommandAddResult(false);
        if (failure == 1) SetCommandUpdateResult(false);
        if (failure == 2) SetModelChunkSamples(0);
        RODAK_CHECK_FALSE(frontend.Init());
        RODAK_CHECK_EQ(ModelCreateCount(), 1U);
        RODAK_CHECK_EQ(ModelDestroyCount(), 1U);
        RODAK_CHECK_EQ(RegistryStats().allocations, 1U);
        RODAK_CHECK_EQ(RegistryStats().frees, 1U);
        RODAK_CHECK_EQ(RegistryStats().free_without_registry, 0U);
        RODAK_CHECK_FALSE(RegistryPresent());
        SetCommandAddResult(true);
        SetCommandUpdateResult(true);
        SetModelChunkSamples(320);
        RODAK_CHECK(frontend.Init());
        RODAK_CHECK(RegistryMatchesModel());
        frontend.Deinit();
        RODAK_CHECK_EQ(RegistryStats().frees, 2U);
    }
}

RODAK_TEST("MultiNet reconfigure failures preserve sole model ownership") {
    for (const auto failure : {0, 1, 2}) {
        Reset();
        AudioCodecInput input;
        VoiceAudioFrontend frontend(input);
        RODAK_CHECK(frontend.Init());
        if (failure == 0) SetCommandClearResult(false);
        if (failure == 1) SetCommandAddResult(false);
        if (failure == 2) SetCommandUpdateResult(false);
        RODAK_CHECK_FALSE(frontend.ConfigureWakeWord(Identity()));
        RODAK_CHECK_EQ(RegistryStats().allocations, 1U);
        RODAK_CHECK_EQ(RegistryStats().frees, 1U);
        RODAK_CHECK_EQ(RegistryStats().free_without_registry, 0U);
        RODAK_CHECK_FALSE(RegistryPresent());
    }
}

RODAK_TEST("MultiNet no returned model means no caller registry cleanup") {
    Reset();
    AudioCodecInput input;
    VoiceAudioFrontend frontend(input);
    SetModelCreateResult(false);
    RODAK_CHECK_FALSE(frontend.Init());
    frontend.Deinit();
    RODAK_CHECK_EQ(ModelDestroyCount(), 0U);
    RODAK_CHECK_EQ(RegistryStats().allocations, 0U);
    RODAK_CHECK_EQ(RegistryStats().frees, 0U);
    SetModelCreateResult(true);
    RODAK_CHECK(frontend.Init());
    RODAK_CHECK(RegistryMatchesModel());
}

RODAK_TEST("MultiNet unaudited model is rejected before create") {
    Reset();
    AudioCodecInput input;
    VoiceAudioFrontend frontend(input);
    SetSelectedModel("mn6_cn");
    RODAK_CHECK_FALSE(frontend.Init());
    RODAK_CHECK_EQ(frontend.LastErrorSnapshot(), "Unsupported MultiNet command ownership contract");
    RODAK_CHECK_EQ(ModelCreateCount(), 0U);
    RODAK_CHECK_EQ(RegistryStats().allocations, 0U);
    RODAK_CHECK_EQ(RegistryStats().frees, 0U);
}
