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

RODAK_TEST("Voice identity rejects invalid revisions modes controls and non-wire-safe expiry") {
    for (int mutation = 0; mutation < 5; ++mutation) {
        auto config = rodakos::DefaultVoiceIdentityConfig();
        if (mutation == 0) config.revision = 0;
        if (mutation == 1) config.mode = static_cast<rodakos::VoiceIdentityApplyMode>(99);
        if (mutation == 2) config.name += '\x7f';
        if (mutation == 3) config.wake_command += '\0';
        if (mutation == 4) {
            config.mode = rodakos::VoiceIdentityApplyMode::kTemporary;
            config.expires_at_ms = 9'007'199'254'740'992LL;
        }
        rodakos::VoiceIdentityConfig normalized;
        std::string error;
        RODAK_CHECK_FALSE(rodakos::NormalizeVoiceIdentityConfig(config, normalized, error));
        RODAK_CHECK_FALSE(error.empty());
    }
}
