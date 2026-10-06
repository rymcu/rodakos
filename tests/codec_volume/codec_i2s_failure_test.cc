#include "test_framework.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_os.h"

#include <mutex>

struct FakeI2sChannel {
    int mode = I2S_COMM_MODE_STD;
    int direction = I2S_DIR_TX;
    bool dma_valid = true;
    bool enabled = false;
    bool fail_next_slot = false;
    int enable_calls = 0;
    int unsafe_enables = 0;
    int slot_calls = 0;
    int io_calls = 0;
};

namespace {
int SetSlot(FakeI2sChannel* channel) {
    ++channel->slot_calls;
    if (channel->fail_next_slot) {
        channel->fail_next_slot = false;
        channel->dma_valid = false;
        return ESP_ERR_NO_MEM;
    }
    // Model IDF's stale buf_size: a retry can report success with DMA still absent.
    return ESP_OK;
}

esp_codec_dev_sample_info_t Format() {
    esp_codec_dev_sample_info_t result{};
    result.sample_rate = 16000;
    result.bits_per_sample = 16;
    result.channel = 4;
    result.channel_mask = 15;
    result.mclk_multiple = 256;
    return result;
}

const audio_codec_data_if_t* Data(int port, FakeI2sChannel* tx, FakeI2sChannel* rx) {
    audio_codec_i2s_cfg_t config{};
    config.port = static_cast<uint8_t>(port);
    config.tx_handle = tx;
    config.rx_handle = rx;
    auto* result = audio_codec_new_i2s_data(&config);
    RODAK_CHECK(result != nullptr);
    return result;
}

esp_codec_dev_handle_t Codec(const audio_codec_data_if_t* data, esp_codec_dev_type_t type) {
    esp_codec_dev_cfg_t config{};
    config.dev_type = type;
    config.data_if = data;
    auto result = esp_codec_dev_new(&config);
    RODAK_CHECK(result != nullptr);
    return result;
}

struct SplitPair {
    FakeI2sChannel tx;
    FakeI2sChannel rx{I2S_COMM_MODE_TDM, I2S_DIR_RX};
    const audio_codec_data_if_t* output = Data(0, &tx, nullptr);
    const audio_codec_data_if_t* input = Data(0, nullptr, &rx);
    esp_codec_dev_handle_t adc = Codec(input, ESP_CODEC_DEV_TYPE_IN);
    ~SplitPair() {
        esp_codec_dev_delete(adc);
        audio_codec_delete_data_if(input);
        audio_codec_delete_data_if(output);
    }
};
}  // namespace

extern "C" {
esp_codec_dev_mutex_handle_t esp_codec_dev_mutex_create() { return new std::recursive_mutex; }
int esp_codec_dev_mutex_lock(esp_codec_dev_mutex_handle_t mutex, int) {
    static_cast<std::recursive_mutex*>(mutex)->lock(); return ESP_OK;
}
int esp_codec_dev_mutex_unlock(esp_codec_dev_mutex_handle_t mutex) {
    static_cast<std::recursive_mutex*>(mutex)->unlock(); return ESP_OK;
}
void esp_codec_dev_mutex_destroy(esp_codec_dev_mutex_handle_t mutex) {
    delete static_cast<std::recursive_mutex*>(mutex);
}
void esp_codec_dev_sleep(int) {}
esp_err_t i2s_channel_get_info(i2s_chan_handle_t channel, i2s_chan_info_t* info) {
    if (!channel) return ESP_ERR_INVALID_ARG;
    *info = {channel->mode, channel->direction, nullptr}; return ESP_OK;
}
esp_err_t i2s_channel_enable(i2s_chan_handle_t channel) {
    ++channel->enable_calls;
    if (!channel->dma_valid) { ++channel->unsafe_enables; return ESP_FAIL; }
    channel->enabled = true; return ESP_OK;
}
esp_err_t i2s_channel_disable(i2s_chan_handle_t channel) {
    channel->enabled = false; return ESP_OK;
}
esp_err_t i2s_channel_reconfig_std_slot(i2s_chan_handle_t channel, const i2s_std_slot_config_t*) {
    return SetSlot(channel);
}
esp_err_t i2s_channel_reconfig_tdm_slot(i2s_chan_handle_t channel, const i2s_tdm_slot_config_t*) {
    return SetSlot(channel);
}
esp_err_t i2s_channel_reconfig_std_clock(i2s_chan_handle_t, const i2s_std_clk_config_t*) { return ESP_OK; }
esp_err_t i2s_channel_reconfig_tdm_clock(i2s_chan_handle_t, const i2s_tdm_clk_config_t*) { return ESP_OK; }
esp_err_t i2s_channel_read(i2s_chan_handle_t channel, void*, size_t bytes, size_t* read, uint32_t) {
    ++channel->io_calls; *read = bytes; return channel->dma_valid ? ESP_OK : ESP_FAIL;
}
esp_err_t i2s_channel_write(i2s_chan_handle_t channel, const void*, size_t bytes, size_t* written, uint32_t) {
    ++channel->io_calls; *written = bytes; return channel->dma_valid ? ESP_OK : ESP_FAIL;
}
}

RODAK_TEST("I2S paired TX DMA failure is propagated and shared port remains blocked across retries") {
    SplitPair fixture;
    auto format = Format();
    RODAK_CHECK_EQ(esp_codec_dev_open(fixture.adc, &format), ESP_CODEC_DEV_OK);
    RODAK_CHECK(fixture.rx.enabled);
    RODAK_CHECK_EQ(esp_codec_dev_close(fixture.adc), ESP_CODEC_DEV_OK);
    fixture.tx.fail_next_slot = true;
    const int enables = fixture.tx.enable_calls;
    RODAK_CHECK_EQ(esp_codec_dev_open(fixture.adc, &format), ESP_CODEC_DEV_DRV_ERR);
    RODAK_CHECK_EQ(fixture.tx.enable_calls, enables);
    RODAK_CHECK_EQ(fixture.tx.unsafe_enables, 0);
    RODAK_CHECK(!fixture.rx.enabled && !fixture.tx.enabled);
    const int slots = fixture.tx.slot_calls + fixture.rx.slot_calls;
    for (int retry = 0; retry < 3; ++retry) {
        RODAK_CHECK_EQ(esp_codec_dev_open(fixture.adc, &format), ESP_CODEC_DEV_DRV_ERR);
        RODAK_CHECK_EQ(fixture.output->enable(fixture.output, ESP_CODEC_DEV_TYPE_OUT, true), ESP_CODEC_DEV_DRV_ERR);
        RODAK_CHECK_EQ(esp_codec_dev_close(fixture.adc), ESP_CODEC_DEV_OK);
    }
    RODAK_CHECK_EQ(fixture.tx.slot_calls + fixture.rx.slot_calls, slots);
    uint8_t sample[4]{};
    RODAK_CHECK_EQ(fixture.input->read(fixture.input, sample, 4), ESP_CODEC_DEV_DRV_ERR);
    RODAK_CHECK_EQ(fixture.output->write(fixture.output, sample, 4), ESP_CODEC_DEV_DRV_ERR);
    RODAK_CHECK_EQ(fixture.rx.io_calls + fixture.tx.io_calls, 0);
    // Recreating only a codec interface does not repair the underlying IDF channel.
    const auto* reopened = Data(0, &fixture.tx, nullptr);
    RODAK_CHECK_EQ(reopened->set_fmt(reopened, ESP_CODEC_DEV_TYPE_OUT, &format), ESP_CODEC_DEV_DRV_ERR);
    RODAK_CHECK_EQ(reopened->enable(reopened, ESP_CODEC_DEV_TYPE_OUT, true), ESP_CODEC_DEV_DRV_ERR);
    audio_codec_delete_data_if(reopened);
}

RODAK_TEST("I2S duplex format preserves first failure without configuring or enabling the second channel") {
    FakeI2sChannel tx;
    FakeI2sChannel rx{I2S_COMM_MODE_TDM, I2S_DIR_RX};
    const auto* data = Data(1, &tx, &rx);
    auto codec = Codec(data, ESP_CODEC_DEV_TYPE_IN_OUT);
    auto format = Format();
    tx.fail_next_slot = true;
    RODAK_CHECK_EQ(esp_codec_dev_open(codec, &format), ESP_CODEC_DEV_DRV_ERR);
    RODAK_CHECK_EQ(tx.slot_calls, 1);
    RODAK_CHECK_EQ(rx.slot_calls, 0);
    RODAK_CHECK_EQ(tx.enable_calls + rx.enable_calls, 0);
    RODAK_CHECK_EQ(data->enable(data, ESP_CODEC_DEV_TYPE_IN_OUT, true), ESP_CODEC_DEV_DRV_ERR);
    esp_codec_dev_delete(codec);
    audio_codec_delete_data_if(data);
}
