#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <3ds.h>
#include "../../hle/3ds/hle_3ds_padmap.h"

static void check_button(uint32_t key, uint16_t expected)
{
    assert(hle_3ds_pad_map_buttons(key) == expected);
}

int main(void)
{
    check_button(KEY_SELECT, 1u << 0);
    check_button(KEY_START, 1u << 3);
    check_button(KEY_DUP, 1u << 4);
    check_button(KEY_DRIGHT, 1u << 5);
    check_button(KEY_DDOWN, 1u << 6);
    check_button(KEY_DLEFT, 1u << 7);
    check_button(KEY_ZL, 1u << 8);
    check_button(KEY_ZR, 1u << 9);
    check_button(KEY_L, 1u << 10);
    check_button(KEY_R, 1u << 11);
    check_button(KEY_Y, 1u << 12);
    check_button(KEY_X, 1u << 13);
    check_button(KEY_A, 1u << 14);
    check_button(KEY_B, 1u << 15);
    assert(hle_3ds_pad_map_buttons(0) == 0);
    assert(hle_3ds_pad_map_buttons(KEY_A | KEY_X | KEY_L | KEY_DUP) ==
           ((1u << 14) | (1u << 13) | (1u << 10) | (1u << 4)));

    uint8_t sticks[4];
    hle_3ds_pad_map_sticks(0, 0, 0, 0, sticks);
    assert(sticks[0] == 128 && sticks[1] == 128 && sticks[2] == 128 && sticks[3] == 128);

    hle_3ds_pad_map_sticks(156, 0, 0, 0, sticks);
    assert(sticks[2] == 255 && sticks[3] == 128); /* Circle Pad right -> PS2 left X right */
    hle_3ds_pad_map_sticks(0, 156, 0, 0, sticks);
    assert(sticks[2] == 128 && sticks[3] == 0);   /* Circle Pad up -> PS2 left Y up */
    hle_3ds_pad_map_sticks(0, 0, 0, 140, sticks);
    assert(sticks[0] == 128 && sticks[1] == 0);   /* C-stick up -> PS2 right Y up */
    hle_3ds_pad_map_sticks(0, 0, 10, 10, sticks);
    assert(sticks[0] == 128 && sticks[1] == 128); /* inside C-stick radial deadzone */

    /* Old hardware has neither a C-stick nor ZL/ZR. All second-stick and shoulder actions remain reachable. */
    assert(hle_3ds_pad_map_old(KEY_DRIGHT | KEY_DUP | KEY_A, 156, 0, sticks) == (1u << 14));
    assert(sticks[0] == 255 && sticks[1] == 0 && sticks[2] == 255);
    assert(hle_3ds_pad_map_old(KEY_SELECT | KEY_DLEFT, 0, 0, sticks) == (1u << 7));
    assert(sticks[0] == 128 && sticks[1] == 128);
    assert(hle_3ds_pad_map_old(KEY_SELECT | KEY_L | KEY_R, 0, 0, sticks) == ((1u << 8) | (1u << 9)));
    assert(hle_3ds_pad_map_old(KEY_SELECT | KEY_A, 0, 0, sticks) == (1u << 1));
    assert(hle_3ds_pad_map_old(KEY_SELECT | KEY_X, 0, 0, sticks) == (1u << 2));
    assert(hle_3ds_pad_map_old(KEY_SELECT | KEY_A | KEY_X, 0, 0, sticks) == ((1u << 1) | (1u << 2)));
    assert(hle_3ds_pad_map_old(KEY_L | KEY_R, 0, 0, sticks) == ((1u << 10) | (1u << 11)));
    assert(hle_3ds_pad_map_old(KEY_SELECT, 0, 0, sticks) == 1u);
    assert(hle_3ds_pad_map_old(KEY_ZL | KEY_ZR | KEY_DUP | KEY_DDOWN, 0, 0, sticks) == 0);
    assert(sticks[0] == 128 && sticks[1] == 128);
    assert(hle_3ds_pad_map_old(0, 0, 0, sticks) == 0);
    assert(sticks[0] == 128 && sticks[1] == 128 && sticks[2] == 128 && sticks[3] == 128);

    puts("3DS HID mapping: buttons, active PS2 positions, stick order, Y direction, deadzones and neutral passed");
    return 0;
}
