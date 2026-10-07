#include "allocation_probe.h"

#include <lvgl.h>
#include <src/draw/lv_draw_buf_private.h>
#include <src/libs/lodepng/lodepng.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void * pointer;
    size_t size;
} allocation_t;

typedef enum {
    FAIL_NONE,
    FAIL_IDAT,
    FAIL_INFLATE,
    FAIL_DESCRIPTOR,
    LIMIT_CONTIGUOUS_BLOCK
} failure_t;

static allocation_t allocations[256];
static bool tracking;
static failure_t failure;
static size_t encoded_size;
static size_t scanlines_size;
static size_t scanlines_capacity;
static size_t live_count;
static size_t live_bytes;
static size_t rejected;
static size_t rejected_size;
static size_t largest_request;
static size_t peak_live_bytes;
static bool inflate_reserved;
static void * idat_pointer;

void * __real_lv_malloc_core(size_t size);
void * __real_lv_realloc_core(void * pointer, size_t size);
void __real_lv_free_core(void * pointer);

static void require(bool condition, const char * message)
{
    if(!condition) {
        fprintf(stderr, "allocation probe invariant failed: %s\n", message);
        abort();
    }
}

static allocation_t * find_allocation(void * pointer)
{
    for(size_t i = 0; i < sizeof(allocations) / sizeof(allocations[0]); ++i) {
        if(allocations[i].pointer == pointer) return &allocations[i];
    }
    return NULL;
}

static void record_allocation(void * pointer, size_t size)
{
    if(!pointer) return;
    allocation_t * slot = find_allocation(NULL);
    require(slot != NULL, "allocation ledger capacity");
    slot->pointer = pointer;
    slot->size = size;
    ++live_count;
    live_bytes += size;
    if(live_bytes > peak_live_bytes) peak_live_bytes = live_bytes;
}

static bool reject(size_t size)
{
    ++rejected;
    rejected_size = size;
    return true;
}

void * __wrap_lv_malloc_core(size_t size)
{
    if(tracking) {
        if(size > largest_request) largest_request = size;
        if(failure == LIMIT_CONTIGUOUS_BLOCK && size > 1081344 && reject(size)) return NULL;
        if(failure == FAIL_IDAT && size == encoded_size && live_count == 0 && reject(size)) return NULL;
        if(failure == FAIL_DESCRIPTOR && inflate_reserved && idat_pointer == NULL &&
           size == sizeof(lv_draw_buf_t) && reject(size)) return NULL;
    }
    void * pointer = __real_lv_malloc_core(size);
    if(tracking) {
        record_allocation(pointer, size);
        if(size == encoded_size && live_count == 1) idat_pointer = pointer;
    }
    return pointer;
}

void * __wrap_lv_realloc_core(void * pointer, size_t size)
{
    if(!tracking) return __real_lv_realloc_core(pointer, size);
    if(size > largest_request) largest_request = size;
    allocation_t * old = pointer ? find_allocation(pointer) : NULL;
    require(!pointer || old != NULL, "realloc belongs to the current decode");
    if(failure == LIMIT_CONTIGUOUS_BLOCK && size > 1081344 && reject(size)) return NULL;
    if(failure == FAIL_INFLATE && size >= scanlines_size && reject(size)) return NULL;
    void * next = __real_lv_realloc_core(pointer, size);
    if(!next) return NULL;
    if(old) {
        live_bytes -= old->size;
        old->pointer = next;
        old->size = size;
        live_bytes += size;
    }
    else record_allocation(next, size);
    if(live_bytes > peak_live_bytes) peak_live_bytes = live_bytes;
    if(size == scanlines_capacity) inflate_reserved = true;
    return next;
}

void __wrap_lv_free_core(void * pointer)
{
    if(tracking && pointer) {
        allocation_t * old = find_allocation(pointer);
        require(old != NULL, "free belongs to the current decode and occurs once");
        --live_count;
        live_bytes -= old->size;
        old->pointer = NULL;
        old->size = 0;
        if(pointer == idat_pointer) idat_pointer = NULL;
    }
    __real_lv_free_core(pointer);
}

static void begin_probe(failure_t next_failure, size_t png_size, size_t filtered_size)
{
    require(!tracking && live_count == 0 && live_bytes == 0, "previous decode returned its allocations");
    failure = next_failure;
    encoded_size = png_size;
    scanlines_size = filtered_size;
    scanlines_capacity = filtered_size + 260;
    rejected = rejected_size = 0;
    largest_request = peak_live_bytes = 0;
    inflate_reserved = false;
    idat_pointer = NULL;
    tracking = true;
}

static int run_decode(const unsigned char * png, size_t png_size,
                      const unsigned char * expected, size_t expected_size,
                      unsigned expected_width, unsigned expected_height, failure_t next_failure)
{
    begin_probe(next_failure, png_size, expected_size + expected_height);
    unsigned char * output = NULL;
    unsigned width = 0, height = 0;
    unsigned error = lodepng_decode32(&output, &width, &height, png, png_size);
    int result = 0;
    if(next_failure != FAIL_NONE && next_failure != LIMIT_CONTIGUOUS_BLOCK) {
        if(error != 83 || output != NULL || rejected == 0) result = 30;
        if(next_failure == FAIL_DESCRIPTOR &&
           (!inflate_reserved || rejected_size != sizeof(lv_draw_buf_t))) result = 31;
        if(next_failure == FAIL_INFLATE && rejected_size < scanlines_size) result = 32;
        if(next_failure == FAIL_IDAT && rejected_size != png_size) result = 33;
    }
    else {
        lv_draw_buf_t * decoded = (lv_draw_buf_t *)output;
        if(error || !decoded || width != expected_width || height != expected_height) result = 34;
        else if(decoded->data_size != scanlines_size || decoded->header.stride != width * 4 ||
                decoded->data != decoded->unaligned_data ||
                decoded->handlers != lv_draw_buf_get_handlers() ||
                (decoded->header.flags & (LV_IMAGE_FLAGS_MODIFIABLE | LV_IMAGE_FLAGS_ALLOCATED)) !=
                    (LV_IMAGE_FLAGS_MODIFIABLE | LV_IMAGE_FLAGS_ALLOCATED) ||
                !inflate_reserved || rejected != 0 || live_count != 2 ||
                largest_request != scanlines_capacity ||
                live_bytes != scanlines_capacity + sizeof(lv_draw_buf_t) ||
                memcmp(decoded->data, expected, expected_size) != 0) result = 35;
    }
    if(output) lv_draw_buf_destroy((lv_draw_buf_t *)output);
    printf("RGBA8 scenario=%d largest_request=%zu peak_live=%zu rejected=%zu final_live=%zu\n",
           (int)next_failure, largest_request, peak_live_bytes, rejected, live_bytes);
    if(live_count != 0 || live_bytes != 0) result = 36;
    tracking = false;
    if(result) fprintf(stderr, "RGBA8 allocation scenario %d failed: code=%u result=%d live=%zu/%zu rejected=%zu\n",
                       (int)next_failure, error, result, live_count, live_bytes, rejected);
    return result;
}

int verify_rgba8_allocation_recovery(const unsigned char * png, size_t png_size,
                                    const unsigned char * expected, size_t expected_size,
                                    unsigned width, unsigned height)
{
    if(expected_size != (size_t)width * height * 4) return 37;
    int result = run_decode(png, png_size, expected, expected_size, width, height, LIMIT_CONTIGUOUS_BLOCK);
    if(result) return result;
    result = run_decode(png, png_size, expected, expected_size, width, height, FAIL_NONE);
    if(result) return result;
    for(failure_t scenario = FAIL_IDAT; scenario <= FAIL_DESCRIPTOR; ++scenario) {
        result = run_decode(png, png_size, expected, expected_size, width, height, scenario);
        if(result) return result;
        result = run_decode(png, png_size, expected, expected_size, width, height, FAIL_NONE);
        if(result) return result;
    }
    puts("471x423 RGBA8: IDAT, inflate realloc and adopted descriptor rejection each release all allocations and recover");
    return 0;
}
