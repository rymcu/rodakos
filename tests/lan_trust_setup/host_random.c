#include "esp_random.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char g_queue[1024];
static size_t g_length;
static size_t g_offset;

void lan_trust_setup_queue_random(const unsigned char* bytes, size_t length) {
    if (g_length + length > sizeof(g_queue)) {
        fprintf(stderr, "random queue overflow\n");
        abort();
    }
    memcpy(g_queue + g_length, bytes, length);
    g_length += length;
}

size_t lan_trust_setup_random_remaining(void) {
    return g_length - g_offset;
}

void esp_fill_random(void* buffer, size_t length) {
    if (g_length == 0) {
        // 未排队时退回系统随机源：生产模块自行生成码、salt 与 nonce
        FILE* source = fopen("/dev/urandom", "rb");
        if (source == NULL || fread(buffer, 1, length, source) != length) abort();
        fclose(source);
        return;
    }
    if (g_offset + length > g_length) {
        fprintf(stderr, "random queue exhausted: need %zu, have %zu\n", length, g_length - g_offset);
        abort();
    }
    memcpy(buffer, g_queue + g_offset, length);
    g_offset += length;
    if (g_offset == g_length) {
        g_offset = 0;
        g_length = 0;
    }
}
