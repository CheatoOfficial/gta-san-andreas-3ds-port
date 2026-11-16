/* Static budget distance priority: compiled with a budget of three vertices (one triangle). */
#include "w3d_citro3d_backend.h"
#include "w3d_citro3d_depth.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int w3d_opt_cull = 1;
void hle_3ds_texture_frame_reset(void) {}

static W3dCam camera;
static W3dState state;
static W3dVF verts[3];
static const uint32_t prims[2] = {0x04030003u, 0x0B000000u};
static W3dMeshView mesh;

static void draw_at(float depth)
{
    const float world[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, depth};
    w3d_be_world(world);
    w3d_be_range(&mesh, NULL, 0, prims, 2);
}

static void frame(void)
{
    w3d_citro3d_frame_reset();
    assert(w3d_be_begin(&camera));
    w3d_be_state(&state);
    draw_at(200.0f);                                /* the game submits the far mesh first */
    draw_at(5.0f);
}

int main(void)
{
    /* Pure helpers. */
    assert(w3d_citro3d_depth_band(-3.0f) == 0 && w3d_citro3d_depth_band(9.9f) == 0);
    assert(w3d_citro3d_depth_band(10.0f) == 1 && w3d_citro3d_depth_band(1.0e9f) == W3D_C3D_DEPTH_BANDS - 1u);
    uint32_t estimate[W3D_C3D_DEPTH_BANDS] = {0};
    estimate[0] = 5; estimate[3] = 5; estimate[9] = 5;
    uint64_t allowance, used = 0;
    assert(w3d_citro3d_static_cut_band(estimate, 0, &allowance) == W3D_C3D_DEPTH_BANDS);   /* no budget */
    assert(w3d_citro3d_static_cut_band(estimate, 15, &allowance) == W3D_C3D_DEPTH_BANDS);
    assert(w3d_citro3d_static_cut_band(estimate, 10, &allowance) == 9 && allowance == 0);
    assert(w3d_citro3d_static_cut_band(estimate, 2, &allowance) == 1 && allowance == 0);   /* band 0 never cut */
    assert(w3d_citro3d_static_cut_band(estimate, 13, &allowance) == 9 && allowance == 3);
    assert(w3d_citro3d_static_admit(3, 9, 5, allowance, &used));                 /* nearer band: always */
    assert(w3d_citro3d_static_admit(9, 9, 2, allowance, &used) && used == 2);    /* cut band: while it fits */
    assert(!w3d_citro3d_static_admit(9, 9, 2, allowance, &used) && used == 2);
    assert(w3d_citro3d_static_admit(9, 9, 1, allowance, &used) && used == 3);
    assert(!w3d_citro3d_static_admit(10, 9, 0, allowance, &used));               /* beyond the cut: never */
    assert(w3d_citro3d_estimate_budget(10000, 0, 0) == 10000);                   /* no calibration yet */
    assert(w3d_citro3d_estimate_budget(10000, 16000, 10000) == 16000);           /* estimate runs 1.6x high */
    assert(w3d_citro3d_estimate_budget(10000, 100000, 10) == 40000);             /* clamped to 4x */
    assert(w3d_citro3d_estimate_budget(10000, 10, 100000) == 5000);              /* and to 0.5x */
    float fog = W3D_C3D_FOG_UNLIMITED;
    assert(w3d_citro3d_fog_limit_step(fog, W3D_C3D_DEPTH_BANDS, 450.0f) == W3D_C3D_FOG_UNLIMITED);
    fog = w3d_citro3d_fog_limit_step(fog, 15, 450.0f);                           /* 150 m: steps down from far */
    assert(fog == 450.0f - W3D_C3D_FOG_STEP_M);
    for (int i = 0; i < 200; ++i) fog = w3d_citro3d_fog_limit_step(fog, 15, 450.0f);
    assert(fog == 150.0f);
    fog = w3d_citro3d_fog_limit_step(fog, 16, 450.0f);                           /* small cut moves are smooth */
    assert(fog == 150.0f + W3D_C3D_FOG_STEP_M);
    fog = w3d_citro3d_fog_limit_step(fog, W3D_C3D_DEPTH_BANDS, 450.0f);
    assert(fog == 150.0f + 2.0f * W3D_C3D_FOG_STEP_M);
    for (int i = 0; i < 200; ++i) fog = w3d_citro3d_fog_limit_step(fog, W3D_C3D_DEPTH_BANDS, 450.0f);
    assert(fog == W3D_C3D_FOG_UNLIMITED);
    assert(w3d_citro3d_fog_limit_step(W3D_C3D_FOG_UNLIMITED, 2, 0.0f) == 1000.0f - W3D_C3D_FOG_STEP_M);

    /* Backend: one triangle per draw, budget three vertices. */
    camera.proj[0] = camera.proj[5] = camera.proj[10] = camera.proj[15] = 1;
    camera.far_z = 450.0f; camera.fog_z = 36.0f;
    state.uscale = 1;
    for (int i = 0; i < 3; ++i) verts[i].c = 0xFFFFFFFFu;
    mesh.verts = (const uint8_t *)verts; mesh.nverts = 3; mesh.nprims = 2; mesh.ntris = 1;
    mesh.stride = sizeof(verts[0]); mesh.fmt = W3D_FMT_UVF;

    frame();                 /* no history yet: draw order decides, the far mesh takes the budget */
    assert(w3d_citro3d_frame_batch_count() == 1 && w3d_citro3d_frame_batches()[0].far_z == 450.0f);
    assert(w3d_citro3d_budget_skipped() == 1);
    frame();                 /* the cut now starts at 200 m: the near mesh is drawn instead */
    assert(w3d_citro3d_frame_batch_count() == 1 && w3d_citro3d_budget_skipped() == 0);
    const W3dC3dBatch *batch = &w3d_citro3d_frame_batches()[0];
    assert(w3d_citro3d_frame_vertices()[batch->first_vertex].view_z == 5.0f);
    assert(batch->far_z < 450.0f);                   /* the fog limit has started moving toward the cut */
    puts("3DS static budget keeps near meshes and fades the distance cut");
    return 0;
}
