#ifndef W3D_CITRO3D_BACKEND_H
#define W3D_CITRO3D_BACKEND_H

#include "hle_w3d.h"
#include "w3d_citro3d_adapter.h"
#include "hle_3ds_profile.h"

typedef struct {
    uint32_t first_vertex, vertex_count, raster;
    int filter, address_u, address_v;
    W3dState state;
    float far_z, fog_z;
    unsigned screen;            /* W3D_C3D_SCREEN_* of the batch's conversion */
} W3dC3dBatch;

int w3d_citro3d_init(void);
void w3d_citro3d_shutdown(void);
void w3d_citro3d_frame_reset(void);
const W3dC3dVertex *w3d_citro3d_frame_vertices(void);
int w3d_citro3d_frame_vertex_count(void);
const W3dC3dBatch *w3d_citro3d_frame_batches(void);
int w3d_citro3d_frame_batch_count(void);
int w3d_citro3d_frame_error(void);
unsigned w3d_citro3d_budget_skipped(void);
void w3d_citro3d_take_conv_stats(uint64_t ticks[2], uint64_t verts[2], uint64_t calls[2]);
void w3d_citro3d_log_static_cut(unsigned frames);    /* logs and resets the static distance-cut counters */
unsigned w3d_citro3d_frame_textures_skipped(void);
unsigned w3d_citro3d_frame_states_simplified(void);
unsigned w3d_citro3d_frame_segments_skipped(void);
unsigned w3d_citro3d_frame_meshes_skipped(void);

#endif
