/* New 3DS HID to DualShock 2 mapping, kept separate so it can be host-tested. */
#include <3ds.h>
#include <math.h>
#include <stdint.h>
#include "hle_3ds_padmap.h"

uint16_t hle_3ds_pad_map_buttons(uint32_t keys)
{
    uint16_t buttons = 0;
    if (keys & KEY_SELECT) buttons |= 1u << 0;
    if (keys & KEY_START) buttons |= 1u << 3;
    if (keys & KEY_DUP) buttons |= 1u << 4;
    if (keys & KEY_DRIGHT) buttons |= 1u << 5;
    if (keys & KEY_DDOWN) buttons |= 1u << 6;
    if (keys & KEY_DLEFT) buttons |= 1u << 7;
    if (keys & KEY_ZL) buttons |= 1u << 8;
    if (keys & KEY_ZR) buttons |= 1u << 9;
    if (keys & KEY_L) buttons |= 1u << 10;
    if (keys & KEY_R) buttons |= 1u << 11;

    /* Match REGTA's New 3DS LCS mapping: Cross=A, Circle=X, Square=B, Triangle=Y. */
    if (keys & KEY_X) buttons |= 1u << 13;
    if (keys & KEY_B) buttons |= 1u << 15;
    if (keys & KEY_A) buttons |= 1u << 14;
    if (keys & KEY_Y) buttons |= 1u << 12;
    return buttons;
}

static float clamp_unit(float value)
{
    return value < -1.0f ? -1.0f : value > 1.0f ? 1.0f : value;
}

static void apply_radial_deadzone(float *x, float *y, float deadzone)
{
    float magnitude = sqrtf(*x * *x + *y * *y);
    if (magnitude <= deadzone) {
        *x = *y = 0.0f;
        return;
    }
    if (magnitude > 1.0f) {
        *x /= magnitude;
        *y /= magnitude;
        magnitude = 1.0f;
    }
    const float scaled_magnitude = (magnitude - deadzone) / (1.0f - deadzone);
    const float scale = scaled_magnitude / magnitude;
    *x *= scale;
    *y *= scale;
}

static uint8_t axis_byte(float value)
{
    value = clamp_unit(value);
    const int mapped = 128 + (int)(value < 0.0f ? value * 128.0f : value * 127.0f);
    return (uint8_t)(mapped < 0 ? 0 : mapped > 255 ? 255 : mapped);
}

void hle_3ds_pad_map_sticks(int16_t circle_x, int16_t circle_y, int16_t cstick_x, int16_t cstick_y,
                            uint8_t sticks[4])
{
    if (!sticks) return;

    float left_x = clamp_unit((float)circle_x / 156.0f);
    float left_y = clamp_unit(-(float)circle_y / 156.0f);
    float right_x = clamp_unit((float)cstick_x / 140.0f);
    float right_y = clamp_unit(-(float)cstick_y / 140.0f);
    apply_radial_deadzone(&left_x, &left_y, 0.14f);
    apply_radial_deadzone(&right_x, &right_y, 0.18f);

    /* The New 3DS C-stick has short travel; REGTA compensates after the deadzone. */
    right_x = clamp_unit(right_x * 2.3f);
    right_y = clamp_unit(right_y * 2.3f);
    sticks[0] = axis_byte(right_x);
    sticks[1] = axis_byte(right_y);
    sticks[2] = axis_byte(left_x);
    sticks[3] = axis_byte(left_y);
}

uint16_t hle_3ds_pad_map_old(uint32_t keys, int16_t circle_x, int16_t circle_y, uint8_t sticks[4])
{
    const uint32_t pad = KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT;
    const uint32_t chords = pad | KEY_L | KEY_R | KEY_A | KEY_X;
    /* The Old model has no ZL/ZR; ignore those bits even when testing on New hardware. */
    uint16_t buttons = hle_3ds_pad_map_buttons(keys & ~(KEY_ZL | KEY_ZR));
    hle_3ds_pad_map_sticks(circle_x, circle_y, 0, 0, sticks);
    if (keys & KEY_SELECT) {
        if (keys & chords) buttons &= ~(1u << 0); /* consume SELECT only for a modifier chord */
        if (keys & KEY_L) { buttons &= ~(1u << 10); buttons |= 1u << 8; }
        if (keys & KEY_R) { buttons &= ~(1u << 11); buttons |= 1u << 9; }
        if (keys & KEY_A) { buttons &= ~(1u << 14); buttons |= 1u << 1; }
        if (keys & KEY_X) { buttons &= ~(1u << 13); buttons |= 1u << 2; }
    } else {
        buttons &= ~(0xFu << 4);
        if (sticks) {
            const int x = !!(keys & KEY_DRIGHT) - !!(keys & KEY_DLEFT);
            const int y = !!(keys & KEY_DDOWN) - !!(keys & KEY_DUP);
            sticks[0] = x < 0 ? 0 : x > 0 ? 255 : 128;
            sticks[1] = y < 0 ? 0 : y > 0 ? 255 : 128;
        }
    }
    return buttons;
}
