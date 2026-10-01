#include "rodak_appearance_policy.h"
#include "test_framework.h"

RODAK_TEST("appearance trial confirms only the selected pending revision") {
    rodakos::AppearanceRevisionState state{4, 5, 0, 0, 0};
    RODAK_CHECK(rodakos::BeginAppearanceTrial(state));
    RODAK_CHECK_EQ(state.previous, uint32_t(4)); RODAK_CHECK_EQ(state.active, uint32_t(4));
    RODAK_CHECK_FALSE(rodakos::ConfirmAppearanceTrial(state, 6));
    RODAK_CHECK(rodakos::ConfirmAppearanceTrial(state, 5));
    RODAK_CHECK_EQ(state.active, uint32_t(5)); RODAK_CHECK_EQ(state.previous, uint32_t(4));
    RODAK_CHECK_EQ(state.pending, uint32_t(0)); RODAK_CHECK_EQ(state.trial, uint32_t(0));
}
RODAK_TEST("appearance unconfirmed trial rolls back to last good and blocks replay") {
    rodakos::AppearanceRevisionState state{4, 5, 0, 0, 0};
    RODAK_CHECK(rodakos::BeginAppearanceTrial(state)); RODAK_CHECK(rodakos::RecoverAppearanceTrial(state));
    RODAK_CHECK_EQ(state.active, uint32_t(4)); RODAK_CHECK_EQ(state.rejected, uint32_t(5));
    RODAK_CHECK_FALSE(rodakos::ShouldAcceptAppearanceRevision(state, 5, 5));
    RODAK_CHECK(rodakos::ShouldAcceptAppearanceRevision(state, 6, 5));
    RODAK_CHECK_FALSE(rodakos::RecoverAppearanceTrial(state));
}
RODAK_TEST("appearance first failed trial returns to builtin and newer revision may retry") {
    rodakos::AppearanceRevisionState state{0, 1, 0, 0, 0};
    RODAK_CHECK(rodakos::BeginAppearanceTrial(state)); RODAK_CHECK(rodakos::RecoverAppearanceTrial(state));
    RODAK_CHECK_EQ(state.active, uint32_t(0)); RODAK_CHECK_FALSE(rodakos::ShouldAcceptAppearanceRevision(state, 1, 1));
    RODAK_CHECK(rodakos::ShouldAcceptAppearanceRevision(state, 2, 1));
}
RODAK_TEST("appearance trial rejects duplicate stale and superseded releases") {
    rodakos::AppearanceRevisionState state{4, 5, 0, 0, 0};
    RODAK_CHECK_FALSE(rodakos::ShouldAcceptAppearanceRevision(state, 4, 5));
    RODAK_CHECK_FALSE(rodakos::ShouldAcceptAppearanceRevision(state, 5, 5));
    RODAK_CHECK_FALSE(rodakos::ShouldAcceptAppearanceRevision(state, 6, 7));
    RODAK_CHECK(rodakos::ShouldAcceptAppearanceRevision(state, 7, 7));
    RODAK_CHECK(rodakos::BeginAppearanceTrial(state)); RODAK_CHECK_FALSE(rodakos::BeginAppearanceTrial(state));
}
RODAK_TEST("appearance publisher rotation resets content epoch only after identity changes") {
    RODAK_CHECK_FALSE(rodakos::ShouldResetAppearanceEpoch("key", "http://rodak", "key", "http://rodak"));
    RODAK_CHECK(rodakos::ShouldResetAppearanceEpoch("old-key", "http://rodak", "new-key", "http://rodak"));
    RODAK_CHECK(rodakos::ShouldResetAppearanceEpoch("key", "http://old-pc", "key", "http://new-pc"));
    RODAK_CHECK(rodakos::ShouldResetAppearanceEpoch("", "", "key", "http://rodak"));
}
RODAK_TEST("appearance HTTP refreshes rejected access token once without repeated retries") {
    RODAK_CHECK(rodakos::ShouldRetryAppearanceAuthentication(401, 0));
    RODAK_CHECK_FALSE(rodakos::ShouldRetryAppearanceAuthentication(401, 1));
    RODAK_CHECK_FALSE(rodakos::ShouldRetryAppearanceAuthentication(403, 0));
    RODAK_CHECK_FALSE(rodakos::ShouldRetryAppearanceAuthentication(500, 0));
}
RODAK_TEST("appearance resume requires exact partial response range and remaining length") {
    using Action = rodakos::AppearanceRangeAction;
    RODAK_CHECK_EQ(rodakos::ValidateAppearanceRangeResponse(206, 4096, 10000, 5904, "bytes 4096-9999/10000"), Action::kAppend);
    RODAK_CHECK_EQ(rodakos::ValidateAppearanceRangeResponse(206, 4096, 10000, 5904, "bytes 4095-9999/10000"), Action::kReject);
    RODAK_CHECK_EQ(rodakos::ValidateAppearanceRangeResponse(206, 4096, 10000, 5904, "bytes 4096-9998/10000"), Action::kReject);
    RODAK_CHECK_EQ(rodakos::ValidateAppearanceRangeResponse(206, 4096, 10000, 5904, "bytes 4096-9999/10001"), Action::kReject);
    RODAK_CHECK_EQ(rodakos::ValidateAppearanceRangeResponse(206, 4096, 10000, 5903, "bytes 4096-9999/10000"), Action::kReject);
    RODAK_CHECK_EQ(rodakos::ValidateAppearanceRangeResponse(206, 4096, 10000, -1, "bytes 4096-9999/10000"), Action::kReject);
}
RODAK_TEST("appearance resume rejects malformed range and safely restarts ignored range") {
    using Action = rodakos::AppearanceRangeAction;
    for (const char* range : {"", "bytes */10000", "bytes 4096-9999/*", "bytes 4096 -9999/10000", "bytes 4096-9999/10000junk", "bytes 999999999999999999999-9999/10000"}) {
        RODAK_CHECK_EQ(rodakos::ValidateAppearanceRangeResponse(206, 4096, 10000, 5904, range), Action::kReject);
    }
    RODAK_CHECK_EQ(rodakos::ValidateAppearanceRangeResponse(200, 4096, 10000, 10000, ""), Action::kRestart);
    RODAK_CHECK_EQ(rodakos::ValidateAppearanceRangeResponse(200, 0, 10000, 10000, ""), Action::kRestart);
    RODAK_CHECK_EQ(rodakos::ValidateAppearanceRangeResponse(206, 0, 10000, 10000, "bytes 0-9999/10000"), Action::kReject);
    RODAK_CHECK_EQ(rodakos::ValidateAppearanceRangeResponse(416, 4096, 10000, 0, ""), Action::kReject);
}
RODAK_TEST("appearance interrupted downloads use bounded increasing delays") {
    RODAK_CHECK_EQ(rodakos::AppearanceDownloadRetryDelay(1), uint32_t(5000));
    RODAK_CHECK_EQ(rodakos::AppearanceDownloadRetryDelay(2), uint32_t(15000));
    RODAK_CHECK_EQ(rodakos::AppearanceDownloadRetryDelay(3), uint32_t(60000));
    RODAK_CHECK_EQ(rodakos::AppearanceDownloadRetryDelay(4), uint32_t(0));
    RODAK_CHECK_EQ(rodakos::AppearanceDownloadRetryDelay(0), uint32_t(0));
}
RODAK_TEST("appearance desired decoding bounds input before parsing and accepts only protocol fields") {
    const std::string desired = "{\"deploymentId\":\"d1\",\"releaseId\":\"r1\",\"keyId\":\"" + std::string(64, 'a') + "\",\"mode\":\"custom\",\"revision\":1}";
    rodakos::AppearanceDesired decoded;
    RODAK_CHECK(rodakos::DecodeAppearanceDesired(desired, decoded));
    RODAK_CHECK_EQ(decoded.deployment_id, std::string("d1")); RODAK_CHECK_EQ(decoded.revision, uint32_t(1));
    const std::string padded = desired + std::string(rodakos::kAppearanceMaxDesiredBytes - desired.size(), ' ');
    RODAK_CHECK(rodakos::DecodeAppearanceDesired(padded, decoded));
    RODAK_CHECK_FALSE(rodakos::DecodeAppearanceDesired(padded + " ", decoded));
    RODAK_CHECK_EQ(decoded.revision, uint32_t(0));
    for (const char* field : {",\"revision\":2}", ",\"unknown\":true}", ",\"unknown\":{\"nested\":true}}"}) {
        RODAK_CHECK_FALSE(rodakos::DecodeAppearanceDesired(desired.substr(0, desired.size() - 1) + field, decoded));
    }
    RODAK_CHECK_FALSE(rodakos::DecodeAppearanceDesired(desired + "{}", decoded));
    RODAK_CHECK_FALSE(rodakos::DecodeAppearanceDesired(desired + std::string(1, '\0'), decoded));
}
RODAK_TEST("appearance desired validates builtin mode identity and unsigned revisions") {
    const std::string prefix = "{\"deploymentId\":\"d1\",\"releaseId\":\"\",\"keyId\":\"" + std::string(64, 'a') + "\",\"mode\":\"builtin\",\"revision\":";
    rodakos::AppearanceDesired decoded;
    RODAK_CHECK(rodakos::DecodeAppearanceDesired(prefix + "4294967295}", decoded));
    RODAK_CHECK_EQ(decoded.revision, uint32_t(0xffffffffu));
    for (const char* revision : {"0", "-1", "1.5", "4294967296", "1e300", "null", "\"1\""}) {
        RODAK_CHECK_FALSE(rodakos::DecodeAppearanceDesired(prefix + revision + "}", decoded));
    }
    RODAK_CHECK_FALSE(rodakos::DecodeAppearanceDesired("{\"deploymentId\":\"d1\\u0000evil\",\"releaseId\":\"r1\",\"keyId\":\"" + std::string(64, 'a') + "\",\"mode\":\"custom\",\"revision\":1}", decoded));
}
