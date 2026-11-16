#ifndef HLE_3DS_WORLD_CLIP_H
#define HLE_3DS_WORLD_CLIP_H
#include "w3d_citro3d_adapter.h"
/* Shared CPU reference clipper. Fog must be evaluated on generated vertices, not interpolated
   from shader fog*W outputs: clipping and that nonlinear operation do not commute. */
static float near_plane_distance(const W3dC3dVertex *vertex)
{
    return vertex->clip[2] + vertex->clip[3];
}

static void interpolate_clip_vertex(const W3dC3dVertex *a, const W3dC3dVertex *b,
                                   float t, W3dC3dVertex *out)
{
    for (int i = 0; i < 4; ++i) out->clip[i] = a->clip[i] + (b->clip[i] - a->clip[i]) * t;
    for (int i = 0; i < 4; ++i) out->color[i] = a->color[i] + (b->color[i] - a->color[i]) * t;
    for (int i = 0; i < 2; ++i) out->uv[i] = a->uv[i] + (b->uv[i] - a->uv[i]) * t;
    out->view_z = a->view_z + (b->view_z - a->view_z) * t;
    out->clip[2] = -out->clip[3];
}

static unsigned clip_triangle_near(const W3dC3dVertex input[3], W3dC3dVertex output[4], int *crossed)
{
    unsigned count = 0;
    int inside_count = 0;
    for (int i = 0; i < 3; ++i)
        inside_count += near_plane_distance(&input[i]) >= 0.0f;
    *crossed = inside_count != 0 && inside_count != 3;
    if (!inside_count) return 0;

    for (int i = 0; i < 3; ++i) {
        const W3dC3dVertex *a = &input[i];
        const W3dC3dVertex *b = &input[(i + 1) % 3];
        const float da = near_plane_distance(a), db = near_plane_distance(b);
        const int a_inside = da >= 0.0f, b_inside = db >= 0.0f;
        if (a_inside) output[count++] = *a;
        if (a_inside != b_inside) {
            const float t = da / (da - db);
            interpolate_clip_vertex(a, b, t, &output[count++]);
        }
    }
    return count;
}

/* Input has already passed the finite check. Unfogged triangles and wholly inside/outside
   triangles retain GPU clipping; only visible fogged crossings require CPU-generated attributes. */
static inline int gpu_fog_needs_cpu_clip(int fog, float d0, float d1, float d2)
{
    if (!fog) return 0;
    const unsigned inside = (d0 >= 0.0f) + (d1 >= 0.0f) + (d2 >= 0.0f);
    return inside != 0 && inside != 3;
}

/* With GPU clipping, every triangle of a batch is copied unchanged when the conversion screen saw only finite
   values and, for a fogged batch, no vertex behind the near plane (so no fogged crossing exists). */
static inline int gpu_batch_copies_as_is(unsigned screen, int fog)
{
    if (screen & (W3D_C3D_SCREEN_NONFINITE | W3D_C3D_SCREEN_UNKNOWN)) return 0;
    return !(fog && (screen & W3D_C3D_SCREEN_BEHIND));
}
#endif
