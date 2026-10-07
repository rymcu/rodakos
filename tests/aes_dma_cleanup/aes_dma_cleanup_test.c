#include "host_fakes.h"

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "ASSERTION FAILED: %s\n", #condition); return 1; } } while (0)
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
        CHECK(state.requested_bytes[1] == (bytes < 1600 ? bytes : 1600));
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
