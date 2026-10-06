#include "test_framework.h"
#include "host_runtime.h"
#include "rodakos_adapters/wifi_adapter.h"
#include "rodakos_adapters/wifi_connection_policy.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace wifi_host;
namespace {
struct Fixture {
    std::unique_ptr<WiFiAdapter> wifi;
    std::vector<WiFiStatus> callbacks;
    Fixture() { Reset(); wifi.reset(CreateWiFiAdapter()); RODAK_CHECK(wifi->Init()); }
    ~Fixture() { wifi.reset(); }
    void Start(const std::string& ssid = "known-ap") {
        RODAK_CHECK(wifi->Connect(ssid, "test-only-password", [this](WiFiStatus status) { callbacks.push_back(status); }));
    }
    void Establish(const std::string& ssid = "known-ap") {
        Start(ssid); Connected(ssid); GotIP();
        RODAK_CHECK_EQ(wifi->GetStatus(), WiFiStatus::kConnected);
    }
};
struct Gate {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false, released = false;
    void Block() {
        std::unique_lock<std::mutex> lock(mutex); entered = true; condition.notify_all();
        condition.wait(lock, [this]() { return released; });
    }
    void Wait() { std::unique_lock<std::mutex> lock(mutex); condition.wait(lock, [this]() { return entered; }); }
    void Release() { std::lock_guard<std::mutex> lock(mutex); released = true; condition.notify_all(); }
};
}

RODAK_TEST("WiFi recovery backs off through a long AP outage and resumes the same configuration") {
    Fixture f; f.Establish();
    RODAK_CHECK_EQ(f.callbacks.size(), 1u);
    Disconnected("known-ap");
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnecting);
    RODAK_CHECK(f.wifi->GetIPAddress().empty());
    for (uint32_t attempt = 0; attempt < 12; ++attempt) {
        int before = ConnectCalls();
        uint32_t delay = WiFiRecoveryBackoffMs(attempt);
        Advance(delay - 1); RODAK_CHECK_EQ(ConnectCalls(), before);
        Advance(1); RODAK_CHECK_EQ(ConnectCalls(), before + 1);
        RODAK_CHECK_EQ(ConfiguredSSID(), "known-ap");
        if (attempt < 11) Disconnected("known-ap");
    }
    Connected("known-ap"); GotIP(0xc0000202);
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
    RODAK_CHECK_EQ(f.wifi->GetIPAddress(), "192.0.2.2");
    RODAK_CHECK_EQ(f.callbacks.size(), 2u);
    RODAK_CHECK_EQ(DriverOverlaps(), 0);
    int before = ConnectCalls(); Advance(300000); RODAK_CHECK_EQ(ConnectCalls(), before);
}

RODAK_TEST("WiFi recovery does not depend on a user callback") {
    Fixture f;
    RODAK_CHECK(f.wifi->Connect("known-ap", "test-only", {}));
    Connected("known-ap"); GotIP(); Disconnected("known-ap"); Advance(1000);
    RODAK_CHECK_EQ(ConnectCalls(), 2);
    Connected("known-ap"); GotIP(); RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
}

RODAK_TEST("WiFi initial bad credentials stop after the finite retry budget") {
    Fixture f; f.Start();
    for (int i = 0; i < 4; ++i) Disconnected("known-ap");
    RODAK_CHECK_EQ(ConnectCalls(), 4);
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kFailed);
    RODAK_CHECK_EQ(f.callbacks.size(), 1u);
    RODAK_CHECK_EQ(f.callbacks[0], WiFiStatus::kFailed);
    Advance(300000); Disconnected("known-ap");
    RODAK_CHECK_EQ(ConnectCalls(), 4);
}

RODAK_TEST("WiFi manual disconnect cancels an already queued recovery tick and late IP") {
    Fixture f; f.Establish(); Disconnected("known-ap"); Advance(1000, false);
    RODAK_CHECK(f.wifi->DisconnectAndWait(5));
    Drain(); Connected("known-ap"); GotIP(); Disconnected("known-ap"); Advance(300000);
    RODAK_CHECK_EQ(ConnectCalls(), 1);
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kDisconnected);
    RODAK_CHECK(f.wifi->GetIPAddress().empty());
}

RODAK_TEST("WiFi changing SSID cancels old ticks and does not inherit persistent recovery eligibility") {
    Fixture f; f.Establish(); Disconnected("known-ap"); Advance(1000, false);
    f.Start("new-ap"); Drain(); Disconnected("known-ap"); Connected("known-ap"); GotIP();
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnecting);
    RODAK_CHECK(f.wifi->GetIPAddress().empty());
    RODAK_CHECK_EQ(ConnectCalls(), 2);
    for (int i = 0; i < 4; ++i) Disconnected("new-ap");
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kFailed);
    int before = ConnectCalls(); Advance(300000); RODAK_CHECK_EQ(ConnectCalls(), before);
}

RODAK_TEST("WiFi recovery watchdog waits for confirmed disconnect before another attempt") {
    Fixture f; f.Establish(); Disconnected("known-ap"); Advance(1000);
    SetDisconnectResult(ESP_OK);
    Advance(20000); RODAK_CHECK_EQ(ConnectCalls(), 2);
    for (int i = 0; i < 3; ++i) { Advance(1000); RODAK_CHECK_EQ(ConnectCalls(), 2); }
    Connected("known-ap"); GotIP();
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnecting);
    Disconnected("known-ap");
    Advance(1999); RODAK_CHECK_EQ(ConnectCalls(), 2);
    Advance(1); RODAK_CHECK_EQ(ConnectCalls(), 3);
    Connected("known-ap"); GotIP(); RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
    SetDisconnectResult(ESP_ERR_WIFI_NOT_CONNECT);
}

RODAK_TEST("WiFi DHCP loss has a watchdog even without station disconnect events") {
    Fixture f; f.Establish(); LostIP(); Advance(19999);
    RODAK_CHECK_EQ(ConnectCalls(), 1); Advance(1); Advance(1000);
    RODAK_CHECK_EQ(ConnectCalls(), 2);
    Connected("known-ap"); GotIP(); RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
}

RODAK_TEST("WiFi late disconnect for the same SSID cannot tear down the recovered live link") {
    Fixture f; f.Establish(); Disconnected("known-ap"); Advance(1000);
    Connected("known-ap"); GotIP(); Disconnected("known-ap", true); Advance(30000);
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
    RODAK_CHECK_EQ(ConnectCalls(), 2);
}

RODAK_TEST("WiFi failed connect API and full event queue retain bounded recovery") {
    Fixture f; f.Establish(); Disconnected("known-ap");
    SetEventPostFailure(true); Advance(5000); RODAK_CHECK_EQ(ConnectCalls(), 1);
    SetEventPostFailure(false); SetConnectResult(ESP_FAIL); Advance(1000);
    RODAK_CHECK_EQ(ConnectCalls(), 2);
    Advance(1999); RODAK_CHECK_EQ(ConnectCalls(), 2); Advance(1); RODAK_CHECK_EQ(ConnectCalls(), 3);
    SetConnectResult(ESP_OK); Advance(4000); Connected("known-ap"); GotIP();
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
}

RODAK_TEST("WiFi driver owner serializes a manual disconnect against an active retry") {
    Fixture f; f.Establish(); Disconnected("known-ap");
    Gate gate; BeforeConnect([&]() { gate.Block(); });
    std::thread retry([]() { Advance(1000); }); gate.Wait();
    std::atomic<bool> done{false};
    std::thread stop([&]() { f.wifi->Disconnect(); done = true; });
    gate.Release(); retry.join(); stop.join(); BeforeConnect({});
    RODAK_CHECK(done); RODAK_CHECK_EQ(DriverOverlaps(), 0);
    int before = ConnectCalls(); Advance(30000); RODAK_CHECK_EQ(ConnectCalls(), before);
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kDisconnected);
}

RODAK_TEST("WiFi deinit drains a timer callback already selected by the daemon before destruction") {
    Fixture f; f.Establish(); Disconnected("known-ap");
    Gate gate; BeforeTimer([&]() { gate.Block(); });
    std::thread timer([]() { Advance(1000, false); }); gate.Wait();
    std::atomic<bool> done{false};
    std::thread shutdown([&]() { f.wifi->Deinit(); done = true; });
    while (!TimerDeletionRequested()) std::this_thread::yield();
    RODAK_CHECK_FALSE(done); gate.Release(); timer.join(); shutdown.join(); BeforeTimer({});
    RODAK_CHECK(done); RODAK_CHECK_EQ(LiveTimers(), 0); Drain();
    RODAK_CHECK_EQ(ConnectCalls(), 1); RODAK_CHECK_EQ(DriverOverlaps(), 0);
    f.wifi.reset(); Advance(30000);
}

RODAK_TEST("WiFi deinit and reinit reject old recovery events") {
    Fixture f; f.Establish(); Disconnected("known-ap"); Advance(1000, false);
    f.wifi->Deinit(); RODAK_CHECK_EQ(LiveTimers(), 0);
    RODAK_CHECK(f.wifi->Init()); f.Start("new-ap"); Drain();
    RODAK_CHECK_EQ(ConnectCalls(), 2);
    RODAK_CHECK_EQ(ConfiguredSSID(), "new-ap");
    Connected("new-ap"); GotIP(); RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
}

RODAK_TEST("WiFi recovery backoff cannot overflow") {
    RODAK_CHECK_EQ(WiFiRecoveryBackoffMs(0), 1000u);
    RODAK_CHECK_EQ(WiFiRecoveryBackoffMs(4), 16000u);
    RODAK_CHECK_EQ(WiFiRecoveryBackoffMs(5), 30000u);
    RODAK_CHECK_EQ(WiFiRecoveryBackoffMs(UINT32_MAX), 30000u);
}

RODAK_TEST("WiFi stale association and GOT IP cannot complete an unassociated retry") {
    Fixture f; f.Establish(); Disconnected("known-ap"); Advance(1000);
    StaleConnected("known-ap"); StaleGotIP(0xc0000201);
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnecting);
    RODAK_CHECK(f.wifi->GetIPAddress().empty());
    Connected("known-ap"); GotIP(0xc0000202); StaleGotIP(0xc0000201);
    RODAK_CHECK_EQ(f.wifi->GetIPAddress(), "192.0.2.2");
    RODAK_CHECK_EQ(f.callbacks.size(), 2u);
}

RODAK_TEST("WiFi unknown driver state uses the stop barrier before retrying") {
    Fixture f; f.Establish(); SetAPInfoResult(ESP_FAIL); SetDisconnectResult(ESP_FAIL);
    Disconnected("known-ap");
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnecting);
    Advance(30000); RODAK_CHECK_EQ(ConnectCalls(), 1);
    SetAPInfoResult(ESP_OK); SetDisconnectResult(ESP_ERR_WIFI_NOT_CONNECT);
    Advance(1000); RODAK_CHECK_EQ(ConnectCalls(), 1);
    Advance(1000); RODAK_CHECK_EQ(ConnectCalls(), 2);
    Connected("known-ap"); GotIP(); RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
}

RODAK_TEST("WiFi manual disconnect timeout leaves recovery cancelled until its barrier settles") {
    Fixture f; f.Establish(); Disconnected("known-ap"); Advance(1000, false);
    SetDisconnectResult(ESP_OK);
    RODAK_CHECK_FALSE(f.wifi->DisconnectAndWait(1));
    Drain(); Connected("known-ap"); GotIP(); Disconnected("known-ap", true);
    Advance(30000); RODAK_CHECK_EQ(ConnectCalls(), 1);
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kDisconnected);
    SetDisconnectResult(ESP_ERR_WIFI_NOT_CONNECT);
    RODAK_CHECK(f.wifi->DisconnectAndWait(1));
    f.Start("new-ap"); RODAK_CHECK_EQ(ConnectCalls(), 2);
    Connected("new-ap"); GotIP(); RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
}

RODAK_TEST("WiFi explicit same SSID connection also revokes the previous generation") {
    Fixture f; f.Establish(); Disconnected("known-ap"); Advance(1000, false);
    f.Start("known-ap"); Drain();
    for (int i = 0; i < 4; ++i) Disconnected("known-ap");
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kFailed);
    int before = ConnectCalls(); Advance(300000); RODAK_CHECK_EQ(ConnectCalls(), before);
}

RODAK_TEST("WiFi live AP verification preserves full length and raw SSID octets") {
    for (const std::string& ssid : {std::string(32, 'x'), std::string("raw\0ssid", 8),
                                  std::string({static_cast<char>(0x80), static_cast<char>(0xff), 'a'})}) {
        Fixture f; f.Establish(ssid); Disconnected(ssid); Advance(1000);
        Connected(ssid); GotIP();
        RODAK_CHECK_EQ(f.wifi->GetConnectedSSID(), ssid);
        RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
    }
}

RODAK_TEST("WiFi delayed LOST IP cannot arm a watchdog while current live IP is healthy") {
    Fixture f; f.Establish(); GotIP(0xc0000202);
    StaleLostIP(); Advance(30000);
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
    RODAK_CHECK_EQ(f.wifi->GetIPAddress(), "192.0.2.2");
    RODAK_CHECK_EQ(ConnectCalls(), 1);
    RODAK_CHECK_EQ(DisconnectCalls(), 0);
}

RODAK_TEST("WiFi foreign or missing LOST IP netif is rejected before querying the SDK") {
    Fixture f; f.Establish();
    int queries = IPInfoCalls();
    ForeignLostIP(); MissingLostIP(); Advance(30000);
    RODAK_CHECK_EQ(IPInfoCalls(), queries);
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
    RODAK_CHECK_EQ(f.wifi->GetIPAddress(), "192.0.2.1");
    RODAK_CHECK_EQ(ConnectCalls(), 1);
    RODAK_CHECK_EQ(DisconnectCalls(), 0);
}

RODAK_TEST("WiFi failed live IP query is not accepted as proof of IP loss") {
    Fixture f; f.Establish(); SetIPInfoResult(ESP_FAIL);
    LostIP(); Advance(30000);
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
    RODAK_CHECK_EQ(DisconnectCalls(), 0);
    SetIPInfoResult(ESP_OK); StaleLostIP();
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnecting);
    RODAK_CHECK(f.wifi->GetIPAddress().empty());
    Advance(20000); Advance(1000);
    RODAK_CHECK_EQ(ConnectCalls(), 2);
}

RODAK_TEST("WiFi valid DHCP renewal cancels a LOST IP watchdog and rejects its delayed duplicates") {
    Fixture f; f.Establish(); LostIP();
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnecting);
    Advance(19000); GotIP(0xc0000202); StaleLostIP(); Advance(30000);
    RODAK_CHECK_EQ(f.wifi->GetStatus(), WiFiStatus::kConnected);
    RODAK_CHECK_EQ(f.wifi->GetIPAddress(), "192.0.2.2");
    RODAK_CHECK_EQ(ConnectCalls(), 1);
    RODAK_CHECK_EQ(DisconnectCalls(), 0);
}
