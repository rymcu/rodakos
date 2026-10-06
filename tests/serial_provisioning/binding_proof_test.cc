#include "test_framework.h"
#include "phone_os/serial_provisioning_binding.h"

#include <string>

namespace {
const std::string kSecret =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const std::string kNonce =
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
}  // namespace

RODAK_TEST("Serial binding proof matches the independent SHA256-key HMAC vector") {
    std::string proof;
    RODAK_CHECK(rodakos::CreateSerialProvisioningBindingProof(kSecret, kNonce, proof));
    RODAK_CHECK_EQ(proof,
        "64263f87f8a6f0e743c2f601b93cf6b883e9938af08308584f7be498ae0a2f2a");
}

RODAK_TEST("Serial binding uses nonce ASCII bytes without changing hex case") {
    const std::string uppercase_nonce =
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789";
    std::string proof;
    RODAK_CHECK(rodakos::CreateSerialProvisioningBindingProof(kSecret, uppercase_nonce, proof));
    RODAK_CHECK_EQ(proof,
        "a96c4c50d411fbcea3fc29d7f9e9bf4e4d1bf18c15c3036fcd2d280325db080e");
}

RODAK_TEST("Serial binding rejects invalid challenges and clears stale proof") {
    for (const auto& nonce : {std::string(), std::string(63, 'a'), std::string(65, 'a'),
                              std::string(64, 'g'), std::string(63, 'a') + '\n',
                              std::string(63, 'a') + '\0'}) {
        std::string proof = "stale";
        RODAK_CHECK_FALSE(rodakos::IsValidSerialProvisioningBindingNonce(nonce));
        RODAK_CHECK_FALSE(rodakos::CreateSerialProvisioningBindingProof(kSecret, nonce, proof));
        RODAK_CHECK(proof.empty());
    }
    std::string proof = "stale";
    RODAK_CHECK_FALSE(rodakos::CreateSerialProvisioningBindingProof("", kNonce, proof));
    RODAK_CHECK(proof.empty());
}

RODAK_TEST("Serial binding proof changes across sessions and device identities") {
    std::string original;
    std::string next_session;
    std::string other_device;
    RODAK_CHECK(rodakos::CreateSerialProvisioningBindingProof(kSecret, kNonce, original));
    RODAK_CHECK(rodakos::CreateSerialProvisioningBindingProof(
        kSecret, std::string(64, '0'), next_session));
    RODAK_CHECK(rodakos::CreateSerialProvisioningBindingProof("other-device", kNonce, other_device));
    RODAK_CHECK_NE(original, next_session);
    RODAK_CHECK_NE(original, other_device);
}
