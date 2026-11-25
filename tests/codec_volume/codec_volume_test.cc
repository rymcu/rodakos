#include "test_framework.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include "esp_codec_dev.h"
#include "es7210_adc.h"
#include "es7210_reg.h"

namespace {

struct FakeCodec : audio_codec_if_t {
    bool ready = true;
    int volume_result = ESP_CODEC_DEV_OK;
    int volume_calls = 0;
    float last_db = 0;
    int format_result = ESP_CODEC_DEV_OK;
    int enable_result = ESP_CODEC_DEV_OK;
    bool enabled = false;
    int mic_gain_result = ESP_CODEC_DEV_OK;
    int mic_channel_gain_result = ESP_CODEC_DEV_OK;
    int mic_gain_calls = 0;
    int mic_channel_gain_calls = 0;
    float last_mic_db = 0;
    float last_mic_channel_db = 0;
    uint16_t last_mic_channel_mask = 0;

    FakeCodec() : audio_codec_if_t{} {
        is_open = [](const audio_codec_if_t* h) { return From(h).ready; };
        set_fs = [](const audio_codec_if_t* h, esp_codec_dev_sample_info_t*) { return From(h).format_result; };
        enable = [](const audio_codec_if_t* h, bool enabled) {
            auto& fake = From(h);
            if (enabled && fake.enable_result != ESP_CODEC_DEV_OK) return fake.enable_result;
            fake.enabled = enabled;
            return ESP_CODEC_DEV_OK;
        };
        set_vol = [](const audio_codec_if_t* h, float db) {
            auto& fake = From(h);
            ++fake.volume_calls;
            fake.last_db = db;
            return fake.volume_result;
        };
        mute = [](const audio_codec_if_t*, bool) { return ESP_CODEC_DEV_OK; };
        set_mic_gain = [](const audio_codec_if_t* h, float db) {
            auto& fake = From(h);
            ++fake.mic_gain_calls;
            fake.last_mic_db = db;
            return fake.mic_gain_result;
        };
        set_mic_channel_gain = [](const audio_codec_if_t* h, uint16_t mask, float db) {
            auto& fake = From(h);
            ++fake.mic_channel_gain_calls;
            fake.last_mic_channel_mask = mask;
            fake.last_mic_channel_db = db;
            return fake.mic_channel_gain_result;
        };
    }

    static FakeCodec& From(const audio_codec_if_t* h) {
        return *const_cast<FakeCodec*>(static_cast<const FakeCodec*>(h));
    }
};

struct FakeData : audio_codec_data_if_t {
    bool ready = true;
    std::vector<int16_t> samples;
    int format_result = ESP_CODEC_DEV_OK;
    int enable_result = ESP_CODEC_DEV_OK;
    int format_calls = 0;
    int enable_calls = 0;
    bool enabled = false;

    FakeData() : audio_codec_data_if_t{} {
        is_open = [](const audio_codec_data_if_t* h) { return From(h).ready; };
        set_fmt = [](const audio_codec_data_if_t* h, esp_codec_dev_type_t, esp_codec_dev_sample_info_t*) {
            auto& fake = From(h);
            ++fake.format_calls;
            return fake.format_result;
        };
        enable = [](const audio_codec_data_if_t* h, esp_codec_dev_type_t, bool enabled) {
            auto& fake = From(h);
            ++fake.enable_calls;
            if (enabled && fake.enable_result != ESP_CODEC_DEV_OK) return fake.enable_result;
            fake.enabled = enabled;
            return ESP_CODEC_DEV_OK;
        };
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
    int open_result = ESP_CODEC_DEV_OK;

    FakeSoftwareVolume() : audio_codec_vol_if_t{} {
        open = [](const audio_codec_vol_if_t* h, esp_codec_dev_sample_info_t*, int) {
            return From(h).open_result;
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

RODAK_TEST("Codec open returns format and enable errors without committing an opened state") {
    CodecFixture fixture;
    esp_codec_dev_sample_info_t format{};
    format.sample_rate = 16000; format.channel = 1; format.bits_per_sample = 16;
    fixture.data.format_result = ESP_ERR_NO_MEM;
    RODAK_CHECK_EQ(esp_codec_dev_open(fixture.device, &format), ESP_ERR_NO_MEM);
    RODAK_CHECK_EQ(fixture.data.enable_calls, 0);
    RODAK_CHECK(!fixture.codec.enabled);
    fixture.data.format_result = ESP_CODEC_DEV_OK;
    fixture.data.enable_result = -801;
    RODAK_CHECK_EQ(esp_codec_dev_open(fixture.device, &format), -801);
    RODAK_CHECK(!fixture.data.enabled && !fixture.codec.enabled);
    fixture.data.enable_result = ESP_CODEC_DEV_OK;
    fixture.Open();
    RODAK_CHECK_EQ(fixture.data.format_calls, 3);
    RODAK_CHECK(fixture.data.enabled && fixture.codec.enabled);
}

RODAK_TEST("Codec open rolls back data enable on codec format and enable failures") {
    CodecFixture fixture;
    esp_codec_dev_sample_info_t format{};
    format.sample_rate = 16000; format.channel = 1; format.bits_per_sample = 16;
    fixture.codec.format_result = -802;
    RODAK_CHECK_EQ(esp_codec_dev_open(fixture.device, &format), -802);
    RODAK_CHECK(!fixture.data.enabled && !fixture.codec.enabled);
    fixture.codec.format_result = ESP_CODEC_DEV_OK;
    fixture.codec.enable_result = -803;
    RODAK_CHECK_EQ(esp_codec_dev_open(fixture.device, &format), -803);
    RODAK_CHECK(!fixture.data.enabled && !fixture.codec.enabled);
    fixture.codec.enable_result = ESP_CODEC_DEV_OK;
    fixture.Open();
    RODAK_CHECK_EQ(fixture.data.format_calls, 3);
}

RODAK_TEST("Codec open rolls back software volume failure and permits a clean retry") {
    CodecFixture fixture;
    fixture.UseSoftwareVolume();
    fixture.software.open_result = -804;
    esp_codec_dev_sample_info_t format{};
    format.sample_rate = 16000; format.channel = 1; format.bits_per_sample = 16;
    RODAK_CHECK_EQ(esp_codec_dev_open(fixture.device, &format), -804);
    RODAK_CHECK(!fixture.data.enabled && !fixture.codec.enabled);
    RODAK_CHECK_EQ(fixture.software.closes, 1);
    fixture.software.open_result = ESP_CODEC_DEV_OK;
    fixture.Open();
    RODAK_CHECK_EQ(fixture.data.format_calls, 2);
}

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

RODAK_TEST("Codec input gain preserves fractional dB and propagates driver errors without committing cache") {
    CodecFixture fixture(true, ESP_CODEC_DEV_TYPE_IN);
    fixture.Open();
    fixture.codec.mic_gain_calls = 0;
    RODAK_CHECK_EQ(esp_codec_dev_set_in_gain(fixture.device, 34.5F), ESP_CODEC_DEV_OK);
    RODAK_CHECK(std::fabs(fixture.codec.last_mic_db - 34.5F) < 0.001F);
    float cached = 0.0F;
    RODAK_CHECK_EQ(esp_codec_dev_get_in_gain(fixture.device, &cached), ESP_CODEC_DEV_OK);
    RODAK_CHECK(std::fabs(cached - 34.5F) < 0.001F);

    fixture.codec.mic_gain_result = ESP_CODEC_DEV_WRITE_FAIL;
    RODAK_CHECK_EQ(esp_codec_dev_set_in_gain(fixture.device, 12.0F), ESP_CODEC_DEV_WRITE_FAIL);
    RODAK_CHECK_EQ(esp_codec_dev_get_in_gain(fixture.device, &cached), ESP_CODEC_DEV_OK);
    RODAK_CHECK(std::fabs(cached - 34.5F) < 0.001F);

    RODAK_CHECK_EQ(esp_codec_dev_set_in_channel_gain(fixture.device, 0x4, 37.5F), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(fixture.codec.last_mic_channel_mask, uint16_t{0x4});
    RODAK_CHECK(std::fabs(fixture.codec.last_mic_channel_db - 37.5F) < 0.001F);
    // Per-channel gain clears cached uniform mic_gain so reopen does not replay 34.5 dB across all channels.
    RODAK_CHECK_EQ(esp_codec_dev_get_in_gain(fixture.device, &cached), ESP_CODEC_DEV_OK);
    RODAK_CHECK(std::fabs(cached) < 0.001F);
    fixture.codec.mic_channel_gain_result = -777;
    RODAK_CHECK_EQ(esp_codec_dev_set_in_channel_gain(fixture.device, 0x4, 9.0F), -777);

    // Closing after uniform gain also clears cached mic_gain so reopen replays 0 dB instead of 30 dB.
    fixture.codec.mic_gain_result = ESP_CODEC_DEV_OK;
    RODAK_CHECK_EQ(esp_codec_dev_set_in_gain(fixture.device, 30.0F), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(esp_codec_dev_close(fixture.device), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(esp_codec_dev_get_in_gain(fixture.device, &cached), ESP_CODEC_DEV_OK);
    RODAK_CHECK(std::fabs(cached) < 0.001F);
    fixture.codec.last_mic_db = -1.0F;
    fixture.Open();
    RODAK_CHECK(std::fabs(fixture.codec.last_mic_db) < 0.001F);
}

RODAK_TEST("ES7210 masked register updates abort on I2C read failure instead of clearing mic enable bit") {
    struct FakeCtrl : audio_codec_ctrl_if_t {
        std::map<int, uint8_t> regs;
        int fail_read_reg = -1;
        int fail_write_reg = -1;
        int writes_after_fail = 0;

        FakeCtrl() : audio_codec_ctrl_if_t{} {
            is_open = [](const audio_codec_ctrl_if_t*) { return true; };
            read_reg = [](const audio_codec_ctrl_if_t* c, int reg, int, void* data, int) {
                auto& self = *const_cast<FakeCtrl*>(static_cast<const FakeCtrl*>(c));
                if (reg == self.fail_read_reg) return ESP_CODEC_DEV_READ_FAIL;
                auto it = self.regs.find(reg);
                *static_cast<uint8_t*>(data) = it != self.regs.end() ? it->second : 0;
                return ESP_CODEC_DEV_OK;
            };
            write_reg = [](const audio_codec_ctrl_if_t* c, int reg, int, void* data, int) {
                auto& self = *const_cast<FakeCtrl*>(static_cast<const FakeCtrl*>(c));
                if (reg == self.fail_write_reg) return ESP_CODEC_DEV_WRITE_FAIL;
                if (self.fail_read_reg >= 0) ++self.writes_after_fail;
                self.regs[reg] = static_cast<uint8_t>(*static_cast<const int*>(data));
                return ESP_CODEC_DEV_OK;
            };
        }
    } ctrl;

    es7210_codec_cfg_t cfg{};
    cfg.ctrl_if = &ctrl;
    cfg.mic_selected = ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2 | ES7210_INPUT_MIC3 | ES7210_INPUT_MIC4;
    const audio_codec_if_t* es7210 = es7210_codec_new(&cfg);
    RODAK_CHECK(es7210 != nullptr);
    // MIC3 enable bit (0x10) must be set after open.
    RODAK_CHECK_NE(ctrl.regs[ES7210_MIC3_GAIN_REG45] & 0x10, 0);
    const uint8_t before = ctrl.regs[ES7210_MIC3_GAIN_REG45];

    ctrl.fail_read_reg = ES7210_MIC3_GAIN_REG45;
    RODAK_CHECK_EQ(es7210->set_mic_channel_gain(es7210, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2), 10.0F),
                   ESP_CODEC_DEV_READ_FAIL);
    RODAK_CHECK_EQ(ctrl.writes_after_fail, 0);
    RODAK_CHECK_EQ(ctrl.regs[ES7210_MIC3_GAIN_REG45], before);
    RODAK_CHECK_NE(ctrl.regs[ES7210_MIC3_GAIN_REG45] & 0x10, 0);

    // Partial enable failure rolls back power registers through es7210_stop.
    ctrl.fail_read_reg = ES7210_MIC2_GAIN_REG44;
    RODAK_CHECK_NE(es7210->enable(es7210, true), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(ctrl.regs[ES7210_POWER_DOWN_REG06], uint8_t{0x07});
    RODAK_CHECK_EQ(ctrl.regs[ES7210_CLOCK_OFF_REG01], uint8_t{0x7f});

    // If rollback write fails during enable(true), a later enable(false) still executes es7210_stop.
    ctrl.fail_write_reg = ES7210_POWER_DOWN_REG06;
    RODAK_CHECK_NE(es7210->enable(es7210, true), ESP_CODEC_DEV_OK);
    ctrl.regs[ES7210_POWER_DOWN_REG06] = 0x00;
    ctrl.fail_read_reg = -1;
    ctrl.fail_write_reg = -1;
    RODAK_CHECK_EQ(es7210->enable(es7210, false), ESP_CODEC_DEV_OK);
    RODAK_CHECK_EQ(ctrl.regs[ES7210_POWER_DOWN_REG06], uint8_t{0x07});

    RODAK_CHECK_EQ(es7210->set_mic_channel_gain(es7210, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2), 10.0F),
                   ESP_CODEC_DEV_OK);
    RODAK_CHECK_NE(ctrl.regs[ES7210_MIC3_GAIN_REG45] & 0x10, 0);
    RODAK_CHECK_EQ(ctrl.regs[ES7210_MIC3_GAIN_REG45] & 0x0F, 3);
    audio_codec_delete_codec_if(es7210);
}
