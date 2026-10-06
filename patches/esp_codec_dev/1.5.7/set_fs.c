/* Derived from esp_codec_dev 1.5.7, Apache-2.0. RodakOS error propagation. */
static int set_fs(i2s_data_t *i2s_data, bool playback, bool skip)
{
    if (_i2s_format_faulted(i2s_data)) {
        return ESP_CODEC_DEV_DRV_ERR;
    }
    i2s_chan_handle_t channel = (i2s_chan_handle_t)(playback ? i2s_data->out_handle : i2s_data->in_handle);
    esp_codec_dev_sample_info_t *fs = playback ? &i2s_data->out_fs : &i2s_data->in_fs;
    uint8_t bits_per_sample = get_bits(i2s_data, playback);
    int ret = set_drv_fs(channel, playback, bits_per_sample, fs);
    if (ret != ESP_CODEC_DEV_OK) {
        _i2s_latch_format_fault(i2s_data);
        return ret;
    }
    i2s_data_t *paired = get_paired(i2s_data, playback);
    if (!skip && !playback && paired && paired->out_handle != NULL && !paired->out_enable) {
        channel = (i2s_chan_handle_t)paired->out_handle;
        (void)_i2s_drv_enable(paired, true, false);
        ret = set_drv_fs(channel, true, bits_per_sample, fs);
        if (ret != ESP_CODEC_DEV_OK) {
            _i2s_latch_format_fault(i2s_data);
            return ret;
        }
        ret = _i2s_drv_enable(paired, true, true);
    }
    return ret;
}
