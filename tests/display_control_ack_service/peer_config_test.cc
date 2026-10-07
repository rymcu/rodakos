#include "test_framework.h"
#include "host_runtime.h"
#include "phone_os/camera_service.h"
#include "phone_os/display_service.h"
#include "phone_os/webrtc_camera_service.h"
#include "phone_os/webrtc_display_service.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {
namespace host = rodakos_test::display_host;

struct Fixture {
    rodakos::CameraService camera;
    rodakos::DisplayService display;
    rodakos::WebRtcCameraService camera_peer{&camera};
    rodakos::WebRtcDisplayService display_peer{&display};
    uint64_t nonce = 0;

    Fixture() { host::Reset(); }
    ~Fixture() { Stop(); }
    bool Start(bool camera_kind, std::function<void(esp_peer_state_t)> callback = {}) {
        if (camera_kind) return camera_peer.Start({}, [](auto, auto&&) {}, std::move(callback));
        rodakos::WebRtcDisplayService::Config config;
        config.stream_lease = std::make_shared<rodakos::StreamLease>(1, 1, ++nonce, "same-session");
        return display_peer.Start(config, [](auto, auto&&) {}, std::move(callback));
    }
    void Stop() {
        camera_peer.Stop();
        display_peer.Stop();
        host::JoinTasks();
    }
    bool Signal(bool camera_kind, esp_peer_msg_type_t type, const std::string& value) {
        const auto* data = reinterpret_cast<const uint8_t*>(value.data());
        return camera_kind ? camera_peer.HandleRemoteMessage(type, data, value.size())
                           : display_peer.HandleRemoteMessage(type, data, value.size());
    }
    bool Running(bool camera_kind) {
        return camera_kind ? camera_peer.IsRunning() : display_peer.IsRunning();
    }
    bool MediaRunning(bool camera_kind) {
        return camera_kind ? camera.preview_running || camera.jpeg_running
                           : display.capture_running || display.jpeg_running;
    }
};

void CheckConfig(const host::OpenAttempt& attempt) {
    RODAK_CHECK(attempt.default_abi_valid);
    RODAK_CHECK_EQ(attempt.config.extra_size, sizeof(esp_peer_default_cfg_t));
    RODAK_CHECK_EQ(attempt.defaults.max_candidates, 32u);
    RODAK_CHECK_EQ(attempt.defaults.agent_recv_timeout, 50u);
    RODAK_CHECK_EQ(attempt.defaults.data_ch_cfg.send_cache_size, 400u * 1024u);
    RODAK_CHECK_EQ(attempt.defaults.data_ch_cfg.recv_cache_size, 400u * 1024u);
    RODAK_CHECK_FALSE(attempt.defaults.ipv6_support);
    RODAK_CHECK_FALSE(attempt.defaults.tcp_support);
    RODAK_CHECK_FALSE(attempt.defaults.insecure_skip_turn_cert_verify);
    RODAK_CHECK_FALSE(attempt.defaults.twcc_cfg.enable);
    RODAK_CHECK(attempt.config.enable_data_channel);
    RODAK_CHECK(attempt.config.manual_ch_create);
    RODAK_CHECK(attempt.config.no_auto_reconnect);
}

std::string FindLog(const std::string& text) {
    const auto logs = host::CapturedLogs();
    const auto found = std::find_if(logs.begin(), logs.end(), [&](const auto& log) {
        return log.find(text) != std::string::npos;
    });
    RODAK_CHECK(found != logs.end());
    return *found;
}

void CheckNoMemoryRecovery(bool camera_kind) {
    Fixture fixture;
    host::SetLogCapture(true);
    host::SetOpenResult(ESP_PEER_ERR_NO_MEM);
    RODAK_CHECK_FALSE(fixture.Start(camera_kind));
    RODAK_CHECK_FALSE(fixture.Running(camera_kind));
    RODAK_CHECK_FALSE(fixture.MediaRunning(camera_kind));
    RODAK_CHECK_EQ(host::LatestPeer(), nullptr);
    auto attempts = host::OpenAttempts();
    RODAK_CHECK_EQ(attempts.size(), 1u);
    CheckConfig(attempts.front());
    RODAK_CHECK(FindLog("phase=open-failed").find("result=-2") != std::string::npos);
    FindLog("phase=start-failed-cleanup");
    fixture.Stop();
    host::SetOpenResult(ESP_PEER_ERR_NONE);
    RODAK_CHECK(fixture.Start(camera_kind));
    fixture.Stop();
    attempts = host::OpenAttempts();
    RODAK_CHECK_EQ(attempts.size(), 2u);
    CheckConfig(attempts.back());
    FindLog("phase=open-before generation=2");
    FindLog("phase=stopped generation=2");
}

void CheckSignals(bool camera_kind) {
    Fixture fixture;
    RODAK_CHECK(fixture.Start(camera_kind));
    const auto peer = host::LatestPeer();
    const std::string sdp = "v=0\r\na=candidate:included-in-sdp 1 udp 1 192.0.2.1 5000 typ host\r\n";
    RODAK_CHECK(fixture.Signal(camera_kind, ESP_PEER_MSG_TYPE_SDP, sdp));
    for (int i = 0; i < 34; ++i) {
        const std::string candidate = i % 2 == 0 ? "candidate:duplicate 1 udp 1 192.0.2.1 5000 typ host"
                                               : "candidate:distinct " + std::to_string(i);
        RODAK_CHECK(fixture.Signal(camera_kind, ESP_PEER_MSG_TYPE_CANDIDATE, candidate));
    }
    host::SetSignalResult(ESP_PEER_ERR_INVALID_ARG);
    RODAK_CHECK_FALSE(fixture.Signal(camera_kind, ESP_PEER_MSG_TYPE_CANDIDATE, "invalid"));
    const auto signals = host::SentSignals();
    RODAK_CHECK_EQ(signals.size(), 36u);
    RODAK_CHECK_EQ(signals[0].type, ESP_PEER_MSG_TYPE_SDP);
    RODAK_CHECK_EQ(signals[0].payload, sdp);
    for (size_t i = 0; i < signals.size(); ++i) {
        RODAK_CHECK_EQ(signals[i].peer, peer);
        RODAK_CHECK(signals[i].nul_terminated);
        if (i > 0 && i <= 34) {
            const auto candidate_index = i - 1;
            RODAK_CHECK_EQ(signals[i].type, ESP_PEER_MSG_TYPE_CANDIDATE);
            RODAK_CHECK_EQ(signals[i].payload, candidate_index % 2 == 0 ?
                "candidate:duplicate 1 udp 1 192.0.2.1 5000 typ host" :
                "candidate:distinct " + std::to_string(candidate_index));
        }
    }
    RODAK_CHECK_EQ(signals.back().payload, "invalid");
    fixture.Stop();
    RODAK_CHECK_FALSE(fixture.Signal(camera_kind, ESP_PEER_MSG_TYPE_CANDIDATE, "after-stop"));
    RODAK_CHECK_EQ(host::SentSignals().size(), 36u);
}

void CheckResources(bool camera_kind) {
    Fixture fixture;
    host::SetLogCapture(true);
    host::SetHeapValues({20000, 10000, 18000, 9000, 2000000, 1000000});
    RODAK_CHECK(fixture.Start(camera_kind));
    FindLog("phase=open-before");
    FindLog("phase=open-after");
    host::SetHeapValues({19000, 8000, 16000, 7000, 1900000, 900000});
    for (int i = 0; i < 18; ++i) {
        RODAK_CHECK(fixture.Signal(camera_kind, ESP_PEER_MSG_TYPE_CANDIDATE, "same-candidate"));
    }
    const auto low = FindLog("candidate_calls=18");
    RODAK_CHECK(low.find("internal_free=19000 internal_largest=8000 dma_free=16000 dma_largest=7000 psram_free=1900000 psram_largest=900000") != std::string::npos);
    host::SetHeapValues({21000, 11000, 19000, 10000, 2100000, 1100000});
    host::EmitState(host::LatestPeer(), ESP_PEER_STATE_DATA_CHANNEL_CONNECTED);
    FindLog("phase=connected");
    fixture.Stop();
    const auto stopped = FindLog("phase=stopped");
    RODAK_CHECK(stopped.find("internal_free=21000 internal_largest=11000 dma_free=19000 dma_largest=10000") != std::string::npos);
    RODAK_CHECK(stopped.find("sampled_min_internal_free=19000 sampled_min_internal_largest=8000 sampled_min_dma_free=16000 sampled_min_dma_largest=7000 sampled_min_psram_free=1900000 sampled_min_psram_largest=900000") != std::string::npos);
    RODAK_CHECK(fixture.Start(camera_kind));
    const auto restarted = FindLog("phase=open-before generation=2");
    RODAK_CHECK(restarted.find("candidate_calls=0") != std::string::npos);
    RODAK_CHECK(restarted.find("sampled_min_internal_free=21000") != std::string::npos);
    fixture.Stop();
}
}  // namespace

RODAK_TEST("camera uses explicit SDK candidate capacity through real extra config ABI") {
    Fixture fixture;
    RODAK_CHECK(fixture.Start(true));
    CheckConfig(host::OpenAttempts().back());
    fixture.Stop();
}
RODAK_TEST("display uses explicit SDK candidate capacity through real extra config ABI") {
    Fixture fixture;
    RODAK_CHECK(fixture.Start(false));
    CheckConfig(host::OpenAttempts().back());
    fixture.Stop();
}
RODAK_TEST("camera candidate capacity open NO_MEM releases preview and permits retry") { CheckNoMemoryRecovery(true); }
RODAK_TEST("display candidate capacity open NO_MEM releases capture and permits retry") { CheckNoMemoryRecovery(false); }
RODAK_TEST("camera forwards SDP and all repeated candidates beyond 32 with original SDK result") { CheckSignals(true); }
RODAK_TEST("display forwards SDP and all repeated candidates beyond 32 with original SDK result") { CheckSignals(false); }
RODAK_TEST("camera samples all resource heaps and resets sampled minima on restart") { CheckResources(true); }
RODAK_TEST("display samples all resource heaps and resets sampled minima on restart") { CheckResources(false); }

RODAK_TEST("camera and display cleanup later startup failures before retry") {
    for (bool camera_kind : {false, true}) {
        for (int failure = 0; failure < 3; ++failure) {
            Fixture fixture;
            if (failure == 0) host::SetConnectionResult(ESP_PEER_ERR_NO_MEM);
            if (failure == 1) host::SetTaskCreationAllowed(false);
            if (failure == 2) {
                fixture.camera.jpeg_allowed = false;
                fixture.display.jpeg_allowed = false;
            }
            RODAK_CHECK_FALSE(fixture.Start(camera_kind));
            RODAK_CHECK_FALSE(fixture.Running(camera_kind));
            RODAK_CHECK_FALSE(fixture.MediaRunning(camera_kind));
            RODAK_CHECK(host::IsClosed(host::LatestPeer()));
            host::SetConnectionResult(ESP_PEER_ERR_NONE);
            host::SetTaskCreationAllowed(true);
            fixture.camera.jpeg_allowed = fixture.display.jpeg_allowed = true;
            RODAK_CHECK(fixture.Start(camera_kind));
            CheckConfig(host::OpenAttempts().back());
            fixture.Stop();
        }
    }
}

RODAK_TEST("camera and display terminal callbacks remain with original stopped lifecycle") {
    for (bool camera_kind : {false, true}) {
        Fixture fixture;
        host::SetCloseStateCallback(true);
        size_t first_terminal = 0;
        size_t second_terminal = 0;
        RODAK_CHECK(fixture.Start(camera_kind, [&](auto state) {
            RODAK_CHECK_EQ(state, ESP_PEER_STATE_CLOSED);
            ++first_terminal;
        }));
        const auto old_peer = host::LatestPeer();
        fixture.Stop();
        RODAK_CHECK_EQ(first_terminal, 1u);
        RODAK_CHECK(host::IsClosed(old_peer));
        RODAK_CHECK(fixture.Start(camera_kind, [&](auto state) {
            RODAK_CHECK_EQ(state, ESP_PEER_STATE_CLOSED);
            ++second_terminal;
        }));
        RODAK_CHECK(host::LatestPeer() != old_peer);
        RODAK_CHECK_EQ(second_terminal, 0u);
        fixture.Stop();
        RODAK_CHECK_EQ(first_terminal, 1u);
        RODAK_CHECK_EQ(second_terminal, 1u);
    }
}
