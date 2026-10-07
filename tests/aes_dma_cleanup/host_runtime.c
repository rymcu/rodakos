#include "host_fakes.h"
#include <stdarg.h>

typedef struct { void *pointer; size_t size; } Allocation;
static Allocation allocations[32];
static HostState state;
static unsigned fail_at;
static const unsigned char *source, *destination;
static size_t source_size, destination_size;
static bool source_dma, destination_external;
static const crypto_dma_desc_t *dma_in, *dma_out;
static unsigned char iv[16];

void host_reset(void) {
    assert(state.outstanding_bytes == 0 && state.lock_depth == 0);
    memset(&state, 0, sizeof(state));
    fail_at = 0;
}
void host_memory(const void *input, size_t in_size, bool in_dma,
                 const void *output, size_t out_size, bool out_external) {
    source = input; source_size = in_size; source_dma = in_dma;
    destination = output; destination_size = out_size; destination_external = out_external;
}
void host_fail_allocation(unsigned call) { fail_at = call; }
HostState host_state(void) { return state; }
static bool contains(const void *base, size_t size, const void *pointer) {
    return (uintptr_t)pointer >= (uintptr_t)base && (uintptr_t)pointer - (uintptr_t)base < size;
}
bool esp_ptr_external_ram(const void *p) { return destination_external && contains(destination, destination_size, p); }
bool esp_ptr_dma_ext_capable(const void *p) { return esp_ptr_external_ram(p); }
bool esp_ptr_dma_capable(const void *p) {
    return !esp_ptr_external_ram(p) && !(contains(source, source_size, p) && !source_dma);
}
int esp_cache_get_alignment(unsigned caps, size_t *alignment) { *alignment = 32; return ESP_OK; }
int esp_cache_msync(void *p, size_t bytes, unsigned flags) { return ESP_OK; }
unsigned cache_hal_get_cache_line_size(unsigned level, unsigned type) { return 32; }
void *heap_caps_aligned_alloc(size_t alignment, size_t bytes, unsigned caps) {
    assert(caps == MALLOC_CAP_DMA);
    state.alloc_calls++;
    assert(state.alloc_calls <= 16);
    state.requested_bytes[state.alloc_calls - 1] = bytes;
    if (state.alloc_calls == fail_at) return NULL;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    void *p = NULL;
    int result = posix_memalign(&p, alignment, bytes);
    if (result != 0) abort();
    for (unsigned i = 0; i < 32; i++) {
        if (allocations[i].pointer) continue;
        allocations[i] = (Allocation){p, bytes};
        state.outstanding_bytes += bytes;
        if (state.outstanding_bytes > state.peak_bytes) state.peak_bytes = state.outstanding_bytes;
        return p;
    }
    abort();
}
void *heap_caps_aligned_calloc(size_t alignment, size_t count, size_t bytes, unsigned caps) {
    void *p = heap_caps_aligned_alloc(alignment, count * bytes, caps);
    if (p) memset(p, 0, count * bytes);
    return p;
}
void host_tracked_free(void *p) {
    if (!p) return;
    for (unsigned i = 0; i < 32; i++) {
        if (allocations[i].pointer != p) continue;
        state.outstanding_bytes -= allocations[i].size;
        allocations[i] = (Allocation){0};
        state.frees++;
        free(p);
        return;
    }
    assert(!"invalid or duplicate production free");
}
void mbedtls_platform_zeroize(void *p, size_t size) { memset(p, 0, size); }
void host_log(const char *tag, const char *format, ...) { state.logs++; }
void esp_crypto_sha_aes_lock_acquire(void) { assert(state.lock_depth == 0); state.lock_depth++; state.lock_acquires++; }
void esp_crypto_sha_aes_lock_release(void) { assert(state.lock_depth == 1); state.lock_depth--; state.lock_releases++; }
void esp_crypto_aes_enable_periph_clk(bool enabled) { state.clock_enabled = enabled; }
unsigned aes_hal_setkey(const unsigned char *key, unsigned bytes, int mode) { return bytes; }
void aes_hal_mode_init(int mode) {}
void aes_hal_set_iv(const unsigned char *value) { memcpy(iv, value, 16); }
void aes_hal_read_iv(unsigned char *value) { memcpy(value, iv, 16); }
void aes_hal_transform_block(const void *input, void *output) {
    for (unsigned i = 0; i < 16; i++) ((unsigned char *)output)[i] = ((const unsigned char *)input)[i] ^ 0xa5;
}
int esp_aes_dma_start(const crypto_dma_desc_t *input, const crypto_dma_desc_t *output) {
    assert(state.lock_depth == 1); dma_in = input; dma_out = output; state.dma_calls++; return ESP_OK;
}
void aes_hal_transform_dma_start(unsigned blocks) {
    const crypto_dma_desc_t *in = dma_in, *out = dma_out;
    size_t in_pos = 0, out_pos = 0;
    for (size_t i = 0; i < blocks * 16; i++) {
        assert(in && out);
        ((unsigned char *)out->buffer)[out_pos++] = ((unsigned char *)in->buffer)[in_pos++] ^ 0xa5;
        if (in_pos == in->dw0.length) { in = in->next; in_pos = 0; }
        if (out_pos == out->dw0.length) { ((crypto_dma_desc_t *)out)->dw0.owner = 0; out = out->next; out_pos = 0; }
    }
}
void aes_hal_transform_dma_finish(void) {}
void aes_hal_wait_done(void) {}
void aes_hal_interrupt_enable(bool enabled) {}
void aes_hal_interrupt_clear(void) {}
bool esp_aes_dma_done(const crypto_dma_desc_t *output) { return output->dw0.owner == 0; }
int esp_intr_level_to_flags(int level) { return level; }
int esp_intr_alloc(int source_id, int flags, void (*callback)(void *), void *arg, void *handle) { return ESP_OK; }
void xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *woken) { *woken = 0; }
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *sem) { return sem; }
int xSemaphoreTake(SemaphoreHandle_t sem, unsigned ticks) { return 1; }
