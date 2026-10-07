#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef uint8_t StackType_t;
typedef uint32_t configSTACK_DEPTH_TYPE;
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
typedef struct { unsigned char storage[100]; } StaticTask_t;
typedef struct { pthread_mutex_t mutex; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { PTHREAD_MUTEX_INITIALIZER }
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY UINT32_MAX
#define tskNO_AFFINITY (-1)
#define pdMS_TO_TICKS(value) ((TickType_t)(value))
#define configSUPPORT_STATIC_ALLOCATION 1
#define configNUMBER_OF_CORES 2
#define configMINIMAL_STACK_SIZE 1536
#define configASSERT(value) do { if (!(value)) abort(); } while (0)
#ifdef __cplusplus
extern "C" {
#endif
void* pvPortMalloc(size_t bytes);
void vPortFree(void* pointer);
void retirement_host_enter_critical(portMUX_TYPE* mux);
void retirement_host_exit_critical(portMUX_TYPE* mux);
#ifdef __cplusplus
}
#endif
#define portENTER_CRITICAL(mux) retirement_host_enter_critical(mux)
#define portEXIT_CRITICAL(mux) retirement_host_exit_critical(mux)
