#ifndef HLE_3DS_TEXTURE_LAYOUT_H
#define HLE_3DS_TEXTURE_LAYOUT_H
#include <stdint.h>
/* x/y address a top-origin expanded source, including edge padding. Width and
   height are allocated multiples of eight; the GPU samples its rows inverted. */
static inline uint32_t hle_3ds_texture_texel_index(uint32_t x, uint32_t y,
                                                   uint32_t width, uint32_t height)
{
    const uint32_t dy = height - 1u - y;
    uint32_t morton = 0;
    for (uint32_t bit = 0; bit < 3u; ++bit) {
        morton |= ((x >> bit) & 1u) << (2u * bit);
        morton |= ((dy >> bit) & 1u) << (2u * bit + 1u);
    }
    return (((dy >> 3) * (width >> 3) + (x >> 3)) * 64u + morton);
}
#endif
