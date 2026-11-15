#ifndef W3D_CITRO3D_ADAPTER_H
#define W3D_CITRO3D_ADAPTER_H

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
int w3d_citro3d_convert_lit_range(const W3dCam *camera, const float world[12], const W3dState *state,
                                 const W3dState *lit_state, const W3dLights *lights, int nbones,
                                 const float bones[8][12], const W3dMeshView *mesh, const uint8_t *verts_override,
                                 uint32_t first_vert, const uint32_t *prim_words, uint32_t nprims,
                                 W3dC3dVertex *output, int output_capacity);

#endif
