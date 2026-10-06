/* Derived from esp_codec_dev 1.5.7, Apache-2.0. RodakOS open error propagation. */
int esp_codec_dev_open(esp_codec_dev_handle_t handle, esp_codec_dev_sample_info_t *fs)
{
    codec_dev_t *dev = (codec_dev_t *)handle;
    if (dev == NULL || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    if (dev->input_opened || dev->output_opened) {
        return ESP_CODEC_DEV_OK;
    }
    bool input = (dev->dev_caps & ESP_CODEC_DEV_TYPE_IN) && _verify_drv_ready(dev, false);
    bool output = (dev->dev_caps & ESP_CODEC_DEV_TYPE_OUT) && _verify_drv_ready(dev, true);
    if (!input && !output) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    const audio_codec_if_t *codec = dev->codec_if;
    const audio_codec_data_if_t *data_if = dev->data_if;
    int ret = ESP_CODEC_DEV_OK;
    bool data_enabled = false;
    bool codec_enabled = false;
    if (data_if->set_fmt) {
        ret = data_if->set_fmt(data_if, dev->dev_caps, fs);
        if (ret != ESP_CODEC_DEV_OK) {
            return ret;
        }
    }
    if (data_if->enable) {
        ret = data_if->enable(data_if, dev->dev_caps, true);
        if (ret != ESP_CODEC_DEV_OK) {
            (void)data_if->enable(data_if, dev->dev_caps, false);
            return ret;
        }
        data_enabled = true;
    }
    if (codec && codec->set_fs) {
        ret = codec->set_fs(codec, fs);
        if (ret != ESP_CODEC_DEV_OK) {
            goto failed;
        }
    }
    if (codec && codec->enable) {
        ret = codec->enable(codec, true);
        if (ret != ESP_CODEC_DEV_OK) {
            (void)codec->enable(codec, false);
            goto failed;
        }
        codec_enabled = true;
    }
    if (output) {
        if ((codec == NULL || codec->set_vol == NULL) && dev->sw_vol == NULL) {
            dev->sw_vol = audio_codec_new_sw_vol();
            if (dev->sw_vol == NULL) {
                ret = ESP_CODEC_DEV_NO_MEM;
                goto failed;
            }
            dev->sw_vol_alloced = true;
        }
        if (dev->sw_vol) {
            ret = dev->sw_vol->open(dev->sw_vol, fs, VOL_TRANSITION_TIME);
            if (ret != ESP_CODEC_DEV_OK) {
                (void)dev->sw_vol->close(dev->sw_vol);
                goto failed;
            }
        }
    }
    dev->input_opened = input;
    dev->output_opened = output;
    _update_codec_setting(dev);
    ESP_LOGI(TAG, "Open codec device OK");
    return ESP_CODEC_DEV_OK;

failed:
    if (codec_enabled) {
        (void)codec->enable(codec, false);
    }
    if (data_enabled) {
        (void)data_if->enable(data_if, dev->dev_caps, false);
    }
    return ret;
}
