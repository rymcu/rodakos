#pragma once

#include <freertos/task.h>

inline BaseType_t xTaskCreateWithCaps(TaskFunction_t entry, const char* name,
                                     uint32_t stack, void* argument, UBaseType_t priority,
                                     TaskHandle_t* handle, uint32_t) {
    return xTaskCreate(entry, name, stack, argument, priority, handle);
}
inline BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t entry, const char* name,
                                                uint32_t stack, void* argument,
                                                UBaseType_t priority, TaskHandle_t* handle,
                                                BaseType_t, uint32_t caps) {
    return xTaskCreateWithCaps(entry, name, stack, argument, priority, handle, caps);
}
inline void vTaskDeleteWithCaps(TaskHandle_t handle) { vTaskDelete(handle); }
