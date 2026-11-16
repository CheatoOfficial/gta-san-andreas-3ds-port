/* Conversion screening that lets the GPU-clip present pass copy whole batches unchanged. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "w3d_citro3d_adapter.h"
#include "hle_3ds_world_clip.h"

static W3dCam camera;
static W3dState state;
static const float world[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};

/* Identity transform: clip = (x, y, z, 1), so a vertex is in front of the near plane when z >= -1. */
static unsigned screen_of(float z0, float z1, float z2, float u0)
{
    W3dVC in[3] = {{u0, 0, 0xFFFFFFFFu, 0, 0, z0}, {0, 0, 0xFFFFFFFFu, 1, 0, z1}, {0, 0, 0xFFFFFFFFu, 0, 1, z2}};
    W3dC3dVertex out[3];
    (void)w3d_citro3d_take_screen();
    assert(w3d_citro3d_convert_tris(&camera, world, &state, in, 3, out, 3) == 3);
    const unsigned screen = w3d_citro3d_take_screen();
    /* Cross-check against the per-triangle tests the present pass would otherwise run. */
    const float d[3] = {near_plane_distance(&out[0]), near_plane_distance(&out[1]), near_plane_distance(&out[2])};
    int finite = 1;
    for (int j = 0; j < 3; ++j)
        for (int k = 0; k < 4; ++k) finite &= isfinite(out[j].clip[k]) && isfinite(out[j].color[k]);
    for (int j = 0; j < 3; ++j) finite &= isfinite(out[j].uv[0]) && isfinite(out[j].uv[1]) && isfinite(out[j].view_z);
    if (gpu_batch_copies_as_is(screen, 1)) assert(finite && !gpu_fog_needs_cpu_clip(1, d[0], d[1], d[2]));
    if (gpu_batch_copies_as_is(screen, 0)) assert(finite);
    if (!finite) assert(screen & W3D_C3D_SCREEN_NONFINITE);
    return screen;
}

int main(void)
{
    camera.proj[0] = camera.proj[5] = camera.proj[10] = camera.proj[15] = 1;
    state.uscale = 1;

    assert(screen_of(0.5f, 2, 3, 0) == 0);
    assert(screen_of(-1, -1, -1, 0) == 0);                                  /* on the plane counts as in front */
    assert(screen_of(-2, 2, 3, 0) == W3D_C3D_SCREEN_BEHIND);               /* a fogged crossing */
    assert(screen_of(-2, -3, -4, 0) == W3D_C3D_SCREEN_BEHIND);             /* wholly behind */
    assert(screen_of(NAN, 2, 3, 0) == (W3D_C3D_SCREEN_NONFINITE | W3D_C3D_SCREEN_BEHIND));
    assert(screen_of(0, 2, 3, INFINITY) == W3D_C3D_SCREEN_NONFINITE);
    assert(screen_of(INFINITY, -INFINITY, 3, 0) & W3D_C3D_SCREEN_NONFINITE);
    assert(w3d_citro3d_take_screen() == 0);                                 /* taking resets */

    assert(gpu_batch_copies_as_is(0, 1) && gpu_batch_copies_as_is(0, 0));
    assert(gpu_batch_copies_as_is(W3D_C3D_SCREEN_BEHIND, 0));               /* unfogged: GPU clips it */
    assert(!gpu_batch_copies_as_is(W3D_C3D_SCREEN_BEHIND, 1));
    assert(!gpu_batch_copies_as_is(W3D_C3D_SCREEN_NONFINITE, 0));
    assert(!gpu_batch_copies_as_is(W3D_C3D_SCREEN_UNKNOWN, 0));
    puts("screen: ok");
    return 0;
}
