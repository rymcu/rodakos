#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
// Only host platform glue; all NVS allocation, page, GC, read/write and CRC code is upstream.
#define ESP_ERR_FLASH_OP_FAIL 0x6002
#define ESP_ERR_FLASH_OP_TIMEOUT 0x6001
#ifdef __cplusplus
#include <new>
#endif
