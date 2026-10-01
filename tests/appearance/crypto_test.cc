#include "rodak_appearance_crypto.h"
#include "test_framework.h"
#include "fixtures.h"

namespace {
bool Verify(const std::string& manifest, const std::string& pem = kPem, const std::string& device = "aa:bb:cc:dd:ee:ff") {
    rodakos::AppearanceSignedRelease release; std::string error;
    return rodakos::VerifyAppearanceManifest(manifest, pem, kKeyId, device, release, error);
}
}
RODAK_TEST("appearance trust computes the same RSA SPKI key id as Rodak") {
    std::string id; RODAK_CHECK(rodakos::AppearancePublicKeyId(kPem, id)); RODAK_CHECK_EQ(id, std::string(kKeyId));
    RODAK_CHECK_EQ(rodakos::AppearanceKeyFingerprint(id).size(), size_t(39)); RODAK_CHECK_FALSE(rodakos::AppearancePublicKeyId("invalid", id));
}
RODAK_TEST("appearance manifest accepts authenticated custom and builtin releases") {
    RODAK_CHECK(Verify(kManifest)); RODAK_CHECK(Verify(kBuiltin));
    rodakos::AppearanceSignedRelease release; std::string error;
    RODAK_CHECK(rodakos::VerifyAppearanceManifest(kManifest, kPem, kKeyId, "aa:bb:cc:dd:ee:ff", release, error));
    RODAK_CHECK_EQ(release.revision, uint32_t(3)); RODAK_CHECK_EQ(release.package_size, size_t(1234));
}
RODAK_TEST("appearance manifest rejects wrong authority altered signature and changed device") {
    RODAK_CHECK_FALSE(Verify(kManifest, kWrongPem)); RODAK_CHECK_FALSE(Verify(kManifest, kPem, "other-device"));
    std::string changed = kManifest; const auto position = changed.find("signature\":\""); changed[position + 12] = changed[position + 12] == 'A' ? 'B' : 'A';
    RODAK_CHECK_FALSE(Verify(changed)); RODAK_CHECK_FALSE(Verify("{}"));
}
RODAK_TEST("appearance signed payload enforces product revision size and checksum bounds") {
    for (const char* manifest : {kWrongDevice, kWrongProduct, kOversize, kFraction, kBadSha, kBadMode}) RODAK_CHECK_FALSE(Verify(manifest));
}
RODAK_TEST("appearance manifest rejects trailing JSON or garbage while allowing whitespace") {
    RODAK_CHECK_FALSE(Verify(std::string(kManifest) + "garbage"));
    RODAK_CHECK_FALSE(Verify(std::string(kManifest) + "{}"));
    RODAK_CHECK_FALSE(Verify(kTrailingPayload));
    RODAK_CHECK(Verify(std::string(kManifest) + "\n \t"));
    RODAK_CHECK(Verify(kWhitespacePayload));
}
