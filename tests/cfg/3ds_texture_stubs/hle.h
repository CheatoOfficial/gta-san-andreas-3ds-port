#ifndef TEXTURE_TEST_HLE_H
#define TEXTURE_TEST_HLE_H
#include <stdint.h>
#include <string.h>
extern uint8_t *hle_ram;
extern uint32_t hle_ram_end;
#define LOG(...) ((void)0)
static inline uint32_t gw(uint32_t p) { uint32_t n; memcpy(&n, hle_ram+p, 4); return n; }
static inline uint8_t gb(uint32_t p) { return hle_ram[p]; }
#endif
