// LVGL heap backend (CONFIG_LV_USE_CUSTOM_MALLOC).
//
// With the C library backend every LVGL object, style and text buffer at or below
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL bytes lands in internal SRAM. Widget trees
// rebuilt on app switches then scatter long-lived small blocks through the internal
// heap and split the contiguous runs that DMA and network tasks need. LVGL data is
// touched from tasks, plus lv_display_flush_ready() in the LCD SPI completion ISR. That
// ISR is not IRAM-safe (bus intr_flags=0), so it stays masked while the flash cache is
// off. Making it cache-safe would require the display object in internal RAM. The
// display DMA buffers come from esp_lvgl_port, not from this heap.
//
// heap_caps_*_prefer bypass the heap_caps_*_default wrappers that inject release-test
// failures, so release-test builds ask the injector directly.

#include <lvgl.h>

#include <esp_heap_caps.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#define LVGL_HEAP_PREFERRED (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
// MALLOC_CAP_DEFAULT keeps the fallback out of the CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL pool,
// which must stay free for DMA buffers such as the Camera receive ring.
#define LVGL_HEAP_FALLBACK (MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

#ifdef RODAKOS_RELEASE_TESTS
bool rodakos_lvgl_allocation_should_fail(size_t size);
#else
static inline bool rodakos_lvgl_allocation_should_fail(size_t size)
{
    LV_UNUSED(size);
    return false;
}
#endif

void lv_mem_init(void) {}

void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void* lv_malloc_core(size_t size)
{
    if (rodakos_lvgl_allocation_should_fail(size)) return NULL;
    return heap_caps_malloc_prefer(size, 2, LVGL_HEAP_PREFERRED, LVGL_HEAP_FALLBACK);
}

void* lv_realloc_core(void* p, size_t new_size)
{
    if (rodakos_lvgl_allocation_should_fail(new_size)) return NULL;
    return heap_caps_realloc_prefer(p, new_size, 2, LVGL_HEAP_PREFERRED, LVGL_HEAP_FALLBACK);
}

void lv_free_core(void* p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t* mon_p)
{
    LV_UNUSED(mon_p);
}

lv_result_t lv_mem_test_core(void)
{
    return LV_RESULT_OK;
}

#endif
