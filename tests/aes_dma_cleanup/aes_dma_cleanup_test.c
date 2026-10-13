#include "host_fakes.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "ASSERTION FAILED: %s\n", #condition); return 1; } } while (0)
#ifdef RODAK_AES_PSRAM_BOUNCE
/* 103: a PSRAM output bounce is rounded up to the 64 B PSRAM cache line. */
static bool output_bounce_bytes(size_t requested, size_t bytes) {
    size_t chunk = bytes < 1600 ? bytes : 1600;
    return requested == (chunk + 63) / 64 * 64;
}
#else
static bool output_bounce_bytes(size_t requested, size_t bytes) {
    return requested == (bytes < 1600 ? bytes : 1600);
}
#endif

/* 103: an unaligned PSRAM output with a DMA-capable input (WebRTC in-place SRTP) needs only an
 * output bounce. The fix takes it from cache-line-aligned PSRAM, never from internal DMA RAM. */
static int psram_output_bounce(size_t bytes, bool ctr) {
    /* 6144 B covers the largest observed WebRTC AES length (5,113 B on the device). */
    _Alignas(64) static unsigned char input[6144];
    _Alignas(64) static unsigned char destination[6208];
    if (bytes > sizeof(input) || bytes + 2 > sizeof(destination)) return 1;
    unsigned char *output = destination + 1;
    unsigned char iv[16] = {0};
    esp_aes_context ctx = {.key_bytes = 16};
    memset(input, 0x3c, sizeof(input));
    memset(destination, 0x7b, sizeof(destination));
    host_reset();
    host_memory(input, sizeof(input), true, destination, sizeof(destination), true);
    /* SRTP uses CTR, which accepts any length and exercises the sub-line tail split. */
    size_t offset = 0;
    unsigned char stream[16] = {0};
    int result = ctr ? esp_aes_crypt_ctr(&ctx, bytes, &offset, iv, stream, input, output)
                     : esp_aes_crypt_cbc(&ctx, ESP_AES_ENCRYPT, bytes, iv, input, output);
    HostState state = host_state();
    CHECK(result == 0);
    for (size_t i = 0; i < bytes; i++) CHECK(output[i] == (0x3c ^ 0xa5));
    CHECK(destination[0] == 0x7b && output[bytes] == 0x7b);
    CHECK(state.alloc_calls >= 1);
    /* First request is the output bounce; later ones are DMA descriptors. */
    CHECK(state.requested_caps[0] == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    CHECK(state.requested_alignment[0] == 64);
    CHECK(state.requested_bytes[0] % 64 == 0);
    CHECK(state.requested_bytes[0] >= (bytes < 1600 ? bytes : 1600));
    for (unsigned i = 1; i < state.alloc_calls && i < 16; i++) CHECK(state.requested_caps[i] == MALLOC_CAP_DMA);
    CHECK(state.outstanding_bytes == 0);
    printf("PASS psram_bounce mode=%s bytes=%zu first_caps=%u first_align=%zu first_bytes=%zu\n",
           ctr ? "ctr" : "cbc", bytes, state.requested_caps[0], state.requested_alignment[0],
           state.requested_bytes[0]);
    return 0;
}
static int run(bool input_dma, bool output_external, size_t bytes, unsigned fail_at) {
    _Alignas(32) unsigned char input[4800];
    _Alignas(32) unsigned char destination[4832];
    unsigned char *output = destination + (output_external ? 1 : 0);
    unsigned char iv[16] = {0};
    esp_aes_context ctx = {.key_bytes = 16};
    memset(input, 0x3c, sizeof(input));
    memset(destination, 0x7b, sizeof(destination));
    host_reset();
    host_memory(input, sizeof(input), input_dma, destination, sizeof(destination), output_external);
    host_fail_allocation(fail_at);
    int result = esp_aes_crypt_cbc(&ctx, ESP_AES_ENCRYPT, bytes, iv, input, output);
    HostState state = host_state();
    CHECK(state.lock_acquires == 1 && state.lock_releases == 1 && state.lock_depth == 0);
    CHECK(!state.clock_enabled);
    CHECK(result == (fail_at ? -1 : 0));
    for (size_t i = 0; i < bytes; i++) CHECK(output[i] == (fail_at ? 0 : (0x3c ^ 0xa5)));
    if (output_external) CHECK(destination[0] == 0x7b);
    CHECK(output[bytes] == 0x7b);
    if (fail_at) CHECK(state.dma_calls == 0);
    if (!input_dma) CHECK(state.requested_bytes[0] == (bytes < 1600 ? bytes : 1600));
    if (!input_dma && output_external && fail_at != 1) {
        CHECK(output_bounce_bytes(state.requested_bytes[1], bytes));
    }
    CHECK(state.outstanding_bytes == 0);
    if (!input_dma && output_external && fail_at == 2) CHECK(state.frees == 1);
    printf("PASS bytes=%zu input_dma=%d output_external=%d failure=%u alloc=%u frees=%u peak=%zu\n",
           bytes, input_dma, output_external, fail_at, state.alloc_calls, state.frees, state.peak_bytes);
    return 0;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "output_failure")) {
        CHECK(run(false, true, 256, 2) == 0);
        CHECK(run(false, true, 4800, 2) == 0);
        return run(false, true, 256, 0);
    }
    if (!strcmp(argv[1], "psram_output_bounce")) {
        CHECK(psram_output_bounce(1232, false) == 0);
        CHECK(psram_output_bounce(4800, false) == 0);
        CHECK(psram_output_bounce(1232, true) == 0);
        CHECK(psram_output_bounce(454, true) == 0);
        CHECK(psram_output_bounce(5113, true) == 0);
        return psram_output_bounce(1601, true);
    }
    if (!strcmp(argv[1], "input_failure")) return run(false, true, 256, 1);
    if (!strcmp(argv[1], "output_only_failure")) return run(true, true, 256, 1);
    if (!strcmp(argv[1], "success")) {
        CHECK(run(false, true, 256, 0) == 0);
        CHECK(run(false, true, 4800, 0) == 0);
        CHECK(run(false, true, 1600, 0) == 0);
        CHECK(run(false, true, 1616, 0) == 0);
        CHECK(run(true, true, 256, 0) == 0);
        CHECK(run(false, false, 256, 0) == 0);
        return run(true, false, 256, 0);
    }
    return 2;
}
