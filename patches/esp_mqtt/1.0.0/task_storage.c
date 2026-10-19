/* 任务栈是客户端最大的内部 RAM 块，大小也正是语音 WebSocket 任务所需。上游任务自删除后
 * 栈由 idle 任务稍后释放，替换客户端会在旧栈仍占用时把新栈放进最大空闲块，旧栈释放后
 * 那块就一直被切开。改为在其他对象之前预留栈、在 destroy 删除停住的任务后最后释放，
 * 替换客户端便从旧栈的位置分配。 */
static bool rodak_mqtt_reserve_task_storage(esp_mqtt_client_handle_t client, int stack_size)
{
    client->rodak_task_stack_size = stack_size > 0 ? stack_size : MQTT_TASK_STACK;
    client->rodak_task_stack = heap_caps_malloc(client->rodak_task_stack_size,
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_MEM_CHECK(TAG, client->rodak_task_stack, return false);
    client->rodak_task_buffer = heap_caps_calloc(1, sizeof(StaticTask_t),
                                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_MEM_CHECK(TAG, client->rodak_task_buffer, return false);
    return true;
}

/* 静态任务自删除后仍在自己的栈上运行，直到 idle 任务回收；因此任务退出时挂起自己，
 * 由其他任务在其挂起后删除。只有 run 已清零的任务才会挂起，由调用方保证。 */
static bool rodak_mqtt_delete_parked_task(esp_mqtt_client_handle_t client)
{
    if (client->task_handle == NULL) {
        return true;
    }
    if (client->task_handle == xTaskGetCurrentTaskHandle()) {
        return false;
    }
    while (eTaskGetState(client->task_handle) != eSuspended) {
        vTaskDelay(1);
    }
    vTaskDelete(client->task_handle);
    client->task_handle = NULL;
    return true;
}

static esp_err_t rodak_mqtt_create_task(esp_mqtt_client_handle_t client)
{
    /* init 之后 esp_mqtt_set_config() 可能调大栈；预留的栈大小固定，拒绝而不是越界。 */
    if (client->config->task_stack > client->rodak_task_stack_size) {
        ESP_LOGE(TAG, "MQTT task stack %d exceeds reserved %d", client->config->task_stack,
                 client->rodak_task_stack_size);
        return ESP_ERR_INVALID_SIZE;
    }
    /* STOPPED_BIT 之前旧任务可能还要拿这里持有的 API 锁，等它挂起会死锁。 */
    if (client->task_handle != NULL &&
        (xEventGroupGetBits(client->status_bits) & STOPPED_BIT) == 0) {
        ESP_LOGE(TAG, "Client has started");
        return ESP_FAIL;
    }
    if (!rodak_mqtt_delete_parked_task(client)) {
        return ESP_FAIL;
    }
    /* 在任务创建前置位，而不是由任务自己置位，任务首次运行前的 stop/destroy 才不会丢失。 */
    client->run = true;
    xEventGroupClearBits(client->status_bits, STOPPED_BIT);
#if MQTT_CORE_SELECTION_ENABLED
    ESP_LOGD(TAG, "Core selection enabled on %u", MQTT_TASK_CORE);
    client->task_handle = xTaskCreateStaticPinnedToCore(esp_mqtt_task, "mqtt_task",
                                                        client->rodak_task_stack_size, client,
                                                        client->config->task_prio,
                                                        client->rodak_task_stack,
                                                        client->rodak_task_buffer, MQTT_TASK_CORE);
#else
    ESP_LOGD(TAG, "Core selection disabled");
    client->task_handle = xTaskCreateStatic(esp_mqtt_task, "mqtt_task",
                                            client->rodak_task_stack_size, client,
                                            client->config->task_prio,
                                            client->rodak_task_stack, client->rodak_task_buffer);
#endif
    if (client->task_handle == NULL) {
        client->run = false;
        xEventGroupSetBits(client->status_bits, STOPPED_BIT);
        ESP_LOGE(TAG, "Error create mqtt task");
        return ESP_FAIL;
    }
    return ESP_OK;
}
