/* Deterministic controller input for local Azahar startup diagnostics only.
 * Do not enable this in a normal game or a distributed 3DSX. */
#include <stdint.h>
#include "hle.h"

#ifndef HLE_DEV_PAD_SCRIPTED
#define HLE_DEV_PAD_SCRIPTED 1
#endif

typedef struct {
    uint32_t begin_ms;
    uint32_t end_ms;
    uint16_t buttons;
    int8_t left_x;
    int8_t left_y;
} ProbeEvent;

#define START  (1u << 3)
#define UP     (1u << 4)
#define TRIANGLE (1u << 12)
#define CROSS  (1u << 14)
#define SQUARE (1u << 15)

static void log_player_state(void)
{
    const uint32_t ped = gw(0x0070A260u);
    const uint32_t model = ped ? gw(ped + 0x14u) : 0;
    const uint32_t vehicle = ped ? gw(ped + 0x5CCu) : 0;
    const uint32_t intro_ip = gw(0x006B26C4u);
    float position[3] = {0.0f, 0.0f, 0.0f};
    if (model) memcpy(position, gptr(model + 0x30u, sizeof position), sizeof position);
    LOG("3DS player probe: vtime %.3f s ped=%08x model=%08x pos=%.2f,%.2f,%.2f vehicle=%08x intro_ip=%08x\n",
        (double)hle_vtime / EE_HZ, ped, model, position[0], position[1], position[2], vehicle, intro_ip);
}

#if HLE_DEV_PAD_SCRIPTED
#define SKIP_PROMPT_AT(ms) \
    { (ms), (ms) + 200u, START, 0, 0 }, \
    { (ms) + 250u, (ms) + 850u, CROSS, 0, 0 }
static const ProbeEvent events[] = {
    /* The first chord is ignored; the second skips straight to the city. */
    SKIP_PROMPT_AT(40500), SKIP_PROMPT_AT(45500),
    /* Check movement and the nearby bike immediately after the observed city transition. */
    { 46500, 49500, UP, 0, -96 },
    { 49700, 50100, TRIANGLE, 0, 0 },
    { 50300, 54000, UP, 0, -96 },
    { 54100, 54500, TRIANGLE, 0, 0 },
    { 54600, 58000, UP, 0, -96 },
    /* Sustained on-foot check: sprint, turn both ways, jump, then idle so the probe shows settled state. */
    { 58100, 64000, UP | CROSS, 0, -127 },
    { 64000, 68000, UP, 100, -100 },
    { 68000, 72000, UP | CROSS, -100, -100 },
    { 72000, 72400, SQUARE, 0, 0 },
    { 72500, 80000, UP, 0, -127 },
    { 80000, 86000, UP | CROSS, 60, -110 },
    { 86000, 90000, UP, -90, -90 },
    { 90000, 100000, UP | CROSS, 0, -127 },
    { 100000, 104000, UP, 110, -60 },
    { 104000, 120000, UP | CROSS, 0, -127 },
    /* After the measured route: open the pause menu, stay 6 s, resume (both-screen HUD transition check). */
    { 121000, 121300, START, 0, 0 },
    { 127000, 127300, START, 0, 0 },
    { 128000, 132000, UP, 0, -127 },
    /* HUD element check: the game's own PS2 cheats (health/armour/money, weapon set 1, two wanted stars),
       then R2 to the next weapon, so ammo, armour and the wanted level are drawn and their places logged */
    /* health, armour, money */
    { 133000u, 133150u, 1u << 11, 0, 0 },
    { 133300u, 133450u, 1u << 9, 0, 0 },
    { 133600u, 133750u, 1u << 10, 0, 0 },
    { 133900u, 134050u, 1u << 14, 0, 0 },
    { 134200u, 134350u, 1u << 7, 0, 0 },
    { 134500u, 134650u, 1u << 6, 0, 0 },
    { 134800u, 134950u, 1u << 5, 0, 0 },
    { 135100u, 135250u, 1u << 4, 0, 0 },
    { 135400u, 135550u, 1u << 7, 0, 0 },
    { 135700u, 135850u, 1u << 6, 0, 0 },
    { 136000u, 136150u, 1u << 5, 0, 0 },
    { 136300u, 136450u, 1u << 4, 0, 0 },
    /* weapon set 1 */
    { 137600u, 137750u, 1u << 11, 0, 0 },
    { 137900u, 138050u, 1u << 9, 0, 0 },
    { 138200u, 138350u, 1u << 10, 0, 0 },
    { 138500u, 138650u, 1u << 9, 0, 0 },
    { 138800u, 138950u, 1u << 7, 0, 0 },
    { 139100u, 139250u, 1u << 6, 0, 0 },
    { 139400u, 139550u, 1u << 5, 0, 0 },
    { 139700u, 139850u, 1u << 4, 0, 0 },
    { 140000u, 140150u, 1u << 7, 0, 0 },
    { 140300u, 140450u, 1u << 6, 0, 0 },
    { 140600u, 140750u, 1u << 5, 0, 0 },
    { 140900u, 141050u, 1u << 4, 0, 0 },
    /* wanted level */
    { 142200u, 142350u, 1u << 11, 0, 0 },
    { 142500u, 142650u, 1u << 11, 0, 0 },
    { 142800u, 142950u, 1u << 13, 0, 0 },
    { 143100u, 143250u, 1u << 9, 0, 0 },
    { 143400u, 143550u, 1u << 7, 0, 0 },
    { 143700u, 143850u, 1u << 5, 0, 0 },
    { 144000u, 144150u, 1u << 7, 0, 0 },
    { 144300u, 144450u, 1u << 5, 0, 0 },
    { 144600u, 144750u, 1u << 7, 0, 0 },
    { 144900u, 145050u, 1u << 5, 0, 0 },
    /* next weapon */
    { 146200u, 146350u, 1u << 9, 0, 0 },
};
#undef SKIP_PROMPT_AT
#endif

uint16_t hle_dev_pad(uint8_t sticks[4])
{
#if HLE_DEV_PAD_SCRIPTED
    const uint32_t now_ms = (uint32_t)(hle_vtime * 1000ull / EE_HZ);
    uint16_t buttons = 0;
    int8_t left_x = 0;
    int8_t left_y = 0;
    for (unsigned i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
        if (now_ms >= events[i].begin_ms && now_ms < events[i].end_ms) {
            buttons |= events[i].buttons;
            left_x = events[i].left_x;
            left_y = events[i].left_y;
        }
    }
    /* Soak after the checks (150 s to 30 min): sprint 22 s, turn 3 s, run 5 s, repeat - keeps the city streaming. */
    if (now_ms >= 150000u && now_ms < 1800000u) {
        const uint32_t phase = (now_ms - 150000u) % 30000u;
        buttons = UP | (phase < 22000u ? CROSS : 0u);
        left_x = phase >= 22000u && phase < 25000u ? ((now_ms / 30000u) & 1u ? 110 : -110) : 0;
        left_y = -110;
    }

    static uint16_t previous_buttons = 0xFFFFu;
    static int8_t previous_x = 0x7F;
    static int8_t previous_y = 0x7F;
    if (buttons != previous_buttons || left_x != previous_x || left_y != previous_y) {
        LOG("3DS scripted pad: vtime %.3f s mask=%04x left=%02x,%02x\n",
            (double)hle_vtime / EE_HZ, buttons, (uint8_t)(left_x + 0x80), (uint8_t)(left_y + 0x80));
        log_player_state();
        hle_plat_log_flush();
        previous_buttons = buttons;
        previous_x = left_x;
        previous_y = left_y;
    }

    if (sticks) {
        sticks[2] = (uint8_t)(left_x + 0x80);
        sticks[3] = (uint8_t)(left_y + 0x80);
    }
    return buttons;
#else
    (void)sticks;
    return 0;
#endif
}

void hle_dev_pad_observe(uint16_t buttons, const uint8_t sticks[4])
{
    static uint16_t previous_buttons = 0xFFFFu;
    static uint8_t previous_left_x = 0xFFu, previous_left_y = 0xFFu;
    const uint8_t left_x = sticks ? sticks[2] : 0x80u;
    const uint8_t left_y = sticks ? sticks[3] : 0x80u;
    if (buttons != previous_buttons || left_x != previous_left_x || left_y != previous_left_y) {
        LOG("3DS observed pad: vtime %.3f s mask=%04x left=%02x,%02x\n",
            (double)hle_vtime / EE_HZ, buttons, left_x, left_y);
        log_player_state();
        hle_plat_log_flush();
        previous_buttons = buttons;
        previous_left_x = left_x;
        previous_left_y = left_y;
    }
}
