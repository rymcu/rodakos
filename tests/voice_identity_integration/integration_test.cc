#include "fixture.h"

using namespace mqtt_host;

RODAK_TEST("real MQTT desired configures wake runtime persists record and reports confirmed identity") {
    identity_host::Fixture f;
    f.Identity(identity_host::Request(2, "持久身份"));
    f.WaitStatus("applied");
    RODAK_CHECK_EQ(f.runtime.Active().name, "持久身份");
    auto report = f.Report();
    const auto* identity = Get(report.get(), "voice_identity");
    RODAK_CHECK_EQ(std::string(Get(identity, "name")->valuestring), "持久身份");
    RODAK_CHECK_EQ(Get(identity, "revisionWatermark")->valueint, 2);
    RODAK_CHECK(cJSON_IsTrue(Get(identity, "activeConfirmed")));
    std::lock_guard<std::mutex> lock(wake_host::StoreMutex());
    const auto record = wake_host::Store().strings.at("voice_wake:identity");
    RODAK_CHECK(record.find("持久身份") != std::string::npos);
}

RODAK_TEST("real MQTT duplicate identity performs no new runtime apply and conflicting revision is rejected") {
    identity_host::Fixture f;
    f.Identity(identity_host::Request(2, "原身份"));
    const auto calls = f.runtime.ConfigureCalls();
    f.Identity(identity_host::Request(2, "原身份"));
    RODAK_CHECK_EQ(f.runtime.ConfigureCalls(), calls);
    f.Identity(identity_host::Request(2, "冲突身份"));
    f.WaitStatus("rejected");
    RODAK_CHECK_EQ(f.runtime.ConfigureCalls(), calls);
    auto report = f.Report();
    const auto* identity = Get(report.get(), "voice_identity");
    RODAK_CHECK_EQ(std::string(Get(identity, "name")->valuestring), "原身份");
    RODAK_CHECK(cJSON_IsTrue(Get(identity, "activeConfirmed")));
}

RODAK_TEST("real wake Unix expiry restores persistent identity and proactively reports retained watermark") {
    identity_host::Fixture f;
    f.Identity(identity_host::Request(2, "持久身份"));
    f.Identity(identity_host::Request(3, "临时身份", true));
    RODAK_CHECK_EQ(f.runtime.Active().name, "临时身份");
    f.SetClock(true, 1800000001000LL, 2000);
    f.WaitStatus("expired");
    RODAK_CHECK_EQ(f.runtime.Active().name, "持久身份");
    auto report = f.Report();
    const auto* identity = Get(report.get(), "voice_identity");
    RODAK_CHECK_EQ(Get(identity, "revision")->valueint, 2);
    RODAK_CHECK_EQ(Get(identity, "revisionWatermark")->valueint, 3);
    RODAK_CHECK(cJSON_IsTrue(Get(identity, "activeConfirmed")));
    f.Identity(identity_host::Request(3, "临时身份", true));
    RODAK_CHECK_EQ(f.runtime.Active().name, "持久身份");
}

RODAK_TEST("real wake failed candidate and rollback proactively report unconfirmed recovery failure") {
    identity_host::Fixture f;
    f.Identity(identity_host::Request(2, "确认身份"));
    f.runtime.FailCandidateAndRollback();
    f.Identity(identity_host::Request(3, "失败候选"));
    f.WaitStatus("recovery_failed");
    auto report = f.Report();
    const auto* identity = Get(report.get(), "voice_identity");
    RODAK_CHECK(cJSON_IsFalse(Get(identity, "activeConfirmed")));
    RODAK_CHECK_FALSE(std::string(Get(identity, "error")->valuestring).empty());
    const auto calls = f.runtime.ConfigureCalls();
    f.Identity(identity_host::Request(4, "不能解冻"));
    RODAK_CHECK_EQ(f.runtime.ConfigureCalls(), calls);
    f.WaitStatus("recovery_failed");
}
