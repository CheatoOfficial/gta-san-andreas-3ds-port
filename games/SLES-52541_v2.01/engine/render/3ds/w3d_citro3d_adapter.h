#ifndef W3D_CITRO3D_ADAPTER_H
#define W3D_CITRO3D_ADAPTER_H

#include <stddef.h>
#include "hle_w3d.h"

typedef struct {
    float clip[4];
    float color[4];
    float uv[2];
    float view_z;
} W3dC3dVertex;

/* Convert PS2RC model-space W3dVC vertices through the 3x4 GE world matrix and
   W3dCam projection. world uses the same layout as w3d_be_world(). Returns the
   converted vertex count, or a negative error code. */
int w3d_citro3d_convert_tris(const W3dCam *camera, const float world[12], const W3dState *state,
                             const W3dVC *input, int vertex_count, W3dC3dVertex *output, int output_capacity);

/* Decode a PS2RC W3dMeshView PRIM range (triangle list or strip) to a Citro3D triangle list. This
   first mesh slice supports the static UV16/UVF vertex formats with untextured state only. */
int w3d_citro3d_convert_mesh_range(const W3dCam *camera, const float world[12], const W3dState *state,
                                  const W3dMeshView *mesh, const uint8_t *verts_override, uint32_t first_vert,
                                  const uint32_t *prim_words, uint32_t nprims, W3dC3dVertex *output,
                                  int output_capacity);
/* Screening of every vertex converted since the previous call (which it resets): NONFINITE if any converted
   position, UV or view Z was not finite (or the check overflowed), BEHIND if any vertex was not strictly in front
   of the near plane (clip z + w >= 0 fails). UNKNOWN marks batches whose vertices were never screened; builds
   without HLE_3DS_GPU_CLIP skip screening and always return UNKNOWN. */
#define W3D_C3D_SCREEN_NONFINITE 1u
#define W3D_C3D_SCREEN_BEHIND 2u
#define W3D_C3D_SCREEN_UNKNOWN 4u
unsigned w3d_citro3d_take_screen(void);

/* GPU-lit conversion (HLE_3DS_GPU_LIGHT, needs HLE_3DS_GPU_CLIP): the same ranges as w3d_citro3d_convert_lit_range,
   but each output vertex is the skinned object-space position (clip = x, y, z, 1), its object-space normal
   (color = nx, ny, nz, 0) and the final UV; the vertex shader applies camera x world, the lighting and the material
   step from W3dGpuLight. The conversion screen (w3d_citro3d_take_screen) covers these vertices too, with the near
   plane evaluated through camera x world, so the caller can fall back to the CPU path when needed. */
typedef struct {
    float world[12], proj[16];                 /* the call's world matrix and camera projection */
    float base[3], material[3], diffuse, alpha; /* lit formula; disabled lighting = base 1, no lights, alpha 1 */
    float dir[4][3], col[4][3];                /* unused lights have zero colour */
    float post_mul[4], post_add[3];            /* material step: rgb = add + c * mul, a = a * mul.a (identity: 1, 0) */
    int mode;                                  /* 0 GPU-lit, 1 static object space (objmode: vertex colour + material) */
    int skin;                                  /* GPU skinning: vertices carry the bind pose and packed weights */
    float bones[24][4];                        /* skin: row 3 * b + k of bone b (object space) */
} W3dGpuLight;
/* GPU-lit conversion cache (HLE_3DS_LIT_CACHE): budget in bytes (0 = off); stats since the last call: hits, misses,
   hit vertices, miss vertices, skinned (uncacheable) vertices, evictions, allocation failures */
void w3d_citro3d_lit_cache_config(size_t budget_bytes);
/* static ranges as object-space vertices for the shader's objmode (HLE_3DS_GPU_LIGHT), cached with the lit ranges;
   the static cache stats (hits, misses, hit vertices, miss vertices) since the last call */
int w3d_citro3d_convert_mesh_range_gpu(const W3dCam *camera, const float world[12], const W3dState *state,
                                      const W3dMeshView *mesh, const uint8_t *verts_override, uint32_t first_vert,
                                      const uint32_t *prim_words, uint32_t nprims, W3dC3dVertex *output,
                                      int output_capacity, W3dGpuLight *gpu);
void w3d_citro3d_static_cache_stats(uint64_t out[4]);
void w3d_citro3d_lit_cache_stats(uint64_t out[7], size_t *bytes);
int w3d_citro3d_convert_lit_range_gpu(const W3dCam *camera, const float world[12], const W3dState *state,
                                     const W3dState *lit_state, const W3dLights *lights, int nbones,
                                     const float bones[8][12], const W3dMeshView *mesh, const uint8_t *verts_override,
                                     uint32_t first_vert, const uint32_t *prim_words, uint32_t nprims,
                                     W3dC3dVertex *output, int output_capacity, W3dGpuLight *gpu);

int w3d_citro3d_convert_lit_range(const W3dCam *camera, const float world[12], const W3dState *state,
                                 const W3dState *lit_state, const W3dLights *lights, int nbones,
                                 const float bones[8][12], const W3dMeshView *mesh, const uint8_t *verts_override,
                                 uint32_t first_vert, const uint32_t *prim_words, uint32_t nprims,
                                 W3dC3dVertex *output, int output_capacity);

#endif
