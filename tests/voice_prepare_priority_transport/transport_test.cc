#include "test_framework.h"
#include "host_runtime.h"
#include "sdk_runtime.h"
#include "phone_os/device_cloud_config.h"
#include "phone_os/realtime_voice_transport.h"
#include "phone_os/voice_prepare_priority_observer.h"
#include <cJSON.h>
#include <initializer_list>
#include <memory>

using rodakos::VoicePreparePriorityStage;
namespace {
using Stage = VoicePreparePriorityStage;
void RespondVoice() {
    trust_test::RespondBound();
    const auto token_url=rodakos::ServerTrustUrlOrigin(trust_test::BootstrapUrl())+"/api/v1/aiot/devices/auth/token";
    auto* root=cJSON_Parse(trust_test::replies[token_url].body.c_str());
    auto* voice=cJSON_Parse(R"({"schema":"rodak-realtime-voice/v1","protocol":"rodak-realtime-voice",
        "protocolVersion":1,"transport":"websocket","authMode":"device-token",
        "uplink":{"codec":"opus","sampleRateHz":16000,"channels":1,"frameDurationMs":60},
        "downlink":{"codec":"opus","sampleRateHz":24000,"channels":1,"frameDurationMs":60},
        "limits":{"maxAudioFrameBytes":8192,"maxControlBytes":65536},"features":[],
        "capabilities":["session","audio-input","audio-output"],
        "events":["session.open","session.ready","input.start","input.stop","wake.detected",
                  "playback.abort","vad","output.start","output.stop","session.end","mcp","error"]})");
    const auto endpoint="wss://"+trust_test::TestTrust().tls_name+":9443/voice";
    cJSON_AddStringToObject(voice,"endpoint",endpoint.c_str());
    cJSON_AddItemToObject(cJSON_GetObjectItem(root,"data"),"realtimeVoice",voice);
    char* text=cJSON_PrintUnformatted(root);
    trust_test::replies[token_url].body=text;
    cJSON_free(text);cJSON_Delete(root);
}
struct Fixture {
    rodakos::DeviceCloudConfigService cloud;
    std::unique_ptr<rodakos::RodakRealtimeVoiceTransport> transport;
    explicit Fixture(unsigned fail_creation=0,bool configured=true,bool voice=true) {
        prepare_host::Reset(fail_creation);trust_test::Reset();
        rodakos::VoicePreparePrioritySnapshot stale{};
        while(rodakos::TryTakeCompletedVoicePreparePrioritySnapshot(stale)) {}
        if(configured) {
            trust_test::SeedBoundLegacy();
            const auto trust=trust_test::TestTrust();std::string proof;
            RODAK_CHECK_EQ(cloud.SaveSerialProvisioning(trust_test::BootstrapUrl(),std::string(64,'a'),proof,&trust),rodakos::ProvisioningUrlSaveResult::kSaved);
            if(voice) RespondVoice(); else trust_test::RespondBound();
            rodakos::DeviceCloudConfig config;
            RODAK_CHECK(cloud.Refresh(config));
            RODAK_CHECK_EQ(config.has_realtime_voice_config,voice);
        }
        transport=std::make_unique<rodakos::RodakRealtimeVoiceTransport>(cloud);
        trust_test::requests.clear();prepare_host::ClearTrace();
    }
    ~Fixture() { prepare_host::after_cloud_return={}; }
};
void CheckSnapshot(std::initializer_list<Stage> stages,std::initializer_list<bool> holds,unsigned calls) {
    rodakos::VoicePreparePrioritySnapshot value{};
    RODAK_CHECK(rodakos::TryTakeCompletedVoicePreparePrioritySnapshot(value));
    RODAK_CHECK_EQ(value.count,stages.size());
    RODAK_CHECK_FALSE(prepare_host::PriorityOpenStates().back());
    unsigned i=0;
    for(auto stage:stages) {
        RODAK_CHECK_EQ(value.samples[i].stage,stage);
        RODAK_CHECK_EQ(value.samples[i].effective_priority,4U);
        RODAK_CHECK_EQ(value.samples[i].self_handle,value.self_handle);
        RODAK_CHECK(value.samples[i].after_us>=value.samples[i].before_us);
        const unsigned expected_returns=(stage==Stage::kCloudReturned || stage==Stage::kOpenReleased) ? calls : 0;
        RODAK_CHECK_EQ(prepare_host::CloudReturnsAtPriorityReads()[i],expected_returns);++i;
    }
    RODAK_CHECK_EQ(value.flags,0U);
    RODAK_CHECK_EQ(prepare_host::PriorityOpenStates(),std::vector<bool>(holds));
    RODAK_CHECK_EQ(prepare_host::CloudCalls(),calls);
    RODAK_CHECK_FALSE(prepare_host::OpenHeld());
    RODAK_CHECK_FALSE(rodakos::TryTakeCompletedVoicePreparePrioritySnapshot(value));
}
void CheckCloudReturn() {
    CheckSnapshot({Stage::kPrepareBegin,Stage::kOpenAcquired,Stage::kCloudReturned,Stage::kOpenReleased},
                  {false,true,true,false},1);
}
}

RODAK_TEST("Start failure closes observation without open mutex or Cloud call") {
    Fixture f(3);
    RODAK_CHECK_FALSE(f.transport->PrepareInteraction());
    CheckSnapshot({Stage::kPrepareBegin,Stage::kExitNoOpen},{false,false},0);
}
RODAK_TEST("Failed open take closes no-open observation without Cloud call") {
    Fixture f;prepare_host::FailOpenTakeOnce();
    RODAK_CHECK_FALSE(f.transport->PrepareInteraction());
    CheckSnapshot({Stage::kPrepareBegin,Stage::kExitNoOpen},{false,false},0);
}
RODAK_TEST("Cancellation after open acquisition observes released mutex at End") {
    Fixture f;
    RODAK_CHECK_FALSE(f.transport->PrepareInteraction([] {return false;}));
    CheckSnapshot({Stage::kPrepareBegin,Stage::kOpenAcquired,Stage::kOpenReleased},{false,true,false},0);
}
RODAK_TEST("Cancellation after closed-channel cleanup still releases before End") {
    Fixture f;unsigned checks=0;
    RODAK_CHECK_FALSE(f.transport->PrepareInteraction([&] {return ++checks<2;}));
    CheckSnapshot({Stage::kPrepareBegin,Stage::kOpenAcquired,Stage::kOpenReleased},{false,true,false},0);
}
RODAK_TEST("Fresh Cloud preparation is called exactly once and repeated scopes drain independently") {
    Fixture f;
    for(unsigned i=0;i<2;++i) {
        prepare_host::ClearTrace();
        RODAK_CHECK(f.transport->PrepareInteraction());CheckCloudReturn();
        RODAK_CHECK(trust_test::requests.empty());
    }
}
RODAK_TEST("Unconfigured real Cloud failure retains cloud-returned and unlocked End") {
    Fixture f(0,false);
    RODAK_CHECK_FALSE(f.transport->PrepareInteraction());CheckCloudReturn();
    RODAK_CHECK(trust_test::requests.empty());
}
RODAK_TEST("Real Cloud TLS-open failure runs one preparation and unlocked End") {
    Fixture f;f.cloud.InvalidateAccessTokenFreshness("new-token");
    trust_test::replies[trust_test::BootstrapUrl()].tls_ok=false;
    RODAK_CHECK_FALSE(f.transport->PrepareInteraction());CheckCloudReturn();
    RODAK_CHECK_EQ(trust_test::requests.size(),1U);
}
RODAK_TEST("Cloud success without voice configuration still observes released mutex") {
    Fixture f(0,true,false);
    RODAK_CHECK_FALSE(f.transport->PrepareInteraction());CheckCloudReturn();
}
RODAK_TEST("Cancellation after actual Cloud return does not repeat preparation") {
    Fixture f;bool allowed=true;
    prepare_host::after_cloud_return=[&] {allowed=false;};
    RODAK_CHECK_FALSE(f.transport->PrepareInteraction([&] {return allowed;}));CheckCloudReturn();
    RODAK_CHECK(trust_test::requests.empty());
}
