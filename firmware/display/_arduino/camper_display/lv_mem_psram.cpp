// LVGL memory in PSRAM (lv_conf.h: LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM).
// With the C library malloc, small LVGL objects land in internal RAM, and a full UI leaves too
// little of it for TLS (weather, Pushover) and WiFi. Falls back to internal RAM if PSRAM is full.
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <stdlib.h>

extern "C" {

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes) {
  LV_UNUSED(mem);
  LV_UNUSED(bytes);
  return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void* lv_malloc_core(size_t size) {
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(size);
}

void* lv_realloc_core(void* p, size_t new_size) {
  void* r = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return r ? r : realloc(p, new_size);
}

void lv_free_core(void* p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t* mon_p) { LV_UNUSED(mon_p); }

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

}  // extern "C"
