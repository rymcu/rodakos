#include "test_framework.h"
#include "phone_os/server_trust.h"
#include "nvs_storage.hpp"
#include <cJSON.h>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr size_t kPageSize = 4096;
constexpr size_t kPageCount = 6;
constexpr uint8_t kNamespace = 1;
constexpr char kSentry[] = "synthetic-public-preserved-payload";

class RamPartition : public nvs::Partition {
public:
    std::vector<uint8_t> bytes = std::vector<uint8_t>(kPageSize * kPageCount, 0xff);
    size_t writes = 0, erases = 0;
    const char* get_partition_name() override { return "synthetic"; }
    uint32_t get_address() override { return 0; }
    uint32_t get_size() override { return static_cast<uint32_t>(bytes.size()); }
    bool get_readonly() override { return false; }
    esp_err_t read_raw(size_t offset, void* data, size_t length) override {
        if (offset > bytes.size() || length > bytes.size() - offset) return ESP_ERR_INVALID_SIZE;
        memcpy(data, bytes.data() + offset, length); return ESP_OK;
    }
    esp_err_t read(size_t offset, void* data, size_t length) override { return read_raw(offset, data, length); }
    esp_err_t write_raw(size_t offset, const void* data, size_t length) override {
        if (offset > bytes.size() || length > bytes.size() - offset) return ESP_ERR_INVALID_SIZE;
        auto* input = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < length; ++i)
            if ((bytes[offset + i] & input[i]) != input[i]) return ESP_ERR_FLASH_OP_FAIL;
        for (size_t i = 0; i < length; ++i) bytes[offset + i] &= input[i];
        ++writes; return ESP_OK;
    }
    esp_err_t write(size_t offset, const void* data, size_t length) override { return write_raw(offset, data, length); }
    esp_err_t erase_range(size_t offset, size_t length) override {
        if (offset % kPageSize || length % kPageSize || offset > bytes.size() ||
            length > bytes.size() - offset) return ESP_ERR_INVALID_SIZE;
        memset(bytes.data() + offset, 0xff, length); ++erases; return ESP_OK;
    }
};

size_t Entries(const std::string& value) { return 1 + (value.size() + 1 + 31) / 32; }
std::string Padding(size_t entries) { return std::string((entries - 1) * 32 - 1, 'x'); }

rodakos::ServerAuthority Authority() {
    rodakos::ServerTrust trust;
    // Existing public, synthetic certificate fixture; no private key or device dump is consumed.
    trust.server_id = "2d71611acf59fb91c453de3fa25796cd9879047f92f9d4af43e2d365bec2d22b";
    trust.tls_name = "rodak-2d71611acf59fb91.local";
    std::ifstream certificate(PUBLIC_TEST_CA_PATH);
    trust.ca_pem.assign(std::istreambuf_iterator<char>(certificate), {});
    std::string error;
    RODAK_CHECK(rodakos::ValidateServerTrust(trust, error));
    rodakos::ServerAuthority authority;
    const std::string path = "/api/v1/aiot/devices/bootstrap";
    authority.active = {"https://" + trust.tls_name + ":9443" + path, trust, true, "192.0.2.10"};
    authority.pending = {"https://" + trust.tls_name + ":9444" + path, trust, true, "192.0.2.11"};
    return authority;
}

std::string LegacyV2(const rodakos::ServerAuthority& authority) {
    auto endpoint = [](const rodakos::ServerEndpoint& value) -> cJSON* {
        if (value.trust.empty()) return cJSON_CreateNull();
        auto* result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "bootstrap_url", value.bootstrap_url.c_str());
        cJSON_AddBoolToObject(result, "requires_bound_identity", value.requires_bound_identity);
        cJSON_AddStringToObject(result, "connect_address", value.connect_address.c_str());
        auto* trust = cJSON_AddObjectToObject(result, "trust");
        cJSON_AddNumberToObject(trust, "version", 1);
        cJSON_AddStringToObject(trust, "server_id", value.trust.server_id.c_str());
        cJSON_AddStringToObject(trust, "tls_name", value.trust.tls_name.c_str());
        cJSON_AddStringToObject(trust, "ca_pem", value.trust.ca_pem.c_str());
        return result;
    };
    auto* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON_AddItemToObject(root, "active", endpoint(authority.active));
    cJSON_AddItemToObject(root, "pending", endpoint(authority.pending));
    char* encoded = cJSON_PrintUnformatted(root);
    RODAK_CHECK(encoded != nullptr);
    std::string result(encoded); cJSON_free(encoded); cJSON_Delete(root);
    rodakos::ServerAuthority decoded;
    std::string error;
    RODAK_CHECK(rodakos::DecodeServerAuthority(result, decoded, error));
    return result;
}

std::string Read(nvs::Storage& storage, const char* key) {
    size_t size = 0;
    RODAK_CHECK_EQ(storage.getItemDataSize(kNamespace, nvs::ItemType::SZ, key, size), ESP_OK);
    std::string value(size, '\0');
    RODAK_CHECK_EQ(storage.readItem(kNamespace, nvs::ItemType::SZ, key, value.data(), size), ESP_OK);
    RODAK_CHECK(!value.empty() && value.back() == '\0'); value.pop_back(); return value;
}

esp_err_t Write(nvs::Storage& storage, const char* key, const std::string& value) {
    return storage.writeItem(kNamespace, nvs::ItemType::SZ, key, value.c_str(), value.size() + 1, false);
}

struct Fixture {
    RamPartition partition;
    rodakos::ServerAuthority authority = Authority();
    std::string old_record, legacy, compact;
    std::unique_ptr<nvs::Storage> storage;
    size_t maximum_compacted_free = 0;

    Fixture() {
        legacy = LegacyV2(authority);
        RODAK_CHECK(rodakos::EncodeServerAuthority(authority, compact));
        auto previous = authority; previous.pending = {};
        old_record = LegacyV2(previous);
        maximum_compacted_free = Entries(legacy) - 1;
        RODAK_CHECK(Entries(compact) < maximum_compacted_free);
        const size_t used = nvs::Page::ENTRY_COUNT - maximum_compacted_free;
        // Build five valid pages using real Page writes/erases, plus one GC reserve.
        // Every page has erased holes and a short tail. Even the best compacted
        // page is one entry short of the legacy candidate, despite ample total free.
        for (size_t index = 0; index < kPageCount - 1; ++index) {
            nvs::Page page;
            RODAK_CHECK_EQ(page.load(&partition, index), ESP_OK);
            RODAK_CHECK_EQ(page.setSeqNumber(index), ESP_OK);
            if (index == 0) {
                const uint8_t value = kNamespace;
                RODAK_CHECK_EQ(page.writeItem(0, nvs::ItemType::U8, "fixture", &value, sizeof(value)), ESP_OK);
                RODAK_CHECK_EQ(page.writeItem(kNamespace, nvs::ItemType::SZ, "authority", old_record.c_str(), old_record.size() + 1), ESP_OK);
                RODAK_CHECK_EQ(page.writeItem(kNamespace, nvs::ItemType::SZ, "sentry", kSentry, sizeof(kSentry)), ESP_OK);
            }
            const auto filler = Padding(used - page.getUsedEntryCount());
            const std::string name = "fill" + std::to_string(index);
            RODAK_CHECK_EQ(page.writeItem(kNamespace, nvs::ItemType::SZ, name.c_str(), filler.c_str(), filler.size() + 1), ESP_OK);
            const auto dead = Padding(10);
            RODAK_CHECK_EQ(page.writeItem(kNamespace, nvs::ItemType::SZ, "erased_hole", dead.c_str(), dead.size() + 1), ESP_OK);
            RODAK_CHECK_EQ(page.eraseItem(kNamespace, nvs::ItemType::SZ, "erased_hole", false), ESP_OK);
            RODAK_CHECK_EQ(page.getUsedEntryCount(), used);
            RODAK_CHECK_EQ(page.getErasedEntryCount(), 10u);
            if (index < kPageCount - 2) RODAK_CHECK_EQ(page.markFull(), ESP_OK);
        }
        Reload();
        std::cout << "synthetic_record_bytes legacy=" << legacy.size() << " compact=" << compact.size()
                  << " entries legacy=" << Entries(legacy) << " compact=" << Entries(compact)
                  << " largest_compacted_free=" << maximum_compacted_free << '\n';
    }
    void Reload() {
        storage.reset();
        storage.reset(new (std::nothrow) nvs::Storage(&partition));
        RODAK_CHECK(storage != nullptr);
        RODAK_CHECK_EQ(storage->init(0, kPageCount), ESP_OK);
        uint8_t ns = 0;
        RODAK_CHECK_EQ(storage->createOrOpenNamespace("fixture", false, ns), ESP_OK);
        RODAK_CHECK_EQ(ns, kNamespace);
    }
    void PreserveSentry() { RODAK_CHECK_EQ(Read(*storage, "sentry"), kSentry); }
    void CheckDecoded(const std::string& record) {
        rodakos::ServerAuthority decoded; std::string error;
        RODAK_CHECK(rodakos::DecodeServerAuthority(record, decoded, error));
        RODAK_CHECK_EQ(decoded.active.bootstrap_url, authority.active.bootstrap_url);
        RODAK_CHECK_EQ(decoded.pending.bootstrap_url, authority.pending.bootstrap_url);
        RODAK_CHECK_EQ(decoded.active.connect_address, authority.active.connect_address);
        RODAK_CHECK_EQ(decoded.pending.connect_address, authority.pending.connect_address);
        RODAK_CHECK_EQ(decoded.active.requires_bound_identity, authority.active.requires_bound_identity);
        RODAK_CHECK_EQ(decoded.pending.requires_bound_identity, authority.pending.requires_bound_identity);
        RODAK_CHECK(rodakos::SameServerTrust(decoded.active.trust, authority.active.trust));
        RODAK_CHECK(rodakos::SameServerTrust(decoded.pending.trust, authority.pending.trust));
    }
};
}

RODAK_TEST("Real NVS equal authority writes do not consume flash entries") {
    Fixture f;
    auto writes = f.partition.writes, erases = f.partition.erases;
    RODAK_CHECK_EQ(Write(*f.storage, "authority", f.old_record), ESP_OK);
    RODAK_CHECK_EQ(f.partition.writes, writes); RODAK_CHECK_EQ(f.partition.erases, erases);
    f.PreserveSentry();
}

RODAK_TEST("Real NVS rejects duplicate CA authority despite enough total free entries and retains old data") {
    Fixture f; nvs_stats_t stats{};
    RODAK_CHECK_EQ(f.storage->fillStats(stats), ESP_OK);
    RODAK_CHECK(stats.available_entries > Entries(f.legacy));
    auto erases = f.partition.erases;
    RODAK_CHECK_EQ(Write(*f.storage, "authority", f.legacy), ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    RODAK_CHECK_EQ(f.partition.erases, erases + 1);
    RODAK_CHECK_EQ(Read(*f.storage, "authority"), f.old_record); f.PreserveSentry();
    f.Reload();
    RODAK_CHECK_EQ(Read(*f.storage, "authority"), f.old_record); f.PreserveSentry();
}

RODAK_TEST("Production compact authority succeeds in the identical synthetic fragmented layout") {
    Fixture f;
    RODAK_CHECK_EQ(Write(*f.storage, "authority", f.compact), ESP_OK);
    RODAK_CHECK_EQ(Read(*f.storage, "authority"), f.compact); f.PreserveSentry();
    f.Reload();
    RODAK_CHECK_EQ(Read(*f.storage, "authority"), f.compact);
    f.CheckDecoded(Read(*f.storage, "authority")); f.PreserveSentry();
}

RODAK_TEST("Production compact authority succeeds after the actual legacy NVS allocation failure") {
    Fixture f;
    RODAK_CHECK_EQ(Write(*f.storage, "authority", f.legacy), ESP_ERR_NVS_NOT_ENOUGH_SPACE);
    RODAK_CHECK_EQ(Read(*f.storage, "authority"), f.old_record);
    RODAK_CHECK_EQ(Write(*f.storage, "authority", f.compact), ESP_OK);
    f.Reload(); f.CheckDecoded(Read(*f.storage, "authority")); f.PreserveSentry();
}

RODAK_TEST("Production compact pending promotion and route migrations survive repeated NVS GC and reload") {
    Fixture f;
    const auto erases_before = f.partition.erases;
    for (int index = 0; index < 30; ++index) {
        f.authority.pending = f.authority.active;
        f.authority.pending.connect_address = index % 2 ? "192.0.2.12" : "192.0.2.13";
        f.authority.pending.bootstrap_url = "https://" + f.authority.active.trust.tls_name +
            (index % 2 ? ":9444" : ":9445") + "/api/v1/aiot/devices/bootstrap";
        std::string encoded; RODAK_CHECK(rodakos::EncodeServerAuthority(f.authority, encoded));
        RODAK_CHECK_EQ(Write(*f.storage, "authority", encoded), ESP_OK);
        f.Reload(); f.CheckDecoded(Read(*f.storage, "authority")); f.PreserveSentry();
        f.authority.active = f.authority.pending;
        f.authority.pending = {};
        RODAK_CHECK(rodakos::EncodeServerAuthority(f.authority, encoded));
        RODAK_CHECK_EQ(Write(*f.storage, "authority", encoded), ESP_OK);
        f.Reload(); f.CheckDecoded(Read(*f.storage, "authority")); f.PreserveSentry();
    }
    RODAK_CHECK(f.partition.erases > erases_before);
}

RODAK_TEST("Real NVS string limit includes NUL and an oversized write preserves the original") {
    RamPartition partition; nvs::Storage storage(&partition);
    RODAK_CHECK_EQ(storage.init(0, kPageCount), ESP_OK);
    uint8_t ns = 0;
    RODAK_CHECK_EQ(storage.createOrOpenNamespace("fixture", true, ns), ESP_OK);
    RODAK_CHECK_EQ(ns, kNamespace);
    const std::string allowed(3999, 'x'), oversized(4000, 'y');
    RODAK_CHECK_EQ(Write(storage, "boundary", allowed), ESP_OK);
    RODAK_CHECK_EQ(Write(storage, "boundary", oversized), ESP_ERR_NVS_VALUE_TOO_LONG);
    RODAK_CHECK_EQ(Read(storage, "boundary"), allowed);
}
