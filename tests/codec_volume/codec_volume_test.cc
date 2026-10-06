#include "test_framework.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "esp_codec_dev.h"

namespace {

struct FakeCodec : audio_codec_if_t {
    bool ready = true;
    int volume_result = ESP_CODEC_DEV_OK;
    int volume_calls = 0;
    float last_db = 0;

    FakeCodec() : audio_codec_if_t{} {
        is_open = [](const audio_codec_if_t* h) { return From(h).ready; };
        set_vol = [](const audio_codec_if_t* h, float db) {
            auto& fake = From(h);
            ++fake.volume_calls;
            fake.last_db = db;
            return fake.volume_result;
        };
        mute = [](const audio_codec_if_t*, bool) { return ESP_CODEC_DEV_OK; };
    }

    static FakeCodec& From(const audio_codec_if_t* h) {
        return *const_cast<FakeCodec*>(static_cast<const FakeCodec*>(h));
    }
};

struct FakeData : audio_codec_data_if_t {
    bool ready = true;
    std::vector<int16_t> samples;

    FakeData() : audio_codec_data_if_t{} {
        is_open = [](const audio_codec_data_if_t* h) { return From(h).ready; };
        read = [](const audio_codec_data_if_t*, uint8_t*, int) { return ESP_CODEC_DEV_OK; };
        write = [](const audio_codec_data_if_t* h, uint8_t* bytes, int size) {
            auto& fake = From(h);
            auto* pcm = reinterpret_cast<int16_t*>(bytes);
            fake.samples.assign(pcm, pcm + size / static_cast<int>(sizeof(int16_t)));
            return ESP_CODEC_DEV_OK;
        };
    }

    static FakeData& From(const audio_codec_data_if_t* h) {
        return *const_cast<FakeData*>(static_cast<const FakeData*>(h));
    }
};

struct FakeSoftwareVolume : audio_codec_vol_if_t {
    int volume_result = ESP_CODEC_DEV_OK;
    int volume_calls = 0;
    int closes = 0;
    float last_db = 0;

    FakeSoftwareVolume() : audio_codec_vol_if_t{} {
        open = [](const audio_codec_vol_if_t*, esp_codec_dev_sample_info_t*, int) {
            return ESP_CODEC_DEV_OK;
        };
        set_vol = [](const audio_codec_vol_if_t* h, float db) {
            auto& fake = From(h);
            ++fake.volume_calls;
            fake.last_db = db;
            return fake.volume_result;
        };
        process = [](const audio_codec_vol_if_t*, uint8_t*, int, uint8_t*, int) {
            return ESP_CODEC_DEV_OK;
        };
        close = [](const audio_codec_vol_if_t* h) {
            ++From(h).closes;
            return ESP_CODEC_DEV_OK;
        };
    }

    static FakeSoftwareVolume& From(const audio_codec_vol_if_t* h) {
        return *const_cast<FakeSoftwareVolume*>(static_cast<const FakeSoftwareVolume*>(h));
    }
};

struct CodecFixture {
    FakeCodec codec;
    FakeData data;
    FakeSoftwareVolume software;
    esp_codec_dev_handle_t device = nullptr;

    explicit CodecFixture(bool with_codec = true,
                          esp_codec_dev_type_t type = ESP_CODEC_DEV_TYPE_OUT) {
        esp_codec_dev_cfg_t config{};
        config.dev_type = type;
        config.codec_if = with_codec ? &codec : nullptr;
        config.data_if = &data;
        device = esp_codec_dev_new(&config);
        RODAK_CHECK(device != nullptr);
    }

    ~CodecFixture() { esp_codec_dev_delete(device); }
    CodecFixture(const CodecFixture&) = delete;
    CodecFixture& operator=(const CodecFixture&) = delete;

    void Open() {
        esp_codec_dev_sample_info_t format{};
        format.sample_rate = 16000;
        format.channel = 1;
        format.bits_per_sample = 16;
        RODAK_CHECK_EQ(esp_codec_dev_open(device, &format), ESP_CODEC_DEV_OK);
        codec.volume_calls = 0;
        software.volume_calls = 0;
    }

    int Volume() {
        int value = -1;
        RODAK_CHECK_EQ(esp_codec_dev_get_out_vol(device, &value), ESP_CODEC_DEV_OK);
        return value;
    }

    void UseSoftwareVolume() {
        RODAK_CHECK_EQ(esp_codec_dev_set_vol_handler(device, &software), ESP_CODEC_DEV_OK);
    }
};

}  // namespace

RODAK_TEST("Codec hardware success commits volume and uses the public volume curve") {
    CodecFixture fixture;
    fixture.Open();
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 20), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(fixture.Volume(), 20);
    RODAK_CHECK_EQ(fixture.codec.volume_calls, 1);
    RODAK_CHECK(std::fabs(fixture.codec.last_db - (-40.0F)) < 0.001F);
}

RODAK_TEST("Codec hardware error is returned unchanged and retains the last accepted volume") {
    CodecFixture fixture;
    fixture.Open();
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 20), ESP_CODEC_DEV_OK);
    fixture.codec.volume_result = ESP_ERR_TIMEOUT;
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 90), ESP_ERR_TIMEOUT);
    RODAK_CHECK_EQ(fixture.Volume(), 20);
    RODAK_CHECK_EQ(fixture.codec.volume_calls, 2);
}

RODAK_TEST("Codec hardware volume can retry after a negative driver failure") {
    CodecFixture fixture;
    fixture.Open();
    fixture.codec.volume_result = -731;
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 60), -731);
    RODAK_CHECK_EQ(fixture.Volume(), 0);
    fixture.codec.volume_result = ESP_CODEC_DEV_OK;
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 60), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(fixture.Volume(), 60);
    RODAK_CHECK_EQ(fixture.codec.volume_calls, 2);
}

RODAK_TEST("Codec prefers an explicit software volume handler over hardware") {
    CodecFixture fixture;
    fixture.UseSoftwareVolume();
    fixture.Open();
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 40), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(fixture.Volume(), 40);
    RODAK_CHECK_EQ(fixture.software.volume_calls, 1);
    RODAK_CHECK_EQ(fixture.codec.volume_calls, 0);
    RODAK_CHECK(std::fabs(fixture.software.last_db - (-30.0F)) < 0.001F);
}

RODAK_TEST("Codec software volume failure retains cache without falling back to hardware") {
    CodecFixture fixture;
    fixture.UseSoftwareVolume();
    fixture.Open();
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 40), ESP_CODEC_DEV_OK);
    fixture.software.volume_result = ESP_CODEC_DEV_WRITE_FAIL;
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 90), ESP_CODEC_DEV_WRITE_FAIL);
    RODAK_CHECK_EQ(fixture.Volume(), 40);
    RODAK_CHECK_EQ(fixture.software.volume_calls, 2);
    RODAK_CHECK_EQ(fixture.codec.volume_calls, 0);
}

RODAK_TEST("Codec software volume can retry after an exact negative driver error") {
    CodecFixture fixture;
    fixture.UseSoftwareVolume();
    fixture.Open();
    fixture.software.volume_result = -912;
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 70), -912);
    RODAK_CHECK_EQ(fixture.Volume(), 0);
    fixture.software.volume_result = ESP_CODEC_DEV_OK;
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 70), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(fixture.Volume(), 70);
    RODAK_CHECK_EQ(fixture.codec.volume_calls, 0);
}

RODAK_TEST("Codec volume rejects null handles and null handler arguments") {
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(nullptr, 50), ESP_CODEC_DEV_INVALID_ARG);
    int unchanged = 73;
    RODAK_CHECK_EQ(esp_codec_dev_get_out_vol(nullptr, &unchanged), ESP_CODEC_DEV_INVALID_ARG);
    RODAK_CHECK_EQ(unchanged, 73);
    CodecFixture fixture;
    RODAK_CHECK_EQ(esp_codec_dev_set_vol_handler(fixture.device, nullptr), ESP_CODEC_DEV_INVALID_ARG);
    RODAK_CHECK_EQ(esp_codec_dev_set_vol_handler(nullptr, &fixture.software), ESP_CODEC_DEV_INVALID_ARG);
    RODAK_CHECK_EQ(fixture.Volume(), 0);
    RODAK_CHECK_EQ(fixture.codec.volume_calls, 0);
}

RODAK_TEST("Codec input-only devices reject output volume without calling a setter") {
    CodecFixture fixture(true, ESP_CODEC_DEV_TYPE_IN);
    fixture.Open();
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 50), ESP_CODEC_DEV_NOT_SUPPORT);
    int unchanged = 81;
    RODAK_CHECK_EQ(esp_codec_dev_get_out_vol(fixture.device, &unchanged), ESP_CODEC_DEV_NOT_SUPPORT);
    RODAK_CHECK_EQ(unchanged, 81);
    RODAK_CHECK_EQ(fixture.codec.volume_calls, 0);
}

RODAK_TEST("Codec unready state rejects volume before calling either setter") {
    CodecFixture fixture;
    fixture.UseSoftwareVolume();
    fixture.Open();
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 30), ESP_CODEC_DEV_OK);
    fixture.codec.ready = false;
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 80), ESP_CODEC_DEV_WRONG_STATE);
    RODAK_CHECK_EQ(fixture.software.volume_calls, 1);
    RODAK_CHECK_EQ(fixture.codec.volume_calls, 0);
    fixture.codec.ready = true;
    RODAK_CHECK_EQ(fixture.Volume(), 30);
}

RODAK_TEST("Codec with no volume setter retains cache when returning unsupported") {
    CodecFixture fixture;
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 25), ESP_CODEC_DEV_OK);
    fixture.codec.set_vol = nullptr;
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 80), ESP_CODEC_DEV_NOT_SUPPORT);
    RODAK_CHECK_EQ(fixture.Volume(), 25);
    RODAK_CHECK_EQ(fixture.codec.volume_calls, 1);
}

RODAK_TEST("Codec without hardware rejects pre-open volume without changing its cache") {
    CodecFixture fixture(false);
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 80), ESP_CODEC_DEV_NOT_SUPPORT);
    RODAK_CHECK_EQ(fixture.Volume(), 0);
}

RODAK_TEST("Codec without hardware automatically creates real software volume and changes PCM") {
    CodecFixture fixture(false);
    fixture.Open();
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 100), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(fixture.Volume(), 100);
    std::vector<int16_t> pcm(1024, 16000);
    RODAK_CHECK_EQ(esp_codec_dev_write(fixture.device, pcm.data(),
                                     static_cast<int>(pcm.size() * sizeof(int16_t))), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(fixture.data.samples.back(), 16000);
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 0), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(fixture.Volume(), 0);
    pcm.assign(1024, 16000);
    RODAK_CHECK_EQ(esp_codec_dev_write(fixture.device, pcm.data(),
                                     static_cast<int>(pcm.size() * sizeof(int16_t))), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(fixture.data.samples.back(), 0);
}

RODAK_TEST("Real software volume accepts closed configuration and remains usable after reopen") {
    CodecFixture fixture(false);
    fixture.Open();
    RODAK_CHECK_EQ(esp_codec_dev_close(fixture.device), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 100), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(fixture.Volume(), 100);
    fixture.Open();
    std::vector<int16_t> pcm(32, 12000);
    RODAK_CHECK_EQ(esp_codec_dev_write(fixture.device, pcm.data(),
                                     static_cast<int>(pcm.size() * sizeof(int16_t))), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(fixture.data.samples.front(), 12000);
    RODAK_CHECK_EQ(fixture.data.samples.back(), 12000);
}

RODAK_TEST("Codec volume preserves custom curve ranges rather than imposing UI limits") {
    CodecFixture fixture;
    fixture.Open();
    esp_codec_dev_vol_map_t entries[] = {{0, -50.0F}, {200, 0.0F}};
    esp_codec_dev_vol_curve_t curve{};
    curve.vol_map = entries;
    curve.count = 2;
    RODAK_CHECK_EQ(esp_codec_dev_set_vol_curve(fixture.device, &curve), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(esp_codec_dev_set_out_vol(fixture.device, 140), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(fixture.Volume(), 140);
    RODAK_CHECK(std::fabs(fixture.codec.last_db - (-15.0F)) < 0.001F);
}
