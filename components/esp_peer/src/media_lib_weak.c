/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "media_lib_os.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"

/**
 * @brief  This file provide weak realization of MOCK media_lib API
 *         So that it can run without media_lib
 */

#define WEAK __attribute__((weak))

// Mock memory allocation functions
void* WEAK media_lib_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void* WEAK media_lib_calloc(size_t nmemb, size_t size)
{
    return heap_caps_calloc(nmemb, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void* WEAK media_lib_realloc(void *ptr, size_t size)
{
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void WEAK media_lib_free(void *ptr) {
    heap_caps_free(ptr);
}

int WEAK media_lib_mutex_create(media_lib_mutex_handle_t *mutex)
{
    if (mutex == NULL) {
        return -1;
    }
    *mutex = (media_lib_mutex_handle_t)xSemaphoreCreateRecursiveMutex();
    return (*mutex ? 0 : -1);
}

int WEAK media_lib_mutex_destroy(media_lib_mutex_handle_t mutex)
{
    if (mutex == NULL) {
        return -1;
    }
    vSemaphoreDelete((QueueHandle_t)mutex);
    return 0;
}

int WEAK media_lib_mutex_lock(media_lib_mutex_handle_t mutex, uint32_t timeout)
{
    if (mutex == NULL) {
        return -1;
    }
    if (timeout != 0xFFFFFFFF) {
        timeout = pdMS_TO_TICKS(timeout);
    }
    return xSemaphoreTakeRecursive(mutex, timeout);
}

int WEAK media_lib_mutex_unlock(media_lib_mutex_handle_t mutex) {
    if (mutex == NULL) {
        return -1;
    }
    xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex);
    return 0;
}

void WEAK media_lib_thread_sleep(int ms)
{
   vTaskDelay(pdMS_TO_TICKS(ms));
}
