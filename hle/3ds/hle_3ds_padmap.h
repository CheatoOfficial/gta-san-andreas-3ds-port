#ifndef HLE_3DS_PADMAP_H
#define HLE_3DS_PADMAP_H

#include <stdint.h>

uint16_t hle_3ds_pad_map_buttons(uint32_t keys);
/* Old 3DS: D-pad camera, SELECT+D-pad digital pad, SELECT+L/R second shoulders, SELECT+A/X stick clicks. */
uint16_t hle_3ds_pad_map_old(uint32_t keys, int16_t circle_x, int16_t circle_y, uint8_t sticks[4]);
void hle_3ds_pad_map_sticks(int16_t circle_x, int16_t circle_y, int16_t cstick_x, int16_t cstick_y,
                           uint8_t sticks[4]);

#endif
