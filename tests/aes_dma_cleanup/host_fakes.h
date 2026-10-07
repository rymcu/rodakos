#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aes/esp_aes.h>

#define CONFIG_SPIRAM 1
#define SOC_PSRAM_DMA_CAPABLE 1
#define SOC_AES_SUPPORT_DMA 1
#define SOC_AES_GDMA 1
#define SOC_AHB_GDMA_VERSION 1
#define CONFIG_MBEDTLS_AES_HW_SMALL_DATA_LEN_OPTIM 1
#define CONFIG_MBEDTLS_AES_USE_INTERRUPT 1
#define CONFIG_MBEDTLS_AES_INTERRUPT_LEVEL 0
#define IRAM_ATTR
#define DRAM_ATTR
#define AES_BLOCK_BYTES 16
#define AES_128_KEY_BYTES 16
#define AES_256_KEY_BYTES 32
#define PSA_ERROR_NOT_SUPPORTED -134
#define ESP_AES_ENCRYPT 1
#define ESP_AES_DECRYPT 0
#define ESP_AES_BLOCK_MODE_ECB 0
#define ESP_AES_BLOCK_MODE_CBC 1
#define ESP_AES_BLOCK_MODE_CFB8 2
#define ESP_AES_BLOCK_MODE_CFB128 3
#define ESP_AES_BLOCK_MODE_OFB 4
#define ESP_AES_BLOCK_MODE_CTR 5
#define MALLOC_CAP_DMA 8u
#define MALLOC_CAP_SPIRAM 1024u
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 1u
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C 2u
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED 4u
#define CACHE_LL_LEVEL_EXT_MEM 2
#define CACHE_LL_LEVEL_INT_MEM 1
#define CACHE_TYPE_DATA 1
#define ESP_OK 0
#define ESP_FAIL -1
#define ETS_AES_INTR_SOURCE 1
#define portTICK_PERIOD_MS 1
#define portYIELD_FROM_ISR() ((void)0)
#define DMA_DESCRIPTOR_BUFFER_MAX_SIZE_16B_ALIGNED 4080
#define DMA_DESCRIPTOR_BUFFER_MAX_SIZE_4B_ALIGNED 4092
#define DMA_DESCRIPTOR_BUFFER_OWNER_DMA 1
#define ESP_LOGE(tag, ...) host_log(tag, __VA_ARGS__)
typedef int esp_err_t;
typedef int BaseType_t;
typedef int StaticSemaphore_t;
typedef void *SemaphoreHandle_t;
typedef struct crypto_dma_desc {
    struct { unsigned size, length, suc_eof, owner; } dw0;
    void *buffer;
    struct crypto_dma_desc *next;
} crypto_dma_desc_t;

int esp_aes_process_dma(esp_aes_context *, const unsigned char *, unsigned char *, size_t, uint8_t *);
bool valid_key_length(const esp_aes_context *);
void esp_aes_intr_alloc(void);
bool esp_ptr_external_ram(const void *);
bool esp_ptr_dma_ext_capable(const void *);
bool esp_ptr_dma_capable(const void *);
int esp_cache_get_alignment(unsigned, size_t *);
int esp_cache_msync(void *, size_t, unsigned);
unsigned cache_hal_get_cache_line_size(unsigned, unsigned);
void *heap_caps_aligned_alloc(size_t, size_t, unsigned);
void *heap_caps_aligned_calloc(size_t, size_t, size_t, unsigned);
void host_tracked_free(void *);
void mbedtls_platform_zeroize(void *, size_t);
void host_log(const char *, const char *, ...);
void esp_crypto_sha_aes_lock_acquire(void);
void esp_crypto_sha_aes_lock_release(void);
void esp_crypto_aes_enable_periph_clk(bool);
unsigned aes_hal_setkey(const unsigned char *, unsigned, int);
void aes_hal_mode_init(int);
void aes_hal_set_iv(const unsigned char *);
void aes_hal_read_iv(unsigned char *);
void aes_hal_transform_block(const void *, void *);
void aes_hal_transform_dma_start(unsigned);
void aes_hal_transform_dma_finish(void);
void aes_hal_wait_done(void);
void aes_hal_interrupt_enable(bool);
void aes_hal_interrupt_clear(void);
int esp_aes_dma_start(const crypto_dma_desc_t *, const crypto_dma_desc_t *);
bool esp_aes_dma_done(const crypto_dma_desc_t *);
int esp_intr_level_to_flags(int);
int esp_intr_alloc(int, int, void (*)(void *), void *, void *);
void xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t *);
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
static inline size_t dma_desc_get_required_num(size_t size, size_t chunk) {
    return (size + chunk - 1) / chunk;
}

typedef struct {
    unsigned alloc_calls, frees, logs, dma_calls, lock_acquires, lock_releases;
    size_t outstanding_bytes, peak_bytes;
    int lock_depth;
    bool clock_enabled;
    size_t requested_bytes[16];
} HostState;
void host_reset(void);
void host_memory(const void *input, size_t input_size, bool input_dma,
                 const void *output, size_t output_size, bool output_external);
void host_fail_allocation(unsigned call);
HostState host_state(void);
