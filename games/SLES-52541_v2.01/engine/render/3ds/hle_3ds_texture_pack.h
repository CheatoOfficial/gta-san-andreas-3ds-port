#ifndef HLE_3DS_TEXTURE_PACK_H
#define HLE_3DS_TEXTURE_PACK_H
#include <stdint.h>

/* PICA component placement, stored little-endian. These are not the PSP's reversed component layouts. */
static inline uint16_t hle_3ds_pack_rgb565(const uint8_t c[4])
{
    return (uint16_t)(((uint16_t)(c[0] >> 3) << 11) | ((uint16_t)(c[1] >> 2) << 5) | (c[2] >> 3));
}
static inline uint16_t hle_3ds_pack_rgba5551(const uint8_t c[4])
{
    return (uint16_t)(((uint16_t)(c[0] >> 3) << 11) | ((uint16_t)(c[1] >> 3) << 6) |
                      ((uint16_t)(c[2] >> 3) << 1) | (c[3] >> 7));
}
static inline uint16_t hle_3ds_pack_rgba4(const uint8_t c[4])
{
    return (uint16_t)(((uint16_t)(c[0] >> 4) << 12) | ((uint16_t)(c[1] >> 4) << 8) |
                      ((uint16_t)(c[2] >> 4) << 4) | (c[3] >> 4));
}
#endif
