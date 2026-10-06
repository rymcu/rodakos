/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * RodakOS: fail closed after a potentially destructive DMA reconfiguration.
 */
static atomic_bool i2s_format_faults[I2S_LL_GET(INST_NUM)];

static bool _i2s_format_faulted(const i2s_data_t *data)
{
    return data->port >= I2S_LL_GET(INST_NUM) || atomic_load(&i2s_format_faults[data->port]);
}

static void _i2s_latch_format_fault(i2s_data_t *data)
{
    if (data->port < I2S_LL_GET(INST_NUM)) {
        atomic_store(&i2s_format_faults[data->port], true);
    }
    /* IDF 6.0.2 may free DMA descriptors without restoring buf_size on OOM.
       Retrying that format or only reopening the codec can enable a NULL DMA
       chain. The physical port stays unavailable until a system restart. */
    for (i2s_data_keep_t *item = i2s_data_list; item != NULL; item = item->next) {
        i2s_data_t *peer = item->i2s_data;
        if (peer->port != data->port) {
            continue;
        }
        if (peer->out_handle != NULL) {
            (void)i2s_channel_disable((i2s_chan_handle_t)peer->out_handle);
        }
        if (peer->in_handle != NULL) {
            (void)i2s_channel_disable((i2s_chan_handle_t)peer->in_handle);
        }
        peer->out_enable = peer->in_enable = false;
        peer->out_disable_pending = peer->in_disable_pending = false;
    }
    ESP_LOGE(TAG, "I2S port %u format failed; audio requires restart", data->port);
}
