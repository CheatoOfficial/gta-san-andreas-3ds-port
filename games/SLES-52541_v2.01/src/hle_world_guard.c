/* hle_world_guard.c - a bound on the world-sector line walk 0x27E180.
 *
 * 0x27E180 walks the 120 x 50-unit world sectors along a line (sector = floor(coordinate / 50 + 60)) from the vector
 * at a0 to the vector at a1; one of its row loops (back-edge 0x27F580) counts from the sector row of one endpoint to
 * the other without clamping. A 3DS soak (scripted route, wanted level 2, running in the Unity-station tunnel) passed a
 * start vector (1.62e29, 0, 1): the saturated row index made the loop run ~2^31 times, and the main loop stopped at
 * virtual 315.76 s, deterministically, in every build tried (the stall watchdog's report). That vector is
 * uninitialised stack: see ped_target_point at the end of this file, which fixes the source.
 *
 * The guard: an endpoint whose x or y is not finite or lies more than 100 000 units out is clamped, in place, to just
 * outside the sector grid (+-3100), so the walk covers at most the 120 rows. Every endpoint inside the world (the
 * game's own coordinates stay within +-3000) is untouched, and the original function runs as before. It stays as a
 * backstop for any other path that hands the walk a wild endpoint. */
#include "hle.h"
#include <math.h>
#include <string.h>

void GUEST_FN(world_line_sectors)(R5900Ctx *c);

static uint64_t guarded;

static int clamp_component(uint32_t address)
{
    if (address + 4u > hle_ram_end || (address & 3u)) return 0;
    const uint32_t raw = gw(address);
    float v;
    memcpy(&v, &raw, sizeof v);
    if (isfinite(v) && v >= -100000.0f && v <= 100000.0f) return 0;
    const float clamped = !isfinite(v) ? 3100.0f : v < 0.0f ? -3100.0f : 3100.0f;
    uint32_t out;
    memcpy(&out, &clamped, sizeof out);
    sw_(address, out);
    return 1;
}

/* where an out-of-world endpoint came from: both endpoints before the clamp. The context's ra can be stale (translated
   code writes ra back only when it needs to), so the stack scan below is the better pointer to the caller. */
static void log_origin(R5900Ctx *c)
{
    LOG("world guard: context ra %08x sp %08x", (unsigned)c->r[31].d[0], (unsigned)c->r[29].d[0]);
    for (int r = 4; r <= 5; ++r) {
        const uint32_t vec = (uint32_t)c->r[r].d[0];
        LOG(" | a%d %08x", r - 4, vec);
        if (vec + 12u <= hle_ram_end && !(vec & 3u))
            for (int k = 0; k < 3; ++k) LOG(" %08x", gw(vec + 4u * (uint32_t)k));
    }
    /* probable call chain: words on the guest stack above sp that look like return addresses into the game's code
       (saved ra of the callers' frames), with their offsets from sp */
    const uint32_t sp = (uint32_t)c->r[29].d[0];
    LOG("\nworld guard: stack return addresses:");
    int found = 0;
    for (uint32_t at = sp; found < 12 && at + 4u <= hle_ram_end && at < sp + 0x800u; at += 4u) {
        const uint32_t w = gw(at);
        if (w >= 0x00100008u && w < 0x0066ba00u && !(w & 3u)) {
            LOG(" sp+%x:%08x", at - sp, w);
            ++found;
        }
    }
    LOG("\n");
}

HLE_FN(world_line_sectors)
{
    int fixed = 0;
    for (int r = 4; r <= 5; ++r) {
        const uint32_t vec = (uint32_t)c->r[r].d[0];
        for (int k = 0; k < 2 && !fixed; ++k) {
            const uint32_t a = vec + 4u * (uint32_t)k;
            if (a + 4u > hle_ram_end || (a & 3u)) continue;
            const uint32_t raw = gw(a);
            float v;
            memcpy(&v, &raw, sizeof v);
            if (!(isfinite(v) && v >= -100000.0f && v <= 100000.0f)) { if (guarded < 16) log_origin(c); fixed = -1; }
        }
    }
    fixed = 0;
    for (int r = 4; r <= 5; ++r) {
        const uint32_t vec = (uint32_t)c->r[r].d[0];
        fixed |= clamp_component(vec) | clamp_component(vec + 4u);
    }
    if (fixed && guarded++ < 16)
        LOG("world guard: line walk 0x27E180 got an endpoint far outside the world (vtime %.3f s); clamped (%llu)\n",
            (double)hle_vtime / EE_HZ, (unsigned long long)guarded);
    GUEST_FN(world_line_sectors)(c);
}

/* 0x50DAA0 (ped, entity, position a2, out a3) computes a point for a ped task and returns 1, but returns 0 on several
   paths without writing *a3. Two of its three callers (0x40DAB0 and 0x40E2F4, in the ped task 0x40D8D0..) ignore the
   result, add 1 to out.z and walk a line of sight from *a3 - so they read uninitialised stack. On this port that held
   70030c01 00000000 (x = 1.62e29) at virtual 315 s in the soak and the line walk ran ~2^31 rows. Pre-filling *a3 with
   the input position gives those callers a real point when nothing is written; when the original writes, it wins. */
void GUEST_FN(ped_target_point)(R5900Ctx *c);
static uint64_t target_unwritten;

HLE_FN(ped_target_point)
{
    const uint32_t from = (uint32_t)c->r[6].d[0], out = (uint32_t)c->r[7].d[0];
    const int ok = from + 12u <= hle_ram_end && out + 12u <= hle_ram_end && !(from & 3u) && !(out & 3u);
    if (ok)
        for (uint32_t k = 0; k < 12u; k += 4u) sw_(out + k, gw(from + k));
    GUEST_FN(ped_target_point)(c);
    if (ok && (uint32_t)c->r[2].d[0] == 0u && target_unwritten++ < 4)
        LOG("world guard: ped target point 0x50DAA0 returned 0 (vtime %.3f s); output pre-filled with the input position\n",
            (double)hle_vtime / EE_HZ);
}
