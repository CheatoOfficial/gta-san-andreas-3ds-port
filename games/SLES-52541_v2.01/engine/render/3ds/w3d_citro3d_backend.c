/* Citro3D implementation of the PS2RC world-backend interface. The adapter decodes PS2 meshes and lighting into
 * frame batches; this file binds the cached textures and maps render state to PICA. GE segment lists remain unsupported. */
#include "w3d_citro3d_backend.h"
#include "w3d_citro3d_depth.h"
#include "hle_w3d_me.h"
#include "hle_3ds_gfx.h"
#include "hle_3ds_texture.h"
#include "hle.h"
#include "hle_plat.h"
#include <3ds.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W3D_C3D_SCRATCH_BYTES (512u * 1024u)
#ifndef HLE_3DS_RENDER_DIAG
#define HLE_3DS_RENDER_DIAG 0
#endif
#ifndef HLE_3DS_GPU_CLIP
#define HLE_3DS_GPU_CLIP 0
#endif
/* HLE_3DS_GPU_LIGHT: lit meshes are transformed and lit by the vertex shader (needs GPU clipping). */
#ifndef HLE_3DS_GPU_LIGHT
#define HLE_3DS_GPU_LIGHT 0
#endif
#if HLE_3DS_GPU_LIGHT && !HLE_3DS_GPU_CLIP
#error HLE_3DS_GPU_LIGHT needs HLE_3DS_GPU_CLIP
#endif
static W3dGpuLight *gpu_lights;           /* one per batch slot (HLE_3DS_GPU_LIGHT) */
static unsigned gpu_lit_batches, gpu_lit_fallbacks, gpu_static_batches, gpu_static_fallbacks;
#ifndef HLE_3DS_CAPTURE_START_FRAME
#define HLE_3DS_CAPTURE_START_FRAME 0
#endif

static W3dC3dVertex *frame_vertices;
static W3dVC *temporary_vertices;
static W3dC3dBatch *frame_batches;
static uint8_t scratch[W3D_C3D_SCRATCH_BYTES] __attribute__((aligned(16)));
static W3dCam current_camera, camera_base;
static float current_world[12];
static W3dState current_state, current_lit_state, current_draw_state;
static W3dLights current_lights;
static float current_bones[8][12];
static unsigned frame_vertex_count, scratch_used;
#ifndef W3D_C3D_STATIC_VERT_BUDGET
#define W3D_C3D_STATIC_VERT_BUDGET (hle_3ds_active_profile->static_budget) /* 0 = unlimited */
#endif
static unsigned frame_static_verts, budget_skipped;
/* Distance priority for the static budget (w3d_citro3d_depth.h): estimated vertices of this frame's static draw
   attempts per depth band, the cut band chosen from the previous frame, and the fog limit that hides it. */
static uint32_t static_band_estimate[W3D_C3D_DEPTH_BANDS];
static uint64_t static_estimated_drawn, static_written_drawn;   /* calibration of the estimate, this frame */
static uint64_t cut_band_allowance, cut_band_used;
static unsigned static_cut_band = W3D_C3D_DEPTH_BANDS, distance_skipped;
static float fog_far_limit = W3D_C3D_FOG_UNLIMITED;
static unsigned frame_batch_count;
static unsigned skipped_textures, simplified_states, skipped_segments, skipped_meshes;
static int current_bone_count;
static uint32_t current_raster;
static int current_filter, current_address_u, current_address_v;
static unsigned epoch;
static int frame_error, has_camera, has_world, has_state;
#if HLE_3DS_RENDER_DIAG
static unsigned diag_camera_frame = ~0u, diag_world_frame = ~0u, diag_state_frame = ~0u;
static unsigned diag_texture_frame = ~0u, diag_proj_count_frame = ~0u;
static void diag_matrix(const char *label, unsigned frame, const float *m, unsigned count)
{
    LOG("Citro3D diag %s frame %u:", label, frame);
    for (unsigned i = 0; i < count; ++i) LOG(" %.6f", m[i]);
    LOG("\n");
}
#endif

static int reserve_vertices(unsigned count, W3dC3dVertex **out)
{
    if (count > W3D_C3D_FRAME_VERTEX_CAPACITY - frame_vertex_count) {
        ++skipped_meshes;
        return 0;
    }
    *out = frame_vertices + frame_vertex_count;
    frame_vertex_count += count;
    return 1;
}

static void record_batch_light(unsigned first_vertex, unsigned vertex_count, unsigned screen, int gpu_light);
static void record_batch(unsigned first_vertex, unsigned vertex_count, unsigned screen)
{
    record_batch_light(first_vertex, vertex_count, screen, -1);
}
static void record_batch_light(unsigned first_vertex, unsigned vertex_count, unsigned screen, int gpu_light)
{
    if (!vertex_count) return;
    if (frame_batch_count >= W3D_C3D_BATCH_CAPACITY) {
        ++skipped_meshes;
        return;
    }
    W3dC3dBatch *batch = &frame_batches[frame_batch_count++];
    batch->first_vertex = first_vertex;
    batch->vertex_count = vertex_count;
    batch->raster = current_raster;
    batch->filter = current_filter;
    batch->address_u = current_address_u;
    batch->address_v = current_address_v;
    batch->state = current_draw_state;
    batch->far_z = current_camera.far_z;
    batch->fog_z = current_camera.fog_z;
    if (fog_far_limit < batch->far_z) {          /* fade out at the static distance cut instead of popping */
        /* Never pull full fog nearer than W3D_C3D_FOG_FLOOR_M: a dense compact-profile scene can cut at 40 m,
           and fog that close turns the street into a wall. Between the cut and the floor, meshes just stop. */
        const float floor_z = batch->fog_z + 20.0f > W3D_C3D_FOG_FLOOR_M ? batch->fog_z + 20.0f : W3D_C3D_FOG_FLOOR_M;
        batch->far_z = fog_far_limit > floor_z ? fog_far_limit : floor_z;
        if (batch->far_z > current_camera.far_z) batch->far_z = current_camera.far_z;
    }
    batch->screen = screen;
    batch->gpu_light = gpu_light;
}

int w3d_citro3d_init(void)
{
    if (frame_vertices) return 1;
    frame_vertices=malloc(sizeof(*frame_vertices)*W3D_C3D_FRAME_VERTEX_CAPACITY);
    temporary_vertices=malloc(sizeof(*temporary_vertices)*W3D_C3D_TEMP_VERTEX_CAPACITY);
    frame_batches=malloc(sizeof(*frame_batches)*W3D_C3D_BATCH_CAPACITY);
#if HLE_3DS_GPU_LIGHT
    gpu_lights=malloc(sizeof(*gpu_lights)*W3D_C3D_BATCH_CAPACITY);
    if (!gpu_lights) { w3d_citro3d_shutdown(); return 0; }
    /* converted non-skinned lit ranges kept across frames (adapter); the compact (Old 3DS) profile has no room */
    w3d_citro3d_lit_cache_config(hle_3ds_active_profile->compact ? 0u : 5u * 1024u * 1024u);
#endif
    if (frame_vertices && temporary_vertices && frame_batches) return 1;
    w3d_citro3d_shutdown(); return 0;
}
void w3d_citro3d_shutdown(void)
{
    free(frame_vertices); frame_vertices=NULL;
    free(temporary_vertices); temporary_vertices=NULL;
    free(frame_batches); frame_batches=NULL;
    free(gpu_lights); gpu_lights=NULL;
    frame_vertex_count=frame_batch_count=0;
}
void w3d_citro3d_frame_reset(void)
{
    if (!w3d_citro3d_init()) { frame_error=-1; return; }
    frame_vertex_count = 0;
    frame_static_verts = 0;
    static_cut_band = w3d_citro3d_static_cut_band(static_band_estimate,
        w3d_citro3d_estimate_budget(W3D_C3D_STATIC_VERT_BUDGET, static_estimated_drawn, static_written_drawn),
        &cut_band_allowance);
    cut_band_used = 0;
    static_estimated_drawn = static_written_drawn = 0;
    fog_far_limit = w3d_citro3d_fog_limit_step(fog_far_limit, static_cut_band, current_camera.far_z);
    memset(static_band_estimate, 0, sizeof static_band_estimate);
    frame_batch_count = 0;
    scratch_used = 0;
    skipped_textures = simplified_states = skipped_segments = skipped_meshes = 0;
    frame_error = 0;
    has_camera = has_world = has_state = 0;
    ++epoch;
    hle_3ds_texture_frame_reset();
}

const W3dC3dVertex *w3d_citro3d_frame_vertices(void) { return frame_vertices; }
int w3d_citro3d_frame_vertex_count(void) { return (int)frame_vertex_count; }
const W3dC3dBatch *w3d_citro3d_frame_batches(void) { return frame_batches; }
int w3d_citro3d_frame_batch_count(void) { return (int)frame_batch_count; }
int w3d_citro3d_frame_error(void) { return frame_error; }
const W3dGpuLight *w3d_citro3d_frame_gpu_light(int index)
{
    return gpu_lights && index >= 0 && (unsigned)index < frame_batch_count ? &gpu_lights[index] : NULL;
}
static uint64_t conv_ticks[2], conv_verts[2], conv_calls[2];
void w3d_citro3d_log_static_cut(unsigned frames)
{
#if HLE_3DS_GPU_LIGHT
    uint64_t sc[4];
    w3d_citro3d_static_cache_stats(sc);
    LOG("Citro3D GPU lighting (%u frames, per frame): %u batches, %u CPU fallbacks | GPU static %u batches, %u CPU "
        "fallbacks, cache %llu hits %llu verts / %llu misses %llu verts\n", frames,
        gpu_lit_batches / frames, gpu_lit_fallbacks / frames, gpu_static_batches / frames, gpu_static_fallbacks / frames,
        (unsigned long long)(sc[0] / frames), (unsigned long long)(sc[2] / frames), (unsigned long long)(sc[1] / frames),
        (unsigned long long)(sc[3] / frames));
    gpu_lit_batches = gpu_lit_fallbacks = gpu_static_batches = gpu_static_fallbacks = 0;
#endif
    LOG("Citro3D static distance cut (%u frames, per frame): %u meshes skipped, cut now %s%u m, fog limit %.0f m\n",
        frames, distance_skipped / frames, static_cut_band < W3D_C3D_DEPTH_BANDS ? "" : "none ",
        static_cut_band < W3D_C3D_DEPTH_BANDS ? (unsigned)(static_cut_band * W3D_C3D_DEPTH_BAND_M) : 0u,
        fog_far_limit < W3D_C3D_FOG_UNLIMITED ? fog_far_limit : 0.0f);
    distance_skipped = 0;
}
void w3d_citro3d_take_conv_stats(uint64_t ticks[2], uint64_t verts[2], uint64_t calls[2])
{
    for (int i = 0; i < 2; ++i) { ticks[i] = conv_ticks[i]; verts[i] = conv_verts[i]; calls[i] = conv_calls[i]; conv_ticks[i] = conv_verts[i] = conv_calls[i] = 0; }
}
unsigned w3d_citro3d_budget_skipped(void) { const unsigned n = budget_skipped; budget_skipped = 0; return n; }
unsigned w3d_citro3d_frame_textures_skipped(void) { return skipped_textures; }
unsigned w3d_citro3d_frame_states_simplified(void) { return simplified_states; }
unsigned w3d_citro3d_frame_segments_skipped(void) { return skipped_segments; }
unsigned w3d_citro3d_frame_meshes_skipped(void) { return skipped_meshes; }

int w3d_be_begin(const W3dCam *camera)
{
    if (!camera) { frame_error = -1; return 0; }
    current_camera = camera_base = *camera;
#if HLE_3DS_RENDER_DIAG
    const unsigned frame = hle_3ds_gfx_frame_for_draw();
    if (frame == HLE_3DS_CAPTURE_START_FRAME && diag_camera_frame != frame) {
        diag_camera_frame = frame;
        LOG("Citro3D diag camera frame %u: guest=%08x near=%.6f far=%.6f fog=%.6f\n",
            frame, camera->camera, camera->near_z, camera->far_z, camera->fog_z);
        diag_matrix("camera-projection", frame, camera->proj, 16);
        hle_plat_log_flush();
    }
#endif
    memset(&current_lights, 0, sizeof current_lights);
    current_bone_count = 0;
    has_camera = 1;
    has_world = has_state = 0;
    return 1;
}

int w3d_be_target(void) { return hle_3ds_gfx_ready(); }

void w3d_be_world(const float matrix[12])
{
    if (!matrix) { frame_error = -1; return; }
    memcpy(current_world, matrix, sizeof current_world);
#if HLE_3DS_RENDER_DIAG
    const unsigned frame = hle_3ds_gfx_frame_for_draw();
    if (frame == HLE_3DS_CAPTURE_START_FRAME && diag_world_frame != frame) {
        diag_world_frame = frame;
        diag_matrix("first-world", frame, matrix, 12);
        hle_plat_log_flush();
    }
#endif
    has_world = 1;
}

void w3d_be_state(const W3dState *state)
{
    if (!state) { frame_error = -1; return; }
    current_lit_state = *state;
    current_draw_state = *state;
#if HLE_3DS_RENDER_DIAG
    const unsigned frame = hle_3ds_gfx_frame_for_draw();
    if (frame == HLE_3DS_CAPTURE_START_FRAME && diag_state_frame != frame) {
        diag_state_frame = frame;
        LOG("Citro3D diag state frame %u: ztest=%d zwrite=%d cull=%d fog=%d blend=%d atest=%d light=%d lights=%d lamb=%08x lemis=%08x\n",
            frame, state->ztest, state->zwrite, state->cull, state->fog, state->blend, state->atest,
            state->light, state->lights, state->lamb, state->lemis);
        hle_plat_log_flush();
    }
#endif
    current_state = current_lit_state;
    /* Texture sampling, lighting, blend, alpha test, and depth are applied by the batch renderer.
       Fog and culling are mapped from each batch state when it is drawn. */
    if (current_lit_state.cull && !w3d_opt_cull)
        ++simplified_states;
    /* Keep vertex colour and PS2RC UV transform; texture and state are carried by the draw batch. */
    current_state.textured = 0;
    current_state.lights = 0;
    current_state.blend = 0;
    current_state.atest = 0;
    current_state.ztest = current_state.zwrite = current_state.cull = current_state.fog = 0;
    has_state = 1;
}

void w3d_be_range(const W3dMeshView *mesh, const uint8_t *verts_override, uint32_t first_vert,
                  const uint32_t *prims, uint32_t nprims)
{
    if (!has_camera || !has_world || !has_state || frame_error) {
        if (!frame_error) frame_error = -1;
        return;
    }
    const int is_lit = mesh && mesh->fmt == W3D_FMT_LIT;
    uint64_t static_estimate = 0;
    if (W3D_C3D_STATIC_VERT_BUDGET && !is_lit && mesh) {
        /* Every attempt counts toward next frame's cut, drawn or not; nprims/nprims of the mesh estimates a range. */
        const unsigned band = w3d_citro3d_depth_band(current_world[11]);
        const uint64_t estimate = mesh->nprims && nprims < mesh->nprims
            ? (uint64_t)mesh->ntris * 3u * nprims / mesh->nprims : (uint64_t)mesh->ntris * 3u;
        static_band_estimate[band] += estimate > UINT32_MAX - static_band_estimate[band]
            ? UINT32_MAX - static_band_estimate[band] : (uint32_t)estimate;
        static_estimate = estimate;
        if (!w3d_citro3d_static_admit(band, static_cut_band, estimate, cut_band_allowance, &cut_band_used)) {
            ++distance_skipped;
            return;
        }
    }
    if (W3D_C3D_STATIC_VERT_BUDGET && !is_lit && frame_static_verts >= W3D_C3D_STATIC_VERT_BUDGET) {
        ++budget_skipped;
        return;
    }
    /* A rejected static draw must not reserve the rest of the buffer: later lit draws still need it. */
    W3dC3dVertex *out;
    const unsigned frame_start = frame_vertex_count;
    if (!reserve_vertices(W3D_C3D_FRAME_VERTEX_CAPACITY - frame_vertex_count, &out)) return;
    const int cap = (int)(W3D_C3D_FRAME_VERTEX_CAPACITY - (unsigned)(out - frame_vertices));
    (void)w3d_citro3d_take_screen();
    const uint64_t conv_t0 = svcGetSystemTick();
    int written, gpu_light = -1;
    unsigned screen = 0;
#if HLE_3DS_GPU_LIGHT
    /* GPU-lit: object-space vertices + the batch's lighting; the CPU path redoes the call when a vertex is not
       finite or a fogged batch reaches behind the near plane (the present pass must clip it on the CPU). */
    if (is_lit && gpu_lights && frame_batch_count < W3D_C3D_BATCH_CAPACITY) {
        W3dGpuLight *light = &gpu_lights[frame_batch_count];
        written = w3d_citro3d_convert_lit_range_gpu(&current_camera, current_world, &current_state,
                                                    &current_lit_state, &current_lights, current_bone_count,
                                                    current_bones, mesh, verts_override, first_vert, prims, nprims,
                                                    out, cap, light);
        screen = w3d_citro3d_take_screen();
        if (written > 0 && !((screen & W3D_C3D_SCREEN_NONFINITE) ||
                             (current_draw_state.fog && (screen & W3D_C3D_SCREEN_BEHIND)))) {
            gpu_light = (int)frame_batch_count;
            ++gpu_lit_batches;
        } else if (written > 0) {
            ++gpu_lit_fallbacks;
            written = w3d_citro3d_convert_lit_range(&current_camera, current_world, &current_state,
                                                   &current_lit_state, &current_lights, current_bone_count,
                                                   current_bones, mesh, verts_override, first_vert, prims, nprims,
                                                   out, cap);
            screen = w3d_citro3d_take_screen();
        }
    } else if (!is_lit && mesh && gpu_lights && frame_batch_count < W3D_C3D_BATCH_CAPACITY) {
        /* GPU static: object-space vertices, camera x world and the material step in the shader (objmode); the
           same CPU fallback as GPU-lit */
        W3dGpuLight *object = &gpu_lights[frame_batch_count];
        written = w3d_citro3d_convert_mesh_range_gpu(&current_camera, current_world, &current_state, mesh,
                                                     verts_override, first_vert, prims, nprims, out, cap, object);
        screen = w3d_citro3d_take_screen();
        if (written > 0 && !((screen & W3D_C3D_SCREEN_NONFINITE) ||
                             (current_draw_state.fog && (screen & W3D_C3D_SCREEN_BEHIND)))) {
            gpu_light = (int)frame_batch_count;
            ++gpu_static_batches;
        } else if (written > 0) {
            ++gpu_static_fallbacks;
            written = w3d_citro3d_convert_mesh_range(&current_camera, current_world, &current_state, mesh,
                                                     verts_override, first_vert, prims, nprims, out, cap);
            screen = w3d_citro3d_take_screen();
        }
    } else
#endif
    {
        written = mesh && mesh->fmt == W3D_FMT_LIT
            ? w3d_citro3d_convert_lit_range(&current_camera, current_world, &current_state, &current_lit_state,
                                           &current_lights, current_bone_count, current_bones, mesh, verts_override,
                                           first_vert, prims, nprims, out, cap)
            : w3d_citro3d_convert_mesh_range(&current_camera, current_world, &current_state, mesh, verts_override,
                                            first_vert, prims, nprims, out, cap);
        screen = w3d_citro3d_take_screen();
    }
    conv_ticks[is_lit] += svcGetSystemTick() - conv_t0;
    if (written > 0) { conv_verts[is_lit] += (uint64_t)written; ++conv_calls[is_lit]; if (!is_lit) frame_static_verts += (unsigned)written; }
    if (written > 0 && static_estimate) { static_estimated_drawn += static_estimate; static_written_drawn += (uint64_t)written; }
    if (written < 0) {
        frame_vertex_count = frame_start;
        if (written == -3 || written == -4 || written == -5) {
            ++skipped_meshes;
            static int first_skip_logged;
            if (!first_skip_logged) {
                first_skip_logged = 1;
                LOG("Citro3D skipped mesh: error=%d fmt=%u stride=%u vtype=%08x nverts=%u nprims=%u first=%u words=%08x\n",
                    written, mesh ? mesh->fmt : 255u, mesh ? mesh->stride : 0u, mesh ? mesh->vtype : 0u,
                    mesh ? mesh->nverts : 0u, nprims, first_vert, prims && nprims ? prims[0] : 0u);
                hle_plat_log_flush();
            }
            return;
        }
        frame_error = written;
        return;
    }
    frame_vertex_count = (unsigned)(out - frame_vertices) + (unsigned)written;
    record_batch_light(frame_start, (unsigned)written, screen, gpu_light);
}

void w3d_be_mesh(const W3dMeshView *mesh, const uint8_t *verts_override)
{
    if (!mesh || !mesh->sub || mesh->nsub < 2 || mesh->nprims > mesh->nsub - 2) {
        frame_error = -1;
        return;
    }
    w3d_be_range(mesh, verts_override, 0, mesh->sub + 1, mesh->nprims);
}

static int mesh_vertex(const W3dMeshView *mesh, const uint8_t *verts, uint16_t index, W3dVC *out)
{
    if (index >= mesh->nverts) return 0;
    if (mesh->fmt == W3D_FMT_UV16 && mesh->stride >= sizeof(W3dV16)) {
        const W3dV16 *v = (const W3dV16 *)(verts + (size_t)index * mesh->stride);
        out->u = (float)v->u / 32768.0f;
        out->v = (float)v->v / 32768.0f;
        out->c = v->c;
        out->x = (float)v->x / 32768.0f;
        out->y = (float)v->y / 32768.0f;
        out->z = (float)v->z / 32768.0f;
        return 1;
    }
    if (mesh->fmt == W3D_FMT_UVF && mesh->stride >= sizeof(W3dVF)) {
        const W3dVF *v = (const W3dVF *)(verts + (size_t)index * mesh->stride);
        out->u = v->u; out->v = v->v; out->c = v->c;
        out->x = (float)v->x / 32768.0f;
        out->y = (float)v->y / 32768.0f;
        out->z = (float)v->z / 32768.0f;
        return 1;
    }
    return 0;
}

const void *w3d_be_indexed(const W3dMeshView *mesh, const uint8_t *verts_override, const uint16_t *indices, int n)
{
    if (!mesh || !indices || n < 3 || (unsigned)n > W3D_C3D_TEMP_VERTEX_CAPACITY || !has_camera || !has_world ||
        !has_state || frame_error) return NULL;
    const uint8_t *verts = verts_override ? verts_override : mesh->verts;
    for (int i = 0; i < n; ++i)
        if (!mesh_vertex(mesh, verts, indices[i], &temporary_vertices[i])) { frame_error = -4; return NULL; }
    W3dC3dVertex *out;
    if (!reserve_vertices((unsigned)n, &out)) return NULL;
    /* Convert into a temporary block first because the public converter requires triangle-list input/output blocks. */
    const int at = (int)(out - frame_vertices);
    frame_vertex_count -= (unsigned)n;
    (void)w3d_citro3d_take_screen();
    const int converted = w3d_citro3d_convert_tris(&current_camera, current_world, &current_state,
                                                   temporary_vertices, n, out, (int)(W3D_C3D_FRAME_VERTEX_CAPACITY - at));
    if (converted < 0) { frame_vertex_count = (unsigned)at; frame_error = converted; return NULL; }
    frame_vertex_count = (unsigned)at + (unsigned)converted;
    record_batch((unsigned)at, (unsigned)converted, w3d_citro3d_take_screen());
    return out;
}

const void *w3d_be_tris(const W3dVC *vertices, int n)
{
    if (!vertices || n < 3 || (unsigned)n > W3D_C3D_FRAME_VERTEX_CAPACITY || !has_camera || !has_world || !has_state || frame_error)
        return NULL;
    W3dC3dVertex *out;
    if (!reserve_vertices((unsigned)n, &out)) return NULL;
    const int at = (int)(out - frame_vertices);
    frame_vertex_count -= (unsigned)n;
    (void)w3d_citro3d_take_screen();
    const int converted = w3d_citro3d_convert_tris(&current_camera, current_world, &current_state, vertices, n, out,
                                                   (int)(W3D_C3D_FRAME_VERTEX_CAPACITY - at));
    if (converted < 0) { frame_vertex_count = (unsigned)at; frame_error = converted; return NULL; }
    frame_vertex_count = (unsigned)at + (unsigned)converted;
    record_batch((unsigned)at, (unsigned)converted, w3d_citro3d_take_screen());
    return out;
}

void w3d_be_indexed_again(const W3dMeshView *mesh, const uint8_t *verts_override, const void *copy, int n)
{
    (void)mesh; (void)verts_override;
    if (!copy || n < 3) return;
    W3dC3dVertex *out;
    const unsigned at = frame_vertex_count;
    if (!reserve_vertices((unsigned)n, &out)) return;
    memcpy(out, copy, (size_t)n * sizeof(*out));
    record_batch(at, (unsigned)n, W3D_C3D_SCREEN_UNKNOWN);   /* copies are not re-screened */
}

void w3d_be_tris_again(const void *copy, int n)
{
    if (!copy || n < 3) return;
    W3dC3dVertex *out;
    const unsigned at = frame_vertex_count;
    if (!reserve_vertices((unsigned)n, &out)) return;
    memcpy(out, copy, (size_t)n * sizeof(*out));
    record_batch(at, (unsigned)n, W3D_C3D_SCREEN_UNKNOWN);   /* copies are not re-screened */
}

void w3d_be_prims(uint32_t vtype, const void *vertices, int n)
{
    if (n < 3 || !vertices || !has_camera || !has_state || frame_error ||
        vtype != (3u | (3u << 5) | (3u << 7)) || (unsigned)n > W3D_C3D_TEMP_VERTEX_CAPACITY) return;
    const float *p = vertices;
    for (int i = 0; i < n; ++i) {
        W3dVC *v = &temporary_vertices[i];
        v->u = p[i * 8 + 0]; v->v = p[i * 8 + 1];
        v->c = 0xFFFFFFFFu;
        v->x = p[i * 8 + 5]; v->y = p[i * 8 + 6]; v->z = p[i * 8 + 7];
    }
    w3d_be_tris(temporary_vertices, n);
}

void w3d_be_proj(const float *projection)
{
    if (!has_camera) return;
    if (projection) memcpy(current_camera.proj, projection, sizeof current_camera.proj);
    else memcpy(current_camera.proj, camera_base.proj, sizeof current_camera.proj);
#if HLE_3DS_RENDER_DIAG
    const unsigned frame = hle_3ds_gfx_frame_for_draw();
    if (frame == HLE_3DS_CAPTURE_START_FRAME && diag_proj_count_frame != frame) {
        diag_proj_count_frame = frame;
        diag_matrix("projection-override", frame, current_camera.proj, 16);
        hle_plat_log_flush();
    }
#endif
}

void w3d_be_lights(const W3dLights *lights)
{
    if (lights) current_lights = *lights;
}
void w3d_be_bones(int n, const float (*matrices)[12])
{
    if (n < 0) n = 0;
    if (n > 8) n = 8;
    current_bone_count = n;
    if (n && matrices) memcpy(current_bones, matrices, sizeof current_bones[0] * (size_t)n);
}
void w3d_be_end(void) { has_camera = has_world = has_state = 0; }
void w3d_be_kick(void) {}
uint32_t w3d_be_epoch(void) { return epoch; }
void w3d_be_hold(int on) { (void)on; }
void w3d_be_forget(void) { ++epoch; }

int w3d_be_texture(uint32_t raster, int filter, int address_u, int address_v, int *alpha_class)
{
    if (alpha_class) *alpha_class = 0;
    current_filter = filter;
    current_address_u = address_u;
    current_address_v = address_v;
    current_raster = 0;
#if HLE_3DS_RENDER_DIAG
    const unsigned frame = hle_3ds_gfx_frame_for_draw();
    if (frame == HLE_3DS_CAPTURE_START_FRAME && diag_texture_frame != frame) {
        diag_texture_frame = frame;
        LOG("Citro3D diag texture frame %u: raster=%08x filter=%d address=%d/%d\n",
            frame, raster, filter, address_u, address_v);
        hle_plat_log_flush();
    }
#endif
    if (raster) {
        int alpha = 0;
        if (hle_3ds_texture_prepare(raster, &alpha)) {
            current_raster = raster;
            if (alpha_class) *alpha_class = alpha;
        } else ++skipped_textures;
    }
    return 1; /* an undecodable raster falls back to vertex colour while retaining its geometry */
}

void *w3d_be_alloc(uint32_t bytes)
{
    const uint32_t aligned = (bytes + 15u) & ~15u;
    if (aligned > W3D_C3D_SCRATCH_BYTES - scratch_used) { frame_error = -5; return NULL; }
    void *p = scratch + scratch_used;
    scratch_used += aligned;
    return p;
}

int w3d_be_tex_desc(uint32_t raster, int filter, int address_u, int address_v, W3mTex *desc)
{
    (void)filter;
    if (!desc) return 0;
    memset(desc, 0, sizeof(*desc));
    desc->ok = 1;
    desc->textured = raster != 0;
    desc->acl = 0;
    desc->wrap = (address_u < 3 ? 1u : 0u) | (address_v < 3 ? 2u : 0u);
    return 1;
}

int w3d_be_seg(const uint32_t *segment) { (void)segment; ++skipped_segments; return 0; }
const uint32_t *w3d_be_global_template(int *n) { if (n) *n = 0; return NULL; }
int w3d_be_me_selftest(char *why, int why_n)
{
    if (why && why_n > 0) snprintf(why, (size_t)why_n, "Citro3D has no PSP GE display-list or Media Engine path");
    return 0;
}

void w3d_be_report(void)
{
    LOG("Citro3D backend: vertices=%u batches=%u error=%d textures-skipped=%u simplified-states=%u PSP-segments-skipped=%u meshes-skipped=%u\n",
        frame_vertex_count, frame_batch_count, frame_error, skipped_textures, simplified_states, skipped_segments, skipped_meshes);
}

void *w3d_plat_arena(uint32_t bytes) { return hle_plat_big_alloc(bytes); }
void w3d_plat_flush(const void *p, uint32_t bytes) { if (p && bytes) GSPGPU_FlushDataCache((void *)p, bytes); }
extern uint32_t hle_3ds_platform_us(void);
uint32_t w3d_plat_us(void) { return hle_3ds_platform_us(); }
