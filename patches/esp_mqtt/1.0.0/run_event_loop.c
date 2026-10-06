static inline void run_event_loop(esp_mqtt_client_handle_t client)
{
    esp_mqtt_event_t event;
    if (xQueueReceive(client->rodak_custom_events, &event, 0) != pdTRUE) {
        return;
    }
    /* The MQTT task owns its recursive API lock. Native dispatch always
     * consumes its event before returning, so its queue is empty here.
     * Consume exactly one custom wakeup per task iteration; nested native
     * events can still post and run while the custom callback is executing. */
    esp_err_t ret = esp_event_post_to(client->config->event_loop_handle,
                                     MQTT_EVENTS, MQTT_USER_EVENT, &event, sizeof(event), 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Deferring custom MQTT event: native dispatch unavailable (%d)", ret);
        /* Rodak custom events are coalesced wakeups. Preserve the wakeup on
         * allocation failure. If a producer filled the slot meanwhile, that
         * replacement wakeup will drain the same bounded service queues. */
        xQueueSendToFront(client->rodak_custom_events, &event, 0);
        return;
    }
    ret = esp_event_loop_run(client->config->event_loop_handle, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error in running event_loop %d", ret);
    }
}
