/* Synthetic near-plane/fog regression; no disc or GPU is needed. */
#include "hle_3ds_world_clip.h"
#include "hle_3ds_fog.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static float fog_s(const W3dC3dVertex *v)
{
    /* Exact coordinate construction in vshader.v.pica, before perspective projection. */
    return (0.99609375f * hle_3ds_fog_factor(1, v->view_z, 100, 50) + 0.001953125f) * v->clip[3];
}

int main(void)
{
    /* Every inside/outside permutation, including boundary and signed zero. */
    for (unsigned mask = 0; mask < 8; ++mask) {
        float d[3];
        for (int i = 0; i < 3; ++i) d[i] = mask & (1u << i) ? 1 : -1;
        assert(gpu_fog_needs_cpu_clip(1,d[0],d[1],d[2]) == (mask != 0 && mask != 7));
        assert(!gpu_fog_needs_cpu_clip(0,d[0],d[1],d[2]));
    }
    assert(gpu_fog_needs_cpu_clip(1,-1,0,-0.0f));
    assert(!gpu_fog_needs_cpu_clip(1,0,-0.0f,1));

    /* Positive W throughout: one vertex lies just behind near=.1, two at far=100.
       No negative-W/eye-plane ambiguity is needed to expose the bug. */
    W3dC3dVertex v[3] = {0}, clipped[4];
    const float z[3] = {0.05f,100,100};
    for (int i = 0; i < 3; ++i) {
        v[i].view_z = v[i].clip[3] = z[i];
        v[i].clip[2] = (100.1f / 99.9f) * z[i] - (20.0f / 99.9f);
        v[i].uv[0] = (float)i; v[i].color[0] = (float)i * 0.25f;
        v[i].color[3] = 1;
    }
    /* Non-degenerate visible geometry, wholly inside the side planes. */
    v[0].clip[0]=-0.01f;
    v[1].clip[0]=v[2].clip[0]=50;
    v[1].clip[1]=-50; v[2].clip[1]=50;
    const float da=near_plane_distance(&v[0]), db=near_plane_distance(&v[1]);
    assert(gpu_fog_needs_cpu_clip(1,da,db,near_plane_distance(&v[2])));
    const float t = da / (da-db);
    const float gpu_s = fog_s(&v[0]) + (fog_s(&v[1])-fog_s(&v[0])) * t;
    const float gpu_q = v[0].clip[3] + (v[1].clip[3]-v[0].clip[3]) * t;
    int crossed=0;
    assert(clip_triangle_near(v,clipped,&crossed) == 4 && crossed);
    const W3dC3dVertex *edge = &clipped[0];
    assert(fabsf(edge->view_z - 0.1f) < 0.00001f);
    assert(fabsf(near_plane_distance(edge)) < 0.00001f);
    assert(hle_3ds_fog_factor(1,edge->view_z,100,50) == 1);
    const float cpu_coordinate = fog_s(edge) / edge->clip[3];
    assert(cpu_coordinate - gpu_s/gpu_q > 0.49f); /* old prototype fogs this near edge by about 50% */

    /* The fallback uses the real reference clipper and evaluates fog after generation.
       A wholly-inside triangle preserves its original vertices, including attributes. */
    for (int i=0; i<3; ++i) v[i].clip[2] = 0;
    assert(!gpu_fog_needs_cpu_clip(1,near_plane_distance(&v[0]),near_plane_distance(&v[1]),near_plane_distance(&v[2])));
    assert(clip_triangle_near(v,clipped,&crossed) == 3 && !crossed);
    assert(memcmp(v,clipped,sizeof(v)) == 0);
    puts("GPU fog crossing regression: projective mismatch reproduced, fallback selection and reference vertices passed");
    return 0;
}
