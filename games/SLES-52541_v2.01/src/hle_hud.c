/* hle_hud.c - the HUD on the PSP screen: 4:3 proportions anchored to the screen corners, a round radar, a size and
 * an outline for legibility (overview and keys: hle_hud.h).
 *
 * How: sites.def routes the calls of the HUD draw (0x2B01A0, called once per frame from the 2D pass) to its element
 * functions through the handlers below. A handler
 *   1. draws the text the game has buffered so far (the game's own "draw the buffered text" function, 0x2A8820, which
 *      the game calls 11 times a frame anyway) - so earlier text is not transformed,
 *   2. sets the group's transform (hle_hud.h), calls the element function as the game would,
 *   3. draws the text that element buffered while the transform is still set, then clears it.
 * The register context is restored around each extra text call (the element call sees its arguments, the caller sees
 * the element's results). Only the moment the buffered text is drawn changes (the HUD text is drawn right after its
 * element instead of at the end of the 2D pass) - nothing the game computes.
 * Element groups (facts of the boot program: the call sites in 0x2B01A0, the element positions in guest units):
 *   top right    0x2AB500 clock / health / armour / money / weapon / ammo (x 497..608), 0x2AA4D0 wanted level
 *                                                              anchor (640, 0)
 *   bottom right 0x2AD660 vehicle name, 0x2ADB60 area name (x 608, y 344)   anchor (640, 448)
 *   radar        0x2AC960 radar (ring x 36..138, y 340..424), 0x2AC6F0 (the ring around the radar in some
 *                vehicles)                                     anchor (0, 448), radar factor
 *   bottom left  0x2ABD00 the player's stats panel shown instead of the radar (x 40..210, y 299..426)
 *                                                              anchor (0, 448)
 *   top left     0x2AE060 help text box (x 30..233)            anchor (0, 0)
 *   centre       0x2AED60 script texts (any position), 0x2AEF80 subtitles / dialogue line (x 320), 0x2AF890,
 *                0x2AF220, 0x2AF630 (big / success / odd-job messages, x 320)   anchor x 320 = the 4:3 picture in
 *                the middle of the screen (4:3 factor only: no size change, no outline)
 * The cross-hair (0x2AA9F0, full-screen sniper scope) and everything outside the HUD draw (menus, subtitles,
 * loading screens) keep the old stretch. */
#include "hle.h"
#include "hle_cfg.h"
#include "hle_hud.h"
#include <stdlib.h>
#include <string.h>

int hle_hud_on, hle_hud_outline;
float hle_hud_ax, hle_hud_kx = 1.0f, hle_hud_ay, hle_hud_ky = 1.0f, hle_hud_sx, hle_hud_sy;
int hle_hud_screen, hle_hud_group = -1;

#define HUD_FONT_DRAW 0x002A8820u          /* draws the buffered text and empties the buffer */
#define HUD_FONT_ITER 0x0066C478u          /* the text buffer's write pointer */
#define HUD_FONT_BUF0 0x007C3200u          /* ... its start (empty buffer) */

/* the screen: guest 640x448 (a 4:3 picture) shown at 480x272 */
#define GUEST_W 640.0f
#define GUEST_H 448.0f
#define PSP_W 480.0f
#define PSP_H 272.0f
/* the radar ring in guest units (0x2AC960's four quarter sprites): 102 x 84 */
#define RADAR_W 102.0f
#define RADAR_H 84.0f

enum { G_TR, G_BR, G_RADAR, G_BL, G_TL, G_C, G_N };
static const char *const g_name[G_N] = {"top-right", "bottom-right", "radar", "bottom-left", "top-left", "centre"};

static int ready, opt_fix, opt_scale = 100, opt_outline, opt_radar_ps2, opt_bottom, depth;
static float k43, kradar;
static uint64_t st_calls[G_N], st_flush;

/* a key: the host's environment (HLE_HUD_ASPECT, ...) first, then the cfg table */
static const char *key(const char *k)
{
#ifndef HLE_PSP
    char e[40] = "HLE_";
    size_t n = strlen(k);
    for (size_t i = 0; i < n && i + 5 < sizeof e; i++) e[4 + i] = (char)(k[i] >= 'a' && k[i] <= 'z' ? k[i] - 32 : k[i]);
    const char *v = getenv(e);
    if (v) return v;
#endif
    return hle_cfg_get(k);
}

static int word(const char *k, const char *on, const char *off, int def)
{
    const char *v = key(k);
    if (!v) return def;
    if (!strcmp(v, on) || !strcmp(v, "1") || !strcmp(v, "on")) return 1;
    if (!strcmp(v, off) || !strcmp(v, "0") || !strcmp(v, "off")) return 0;
    return def;
}

static void hud_init(void)
{
    ready = 1;
#ifdef HLE_PSP
    opt_fix = word("hud_aspect", "fix", "stretch", 1);
#else
    opt_fix = word("hud_aspect", "fix", "stretch", 0);         /* host default: the old picture (guest-identical runs) */
#endif
    const char *v = key("hud_scale");
    if (v) opt_scale = atoi(v);
    if (opt_scale < 50) opt_scale = 50;
    if (opt_scale > 200) opt_scale = 200;
    opt_outline = word("hud_outline", "on", "off", 0);
    opt_radar_ps2 = word("hud_radar", "ps2", "round", 0);
    /* 4:3 picture -> the PSP's 480x272: a guest unit is (4/3)/640 of the picture height wide on a TV and
       (PSP_W/GUEST_W) / (PSP_H/GUEST_H) times its height on the PSP */
    float tv = (4.0f / 3.0f) * GUEST_H / GUEST_W, psp = (PSP_W / GUEST_W) / (PSP_H / GUEST_H);
    k43 = tv / psp;                                            /* 0.756 */
    kradar = (RADAR_H * PSP_H / GUEST_H) / (RADAR_W * PSP_W / GUEST_W);   /* 0.667: the ring as wide as high */
    if (opt_radar_ps2) kradar = k43;
#ifdef HLE_3DS
    opt_bottom = word("hud_layout", "bottom", "classic", 1);   /* 3DS default: the dual-screen layout */
    LOG("hud: hud_layout=%s\n", opt_bottom ? "bottom" : "classic");
#endif
    LOG("hud: hud_aspect=%s hud_scale=%d hud_outline=%s hud_radar=%s (4:3 factor %.4f, radar factor %.4f)\n",
        opt_fix ? "fix" : "stretch", opt_scale, opt_outline ? "on" : "off", opt_radar_ps2 ? "ps2" : "round", k43, kradar);
}

/* draw the buffered text now, the register context unchanged for the caller */
static void font_flush(R5900Ctx *c)
{
    static R5900Ctx save;
    if (gw(HUD_FONT_ITER) == HUD_FONT_BUF0) return;            /* nothing buffered */
    memcpy(&save, c, sizeof save);
    rc_dispatch(c, HUD_FONT_DRAW);
    memcpy(c, &save, sizeof save);
    st_flush++;
}

static void set_group(int g)
{
    float s = (float)opt_scale / 100.0f;
    hle_hud_outline = opt_outline;
    switch (g) {
    case G_TR: hle_hud_ax = GUEST_W; hle_hud_ay = 0.0f; hle_hud_kx = k43 * s; hle_hud_ky = s; break;
    case G_BR: hle_hud_ax = GUEST_W; hle_hud_ay = GUEST_H; hle_hud_kx = k43 * s; hle_hud_ky = s; break;
    case G_RADAR: hle_hud_ax = 0.0f; hle_hud_ay = GUEST_H; hle_hud_kx = kradar * s; hle_hud_ky = s; break;
    case G_BL: hle_hud_ax = 0.0f; hle_hud_ay = GUEST_H; hle_hud_kx = k43 * s; hle_hud_ky = s; break;
    case G_TL: hle_hud_ax = 0.0f; hle_hud_ay = 0.0f; hle_hud_kx = k43 * s; hle_hud_ky = s; break;
    default: hle_hud_ax = GUEST_W / 2; hle_hud_ay = 0.0f; hle_hud_kx = k43; hle_hud_ky = 1.0f; hle_hud_outline = 0; break;
    }
    hle_hud_sx = hle_hud_ax; hle_hud_sy = hle_hud_ay;
    hle_hud_on = 1;
}

/* 3DS bottom screen (320x240 px = 640x448 bottom guest units, so a guest unit keeps its 4:3-TV shape there):
   the radar fills a 240x240 px square on the left, the status column (weapon, ammo, clock, money, health, armour,
   wanted level) the 80 px on the right, like the dual-screen showcase layout. Source boxes are the groups' guest
   positions on the PS2 screen (logged by hle_hud_log_bounds); each is scaled uniformly into its target box. */
typedef struct { float sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1; } HudBox;
static const HudBox bottom_box[G_N] = {
    [G_TR]    = {490.0f, 90.0f, 616.0f, 150.0f,  484.0f, 304.0f, 636.0f, 440.0f},  /* rest of status + wanted level */
    [G_RADAR] = { 36.0f, 340.0f, 138.0f, 424.0f, -15.0f, 14.0f, 495.0f, 434.0f},   /* radar ring x5: fills the panel */
    [G_BL]    = { 40.0f, 299.0f, 210.0f, 426.0f,   8.0f,  8.0f, 472.0f, 440.0f},   /* stats panel instead of radar */
};
static int bottom_group(int g) { return g == G_TR || g == G_RADAR || g == G_BL; }

static void set_group_bottom(int g)
{
    const HudBox *b = &bottom_box[g];
    const float kx = (b->dx1 - b->dx0) / (b->sx1 - b->sx0), ky = (b->dy1 - b->dy0) / (b->sy1 - b->sy0);
    const float k = kx < ky ? kx : ky;                        /* uniform: keep the PS2 proportions */
    /* centre the scaled source box horizontally, top-align it (the column grows downward) */
    hle_hud_kx = hle_hud_ky = k;
    hle_hud_sx = b->sx0; hle_hud_sy = b->sy0;
    hle_hud_ax = b->dx0 + ((b->dx1 - b->dx0) - (b->sx1 - b->sx0) * k) * 0.5f;
    hle_hud_ay = g == G_TR ? b->dy0 : b->dy0 + ((b->dy1 - b->dy0) - (b->sy1 - b->sy0) * k) * 0.5f;
    hle_hud_outline = 0;
    hle_hud_screen = 1;
    hle_hud_on = 1;
}

int hle_hud_bottom_layout(void)
{
    if (!ready) hud_init();
    return opt_bottom;
}

static void hud_call(R5900Ctx *c, uint32_t target, int g)
{
    if (!ready) hud_init();
#ifdef HLE_3DS
    if (g == G_TR && !depth) opt_bottom = word("hud_layout", "bottom", "classic", 1);   /* settings menu: live */
#endif
    const int bottom = opt_bottom && bottom_group(g);
    if ((!opt_fix && !bottom) || depth) { rc_dispatch(c, target); return; }
    depth++;
    st_calls[g]++;
    font_flush(c);                                             /* earlier text: as before */
    if (bottom) set_group_bottom(g); else set_group(g);
    hle_hud_group = g;
    rc_dispatch(c, target);
    font_flush(c);                                             /* this element's text: transformed */
    hle_hud_on = 0;
    hle_hud_outline = 0;
    hle_hud_screen = 0;
    hle_hud_group = -1;
    depth--;
}

/* the handlers named in sites.def */
void hud_site_tr(R5900Ctx *c, uint32_t target);
void hud_site_br(R5900Ctx *c, uint32_t target);
void hud_site_radar(R5900Ctx *c, uint32_t target);
void hud_site_bl(R5900Ctx *c, uint32_t target);
void hud_site_tl(R5900Ctx *c, uint32_t target);
void hud_site_c(R5900Ctx *c, uint32_t target);
void hud_site_tr(R5900Ctx *c, uint32_t target) { hud_call(c, target, G_TR); }
void hud_site_br(R5900Ctx *c, uint32_t target) { hud_call(c, target, G_BR); }
void hud_site_radar(R5900Ctx *c, uint32_t target) { hud_call(c, target, G_RADAR); }
void hud_site_bl(R5900Ctx *c, uint32_t target) { hud_call(c, target, G_BL); }
void hud_site_tl(R5900Ctx *c, uint32_t target) { hud_call(c, target, G_TL); }
void hud_site_c(R5900Ctx *c, uint32_t target) { hud_call(c, target, G_C); }

/* Elements inside the status draw 0x2AB500 (sites.def hud_sub_N): while one draws, its buffered text is flushed
   around it so its primitives (and bounds) belong to it alone. */
#define HUD_SUBS 32                                  /* 0..19 status draw, 20..31 radar draw */
static int sub_active = -1;
static uint64_t sub_skipped;
static float sub_bounds[HUD_SUBS][4];
static int sub_seen[HUD_SUBS];

/* Placement of each status element in the bottom screen's right-hand column (bottom guest units: 640x448 for the
   320x240 px screen; the column is x 484..636, centre 560). (sx, sy) is the element's anchor on the PS2 screen
   (logged by hle_hud_log_bounds), (dx, dy) where it goes, k its uniform scale. Top to bottom like the dual-screen
   showcase: weapon icon, ammo, clock, money, health, armour, breath; the wanted level follows (group transform). */
typedef struct { float sx, sy, dx, dy, k; } HudPlace;   /* k < 0: not drawn in the bottom layout */
static const HudPlace sub_place[HUD_SUBS] = {
    [0]  = {580.5f, 18.5f, 560.0f, 146.0f, 1.40f},   /* clock 0x2A7020: x 545.5..615.5 y 18.5..43.1, centred */
    [3]  = {577.5f, 61.0f, 560.0f, 230.0f, 2.00f},   /* health bar 0x2A9C60: x 547..608 y 61..70 */
    [4]  = {577.5f, 61.0f, 560.0f, 230.0f, 2.00f},   /* (second health call) */
    [5]  = {577.5f, 41.0f, 560.0f, 256.0f, 2.00f},   /* armour bar 0x2A99E0: x 546..608 y 41..50 (cheat run) */
    [6]  = {577.5f, 41.0f, 560.0f, 256.0f, 2.00f},
    [9]  = {577.5f, 47.0f, 560.0f, 282.0f, 2.00f},   /* 0x2A9B40 (breath bar, x 546; y not yet observed) */
    [10] = {577.5f, 47.0f, 560.0f, 282.0f, 2.00f},
    [13] = {556.3f, 69.1f, 560.0f, 186.0f, 1.20f},   /* money 0x2A7020: x 497.1..615.5 y 69.1..93.6, centred */
    [16] = {520.5f, 18.0f, 560.0f,  10.0f, 1.80f},   /* weapon icon 0x2A9F20: x 497..544 y 18..72, centred */
    [17] = {520.5f, 18.0f, 560.0f,  10.0f, 1.80f},
    [18] = {521.5f, 57.0f, 560.0f, 112.0f, 1.50f},   /* ammo 0x2AA150: x 504.6..538.4 y 57..72 (soak); under the icon */
    [19] = {521.5f, 57.0f, 560.0f, 112.0f, 1.50f},
    /* radar draw 0x2AC960: 26..29 are the four quarter sprites of the black ring (0x2B0B60, one ring quadrant each,
       ring box x 36..138 y 340..424); the square bottom-screen radar leaves them out, like the showcase */
    [26] = {0, 0, 0, 0, -1.0f}, [27] = {0, 0, 0, 0, -1.0f}, [28] = {0, 0, 0, 0, -1.0f}, [29] = {0, 0, 0, 0, -1.0f},
};

static void hud_sub_call(R5900Ctx *c, uint32_t target, int n)
{
    if (!opt_bottom || hle_hud_group != (n < 20 ? G_TR : G_RADAR) || depth != 1) { rc_dispatch(c, target); return; }
    font_flush(c);                                             /* the status text so far: before this element */
    const int previous = sub_active;
    const float ax = hle_hud_ax, ay = hle_hud_ay, sx = hle_hud_sx, sy = hle_hud_sy, kx = hle_hud_kx, ky = hle_hud_ky;
    const HudPlace *place = &sub_place[n];
    if (place->k < 0.0f) { sub_skipped++; return; }           /* left out of the bottom layout */
    if (place->k > 0.0f) {
        hle_hud_sx = place->sx; hle_hud_sy = place->sy;
        hle_hud_ax = place->dx; hle_hud_ay = place->dy;
        hle_hud_kx = hle_hud_ky = place->k;
    }
    sub_active = n;
    rc_dispatch(c, target);
    font_flush(c);                                             /* this element's text, still placed */
    sub_active = previous;
    hle_hud_ax = ax; hle_hud_ay = ay; hle_hud_sx = sx; hle_hud_sy = sy; hle_hud_kx = kx; hle_hud_ky = ky;
}
#define HUD_SUB(n) void hud_sub_##n(R5900Ctx *c, uint32_t target); \
    void hud_sub_##n(R5900Ctx *c, uint32_t target) { hud_sub_call(c, target, n); }
HUD_SUB(0) HUD_SUB(1) HUD_SUB(2) HUD_SUB(3) HUD_SUB(4) HUD_SUB(5) HUD_SUB(6) HUD_SUB(7) HUD_SUB(8) HUD_SUB(9)
HUD_SUB(10) HUD_SUB(11) HUD_SUB(12) HUD_SUB(13) HUD_SUB(14) HUD_SUB(15) HUD_SUB(16) HUD_SUB(17) HUD_SUB(18) HUD_SUB(19)
HUD_SUB(20) HUD_SUB(21) HUD_SUB(22) HUD_SUB(23) HUD_SUB(24) HUD_SUB(25) HUD_SUB(26) HUD_SUB(27) HUD_SUB(28) HUD_SUB(29)
HUD_SUB(30) HUD_SUB(31)

/* source-space bounds per group, from transformed primitive bounds (inverse of the active transform) */
static float bounds[G_N][4];
static int bounds_seen[G_N];
void hle_hud_note_bounds(float x0, float y0, float x1, float y1)
{
    const int g = hle_hud_group;
    if (g < 0 || g >= G_N || !hle_hud_on || hle_hud_kx == 0.0f || hle_hud_ky == 0.0f) return;
    const float sx0 = hle_hud_sx + (x0 - hle_hud_ax) / hle_hud_kx, sx1 = hle_hud_sx + (x1 - hle_hud_ax) / hle_hud_kx;
    const float sy0 = hle_hud_sy + (y0 - hle_hud_ay) / hle_hud_ky, sy1 = hle_hud_sy + (y1 - hle_hud_ay) / hle_hud_ky;
    if (sub_active >= 0) {
        float *e = sub_bounds[sub_active];
        if (!sub_seen[sub_active]) { e[0] = sx0; e[1] = sy0; e[2] = sx1; e[3] = sy1; sub_seen[sub_active] = 1; }
        else {
            if (sx0 < e[0]) e[0] = sx0;
            if (sy0 < e[1]) e[1] = sy0;
            if (sx1 > e[2]) e[2] = sx1;
            if (sy1 > e[3]) e[3] = sy1;
        }
    }
    float *b = bounds[g];
    if (!bounds_seen[g]) { b[0] = sx0; b[1] = sy0; b[2] = sx1; b[3] = sy1; bounds_seen[g] = 1; return; }
    if (sx0 < b[0]) b[0] = sx0;
    if (sy0 < b[1]) b[1] = sy0;
    if (sx1 > b[2]) b[2] = sx1;
    if (sy1 > b[3]) b[3] = sy1;
}

void hle_hud_log_bounds(unsigned frame)
{
    for (int g = 0; g < G_N; g++) {
        if (!bounds_seen[g]) continue;
        LOG("hud bounds frame %u %s: guest x %.1f..%.1f y %.1f..%.1f\n", frame, g_name[g],
            bounds[g][0], bounds[g][2], bounds[g][1], bounds[g][3]);
        bounds_seen[g] = 0;
    }
    for (int n = 0; n < HUD_SUBS; n++) {
        if (!sub_seen[n]) continue;
        LOG("hud bounds frame %u element %d: guest x %.1f..%.1f y %.1f..%.1f\n", frame, n,
            sub_bounds[n][0], sub_bounds[n][2], sub_bounds[n][1], sub_bounds[n][3]);
        sub_seen[n] = 0;
    }
}

void hle_hud_report(void)
{
    if (!ready) return;
    LOG("hud: layout %s, %llu ring draws left out\n", opt_bottom ? "bottom" : "classic", (unsigned long long)sub_skipped);
    LOG("hud: %s, scale %d %%, outline %s; element calls", opt_fix ? "fix" : "stretch", opt_scale,
        opt_outline ? "on" : "off");
    for (int g = 0; g < G_N; g++) LOG(" %s %llu", g_name[g], (unsigned long long)st_calls[g]);
    LOG(", extra text draws %llu\n", (unsigned long long)st_flush);
}
