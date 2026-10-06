int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t handle, int volume)
{
    codec_dev_t *dev = (codec_dev_t *) handle;
    if (dev == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    int ret = _verify_codec_setting(dev, true);
    if (ret != ESP_CODEC_DEV_OK) {
        return ret;
    }
    const audio_codec_if_t *codec = dev->codec_if;
    float db_value = _get_vol_db(&dev->vol_curve, volume);
    // Prefer to use software volume setting
    if (dev->sw_vol) {
        ret = dev->sw_vol->set_vol(dev->sw_vol, db_value);
    } else if (codec && codec->set_vol) {
        ret = codec->set_vol(codec, db_value);
    } else {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (ret == ESP_CODEC_DEV_OK) {
        dev->volume = volume;
    }
    return ret;
}
