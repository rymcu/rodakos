#include "test_framework.h"

#include "phone_os/voice_identity.h"

RODAK_TEST("Voice identity defaults to the Rodak Chinese command") {
    const auto config = rodakos::DefaultVoiceIdentityConfig();
    RODAK_CHECK_EQ(config.name, "罗达克");
    RODAK_CHECK_EQ(config.wake_word, "你好达克");
    RODAK_CHECK_EQ(config.wake_command, "ni hao da ke");
    RODAK_CHECK(config.mode == rodakos::VoiceIdentityApplyMode::kPersistent);
}

RODAK_TEST("Temporary voice identity requires a future expiry") {
    auto config = rodakos::DefaultVoiceIdentityConfig();
    config.mode = rodakos::VoiceIdentityApplyMode::kTemporary;
    config.expires_at_ms = 2'000;

    rodakos::VoiceIdentityConfig normalized;
    std::string error;
    RODAK_CHECK(rodakos::NormalizeVoiceIdentityConfig(config, normalized, error));
    RODAK_CHECK_FALSE(rodakos::IsVoiceIdentityExpired(normalized, 1'999));
    RODAK_CHECK(rodakos::IsVoiceIdentityExpired(normalized, 2'000));
}

