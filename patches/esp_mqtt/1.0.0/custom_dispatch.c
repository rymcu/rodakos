static void rodak_mqtt_reset_custom_events(esp_mqtt_client_handle_t client)
{
    xQueueReset(client->rodak_custom_events);
}

esp_err_t esp_mqtt_dispatch_custom_event(esp_mqtt_client_handle_t client, esp_mqtt_event_t *event)
{
    if (client == NULL || event == NULL || client->rodak_custom_events == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Native events synchronously post and run their own loop. An external
     * producer must not occupy that loop's sole slot before a lifecycle event. */
    if (xQueueSend(client->rodak_custom_events, event, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
