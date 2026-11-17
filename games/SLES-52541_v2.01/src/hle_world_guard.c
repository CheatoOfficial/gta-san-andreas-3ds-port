/* hle_world_guard.c - a bound on the world-sector line walk 0x27E180.
 *
 * 0x27E180 walks the 120 x 50-unit world sectors along a line (sector = floor(coordinate / 50 + 60)) from the vector
 * at a0 to the vector at a1; one of its row loops (back-edge 0x27F580) counts from the sector row of one endpoint to
 * the other without clamping. A 3DS soak (scripted route, wanted level 2, running in the Unity-station tunnel) passed a
 * start vector (1.62e29, 0, 1): the saturated row index made the loop run ~2^31 times, and the main loop stopped at
 * virtual 315.76 s, deterministically, in every build tried (the stall watchdog's report). Where that vector comes from
 * is not known yet (possibly an uninitialised stack vector whose leftover contents differ from a PS2).
 *
 * The guard: an endpoint whose x or y is not finite or lies more than 100 000 units out is clamped, in place, to just
 * outside the sector grid (+-3100), so the walk covers at most the 120 rows. Every endpoint inside the world (the
 * game's own coordinates stay within +-3000) is untouched, and the original function runs as before. */
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

HLE_FN(world_line_sectors)
{
    int fixed = 0;
    for (int r = 4; r <= 5; ++r) {
        const uint32_t vec = (uint32_t)c->r[r].d[0];
        fixed |= clamp_component(vec) | clamp_component(vec + 4u);
    }
    if (fixed && guarded++ < 16)
        LOG("world guard: line walk 0x27E180 got an endpoint far outside the world (vtime %.3f s); clamped (%llu)\n",
            (double)hle_vtime / EE_HZ, (unsigned long long)guarded);
    GUEST_FN(world_line_sectors)(c);
}
