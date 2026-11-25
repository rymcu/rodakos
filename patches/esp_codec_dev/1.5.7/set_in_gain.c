/* Derived from esp_codec_dev 1.5.7, Apache-2.0. RodakOS input gain error propagation. */
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t handle, float db)
{
    codec_dev_t *dev = (codec_dev_t *) handle;
    if (dev == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, false);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    const audio_codec_if_t *codec = dev->codec_if;
    if (codec && codec->set_mic_gain) {
        ret = codec->set_mic_gain(codec, db);
        if (ret == ESP_CODEC_DEV_OK) {
            dev->mic_gain = db;
        }
        return ret;
    }
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

int esp_codec_dev_set_in_channel_gain(esp_codec_dev_handle_t handle, uint16_t channel_mask, float db)
{
    codec_dev_t *dev = (codec_dev_t *) handle;
    if (dev == NULL || channel_mask == 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, false);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    const audio_codec_if_t *codec = dev->codec_if;
    if (codec && codec->set_mic_channel_gain) {
        ret = codec->set_mic_channel_gain(codec, channel_mask, db);
        if (ret == ESP_CODEC_DEV_OK) {
            // Per-channel gains supersede any prior uniform gain so reopen never replays it across all channels.
            dev->mic_gain = 0.0f;
        }
        return ret;
    }
    return ESP_CODEC_DEV_NOT_SUPPORT;
}
