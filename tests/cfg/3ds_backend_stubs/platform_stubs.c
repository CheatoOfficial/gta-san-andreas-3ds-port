#include <stdint.h>
#include <stdlib.h>
int hle_3ds_gfx_ready(void) { return 1; }
void hle_plat_log_flush(void) {}
int hle_3ds_texture_prepare(uint32_t raster, int *alpha) { (void)raster; (void)alpha; return 0; }
void *hle_plat_big_alloc(uint32_t n) { return malloc(n); }
void GSPGPU_FlushDataCache(void *p, uint32_t n) { (void)p; (void)n; }
uint32_t hle_3ds_platform_us(void) { return 0; }
