#include "w3d_citro3d_adapter.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef HLE_3DS_LIGHTING_DEBUG
#define HLE_3DS_LIGHTING_DEBUG 0
#endif

enum {
    W3D_C3D_ERR_ARGUMENT = -1,
    W3D_C3D_ERR_UNSUPPORTED_STATE = -2,
    W3D_C3D_ERR_UNSUPPORTED_PRIM = -3,
    W3D_C3D_ERR_UNSUPPORTED_FORMAT = -4,
    W3D_C3D_ERR_CAPACITY = -5,
};

static unsigned mul255(unsigned a, unsigned b) {
    return (a * b + 127u) / 255u;
}

static unsigned color_component(uint32_t color, int channel) {
    return (color >> (channel * 8)) & 0xFFu;
}

static uint32_t apply_simple_light(uint32_t color, const W3dState *state) {
#if HLE_3DS_LIGHTING_DEBUG
    (void)state;
    return 0xFFFFFFFFu;
#else
    if (!state->light) return color;

    uint32_t result = 0;
    for (int channel = 0; channel < 3; ++channel) {
        unsigned value = color_component(state->lemis, channel) +
                         mul255(color_component(color, channel), color_component(state->lamb, channel));
        if (value > 255u) value = 255u;
        result |= value << (channel * 8);
    }
    result |= mul255(color_component(color, 3), color_component(state->lamb, 3)) << 24;
    return result;
#endif
}

/* Screening for the present pass (w3d_citro3d_take_screen). The sum of every float output is non-finite whenever
   one of them is (colours come from bytes and are always finite); a huge finite sum can also overflow, which only
   sends a batch through the checked path. Any vertex not in front of the near plane (or NaN) sets the behind flag. */
#ifndef HLE_3DS_GPU_CLIP
#define HLE_3DS_GPU_CLIP 0
#endif
static float screen_sum;
static unsigned screen_behind;

unsigned w3d_citro3d_take_screen(void)
{
    if (!HLE_3DS_GPU_CLIP) return W3D_C3D_SCREEN_UNKNOWN;   /* only the GPU-clip present pass uses the screen */
    unsigned flags = screen_behind ? W3D_C3D_SCREEN_BEHIND : 0u;
    if (!(screen_sum - screen_sum == 0.0f)) flags |= W3D_C3D_SCREEN_NONFINITE;
    screen_sum = 0.0f;
    screen_behind = 0;
    return flags;
}

static void convert_vertex(const W3dCam *camera, const float world[12], const W3dState *state,
                           const W3dVC *input, W3dC3dVertex *output) {
    const float x = input->x * world[0] + input->y * world[3] + input->z * world[6] + world[9];
    const float y = input->x * world[1] + input->y * world[4] + input->z * world[7] + world[10];
    const float z = input->x * world[2] + input->y * world[5] + input->z * world[8] + world[11];
    const float *m = camera->proj;
    output->view_z = z;

    /* Matches PS2RC's world transform and GE/host camera transform. */
    output->clip[0] = x * m[0] + y * m[4] + z * m[8] + m[12];
    output->clip[1] = x * m[1] + y * m[5] + z * m[9] + m[13];
    output->clip[2] = x * m[2] + y * m[6] + z * m[10] + m[14];
    output->clip[3] = x * m[3] + y * m[7] + z * m[11] + m[15];

    output->uv[0] = input->u * state->uscale + state->ubase;
    output->uv[1] = input->v * state->uscale + state->vbase;
#if HLE_3DS_GPU_CLIP
    screen_sum += output->clip[0] + output->clip[1] + output->clip[2] + output->clip[3] +
                  output->uv[0] + output->uv[1] + z;
    if (!(output->clip[2] + output->clip[3] >= 0.0f)) screen_behind = 1;
#endif

    const uint32_t color = apply_simple_light(input->c, state);
    for (int channel = 0; channel < 4; ++channel)
        output->color[channel] = (float)color_component(color, channel) * (1.0f / 255.0f);
}

int w3d_citro3d_convert_tris(const W3dCam *camera, const float world[12], const W3dState *state,
                             const W3dVC *input, int vertex_count, W3dC3dVertex *output, int output_capacity) {
    if (!camera || !world || !state || !input || !output || vertex_count < 3 || vertex_count % 3 ||
        output_capacity < vertex_count)
        return W3D_C3D_ERR_ARGUMENT;
    /* This first slice handles the untextured triangle-list path and its vertex/ambient color.
       Texture sampling and directional lights are handled by the batch renderer. */
    if (state->textured || state->lights) return W3D_C3D_ERR_UNSUPPORTED_STATE;

    for (int i = 0; i < vertex_count; ++i)
        convert_vertex(camera, world, state, &input[i], &output[i]);
    return vertex_count;
}

static int mesh_vertex(const W3dMeshView *mesh, const uint8_t *verts, uint32_t index, W3dVC *out) {
    if (mesh->fmt == W3D_FMT_UV16) {
        const W3dV16 *v = (const W3dV16 *)(verts + (size_t)index * mesh->stride);
        out->u = (float)v->u / 32768.0f;
        out->v = (float)v->v / 32768.0f;
        out->c = v->c;
        out->x = (float)v->x / 32768.0f;
        out->y = (float)v->y / 32768.0f;
        out->z = (float)v->z / 32768.0f;
        return 1;
    }
    if (mesh->fmt == W3D_FMT_UVF) {
        const W3dVF *v = (const W3dVF *)(verts + (size_t)index * mesh->stride);
        out->u = v->u;
        out->v = v->v;
        out->c = v->c;
        out->x = (float)v->x / 32768.0f;
        out->y = (float)v->y / 32768.0f;
        out->z = (float)v->z / 32768.0f;
        return 1;
    }
    return 0;
}

static int prim_output_count(uint32_t word, uint32_t *input_count, uint32_t *output_count) {
    const uint32_t command = word >> 24;
    if (command == 0x0B) {
        *input_count = 0;
        *output_count = 0;
        return 1; /* RET ends the mesh sub-list. */
    }
    if (command != 0x04) return 0;

    const uint32_t count = word & 0xFFFFu;
    const uint32_t prim = (word >> 16) & 7u;
    *input_count = count;
    if (prim == 3) {
        *output_count = (count / 3u) * 3u;
        return 1;
    }
    if (prim == 4) {
        *output_count = count >= 3u ? (count - 2u) * 3u : 0u;
        return 1;
    }
    return 0;
}

#define W3D_C3D_STRIP_CACHE 512u
static W3dC3dVertex strip_cache[W3D_C3D_STRIP_CACHE];

static int emit_mesh_vertex(const W3dCam *camera, const float world[12], const W3dState *state,
                            const W3dMeshView *mesh, const uint8_t *verts, uint32_t index,
                            W3dC3dVertex *output) {
    W3dVC v;
    if (!mesh_vertex(mesh, verts, index, &v)) return 0;
    convert_vertex(camera, world, state, &v, output);
    return 1;
}

int w3d_citro3d_convert_mesh_range(const W3dCam *camera, const float world[12], const W3dState *state,
                                  const W3dMeshView *mesh, const uint8_t *verts_override, uint32_t first_vert,
                                  const uint32_t *prim_words, uint32_t nprims, W3dC3dVertex *output,
                                  int output_capacity) {
    if (!camera || !world || !state || !mesh || !prim_words || !nprims || nprims > mesh->nprims ||
        !output || output_capacity < 0 ||
        mesh->nverts == 0 || mesh->stride == 0 || (!verts_override && !mesh->verts) || first_vert >= mesh->nverts)
        return W3D_C3D_ERR_ARGUMENT;
    if (state->textured || state->lights) return W3D_C3D_ERR_UNSUPPORTED_STATE;
    if (mesh->fmt != W3D_FMT_UV16 && mesh->fmt != W3D_FMT_UVF) return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
    if ((mesh->fmt == W3D_FMT_UV16 && mesh->stride < sizeof(W3dV16)) ||
        (mesh->fmt == W3D_FMT_UVF && mesh->stride < sizeof(W3dVF)))
        return W3D_C3D_ERR_ARGUMENT;

    uint64_t need = 0;
    uint64_t consumed = 0;
    for (uint32_t i = 0; i < nprims; ++i) {
        uint32_t in_count, out_count;
        if (!prim_output_count(prim_words[i], &in_count, &out_count)) return W3D_C3D_ERR_UNSUPPORTED_PRIM;
        consumed += in_count;
        need += out_count;
        if (first_vert + consumed > mesh->nverts || need > INT_MAX) return W3D_C3D_ERR_ARGUMENT;
        if ((prim_words[i] >> 24) == 0x0B) break;
    }
    if (need > (uint64_t)output_capacity) return W3D_C3D_ERR_CAPACITY;

    const uint8_t *verts = verts_override ? verts_override : mesh->verts;
    uint32_t at = 0;
    int written = 0;
    for (uint32_t i = 0; i < nprims; ++i) {
        const uint32_t word = prim_words[i];
        uint32_t in_count, out_count;
        if (!prim_output_count(word, &in_count, &out_count)) return W3D_C3D_ERR_UNSUPPORTED_PRIM;
        if ((word >> 24) == 0x0B) break;
        const uint32_t prim = (word >> 16) & 7u;
        if (prim == 3) {
            for (uint32_t j = 0; j < out_count; ++j) {
                if (!emit_mesh_vertex(camera, world, state, mesh, verts, first_vert + at + j, &output[written++]))
                    return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
            }
        } else if (in_count <= W3D_C3D_STRIP_CACHE) { /* triangle strip; each vertex converted once, winding alternates. */
            for (uint32_t j = 0; j < in_count; ++j)
                if (!emit_mesh_vertex(camera, world, state, mesh, verts, first_vert + at + j, &strip_cache[j]))
                    return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
            for (uint32_t j = 2; j < in_count; ++j) {
                uint32_t a = j - 2u, b = j - 1u;
                if ((j - 2u) & 1u) { const uint32_t tmp = a; a = b; b = tmp; }
                output[written++] = strip_cache[a];
                output[written++] = strip_cache[b];
                output[written++] = strip_cache[j];
            }
        } else { /* oversized strip: per-triangle conversion */
            for (uint32_t j = 2; j < in_count; ++j) {
                uint32_t a = j - 2u, b = j - 1u, c = j;
                if ((j - 2u) & 1u) { const uint32_t tmp = a; a = b; b = tmp; }
                if (!emit_mesh_vertex(camera, world, state, mesh, verts, first_vert + at + a, &output[written++]) ||
                    !emit_mesh_vertex(camera, world, state, mesh, verts, first_vert + at + b, &output[written++]) ||
                    !emit_mesh_vertex(camera, world, state, mesh, verts, first_vert + at + c, &output[written++]))
                    return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
            }
        }
        at += in_count;
    }
    return written;
}

/* GE vertex layout used by PS2RC's lit/skinned groups. Each attribute is aligned to
   its component size, then the whole vertex is aligned to its largest component. */
typedef struct {
    uint32_t weights_off, uv_off, normal_off, position_off, size;
    unsigned weights_count, weights_fmt, uv_fmt, normal_fmt, position_fmt;
} W3dLitLayout;

static unsigned lit_component_size(unsigned fmt)
{
    static const unsigned sizes[4] = {0, 1, 2, 4};
    return fmt < 4 ? sizes[fmt] : 0;
}

static int lit_layout(uint32_t vtype, W3dLitLayout *layout)
{
    memset(layout, 0, sizeof(*layout));
    layout->weights_fmt = (vtype >> 9) & 3u;
    layout->uv_fmt = vtype & 3u;
    layout->normal_fmt = (vtype >> 5) & 3u;
    layout->position_fmt = (vtype >> 7) & 3u;
    layout->weights_count = layout->weights_fmt ? ((vtype >> 14) & 7u) + 1u : 0u;
    if (!layout->position_fmt) return 0;

    uint32_t offset = 0;
    unsigned largest = 1;
#define LIT_ATTRIBUTE(fmt_, count_, off_) do { \
        const unsigned size_ = lit_component_size(fmt_); \
        if ((fmt_) != 0) { \
            if (!size_) return 0; \
            if (size_ > largest) largest = size_; \
            offset = (offset + size_ - 1u) & ~(size_ - 1u); \
            (off_) = offset; \
            offset += size_ * (count_); \
        } \
    } while (0)
    LIT_ATTRIBUTE(layout->weights_fmt, layout->weights_count, layout->weights_off);
    LIT_ATTRIBUTE(layout->uv_fmt, 2u, layout->uv_off);
    LIT_ATTRIBUTE(layout->normal_fmt, 3u, layout->normal_off);
    LIT_ATTRIBUTE(layout->position_fmt, 3u, layout->position_off);
#undef LIT_ATTRIBUTE
    layout->size = (offset + largest - 1u) & ~(largest - 1u);
    return layout->size != 0;
}

static float lit_read_component(const uint8_t *p, unsigned fmt, unsigned index, int is_uv)
{
    if (fmt == 1) {
        if (is_uv) return p[index] * (1.0f / 128.0f);
        return ((const int8_t *)p)[index] * (1.0f / 127.0f);
    }
    if (fmt == 2) {
        if (is_uv) {
            uint16_t value;
            memcpy(&value, p + 2u * index, sizeof(value));
            return value * (1.0f / 32768.0f);
        }
        int16_t value;
        memcpy(&value, p + 2u * index, sizeof(value));
        return value * (1.0f / 32768.0f);
    }
    float value;
    memcpy(&value, p + 4u * index, sizeof(value));
    return value;
}

static float lit_read_weight(const uint8_t *p, unsigned fmt, unsigned index)
{
    if (fmt == 1) return p[index] * (1.0f / 128.0f);
    if (fmt == 2) {
        uint16_t value;
        memcpy(&value, p + 2u * index, sizeof(value));
        return value * (1.0f / 32768.0f);
    }
    float value;
    memcpy(&value, p + 4u * index, sizeof(value));
    return value;
}

/* Material and light-count terms are constant for a conversion call. Preserve the per-vertex
   arithmetic order below: factoring material into the directional colour changes byte rounding. */
typedef struct {
    int enabled, nlights;
    uint32_t alpha;
    float diffuse_scale, material[3], base[3];
    const W3dLights *lights;
} W3dLitPrepared;

static W3dLitPrepared lit_prepare(const W3dState *state, const W3dLights *lights)
{
    W3dLitPrepared result = {0};
#if !HLE_3DS_LIGHTING_DEBUG
    if (state->lights && lights) {
        result.enabled = 1;
        result.lights = lights;
        result.alpha = state->lamb & 0xFF000000u;
        result.nlights = lights->n;
        if (result.nlights < 0) result.nlights = 0;
        if (result.nlights > 4) result.nlights = 4;
        if (result.nlights > state->lights - 1) result.nlights = state->lights - 1;
        result.diffuse_scale = (float)((state->lemis >> 8) & 0xFFu) * (1.0f / 255.0f);
        const float ambient_scale = (float)(state->lemis & 0xFFu) * (1.0f / 255.0f);
        for (int channel = 0; channel < 3; ++channel) {
            result.material[channel] = (float)((state->lamb >> (8 * channel)) & 0xFFu) * (1.0f / 255.0f);
            result.base[channel] = (float)((state->lemit >> (8 * channel)) & 0xFFu) * (1.0f / 255.0f) +
                                  result.material[channel] * ambient_scale * lights->amb[channel];
        }
    }
#else
    (void)state; (void)lights;
#endif
    return result;
}

static uint32_t lit_vertex_colour(const float normal[3], const W3dLitPrepared *prepared)
{
    if (!prepared->enabled) return 0xFFFFFFFFu;
    const W3dLights *lights = prepared->lights;
    const float normal_length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    const float inverse_length = normal_length > 0.0f ? 1.0f / normal_length : 0.0f;
    float light_factor[4];
    for (int light = 0; light < prepared->nlights; ++light) {
        const float direction = (normal[0] * lights->dir[light][0] + normal[1] * lights->dir[light][1] +
                                 normal[2] * lights->dir[light][2]) * inverse_length;
        light_factor[light] = direction > 0.0f ? prepared->diffuse_scale * direction : 0.0f;
    }
    uint32_t colour = prepared->alpha;
    for (int channel = 0; channel < 3; ++channel) {
        float value = prepared->base[channel];
        for (int light = 0; light < prepared->nlights; ++light)
            if (light_factor[light] > 0.0f)
                value += prepared->material[channel] * light_factor[light] * lights->col[light][channel];
        int component = (int)(value * 255.0f + 0.5f);
        if (component < 0) component = 0;
        if (component > 255) component = 255;
        colour |= (uint32_t)component << (8 * channel);
    }
    return colour;
}

/* Decode one lit vertex and apply the skin: object-space position, normal and raw UV. */
static int lit_decode(int nbones, const float bones[8][12], const W3dLitLayout *layout, const W3dMeshView *mesh,
                      const uint8_t *vertices, uint32_t index, float position[3], float normal[3], float uv[2])
{
    if (index >= mesh->nverts) return 0;
    const uint8_t *v = vertices + (size_t)index * mesh->stride;
    normal[0] = 0.0f; normal[1] = 0.0f; normal[2] = 1.0f;
    uv[0] = 0.0f; uv[1] = 0.0f;
    for (int i = 0; i < 3; ++i)
        position[i] = lit_read_component(v + layout->position_off, layout->position_fmt, (unsigned)i, 0);
    if (layout->normal_fmt)
        for (int i = 0; i < 3; ++i)
            normal[i] = lit_read_component(v + layout->normal_off, layout->normal_fmt, (unsigned)i, 0);
    if (layout->uv_fmt)
        for (int i = 0; i < 2; ++i)
            uv[i] = lit_read_component(v + layout->uv_off, layout->uv_fmt, (unsigned)i, 1);

    if (layout->weights_count && nbones > 0) {
        float skinned_position[3] = {0.0f, 0.0f, 0.0f};
        float skinned_normal[3] = {0.0f, 0.0f, 0.0f};
        int count = (int)layout->weights_count;
        if (count > nbones) count = nbones;
        if (count > 8) count = 8;
        for (int b = 0; b < count; ++b) {
            const float weight = lit_read_weight(v + layout->weights_off, layout->weights_fmt, (unsigned)b);
            if (weight == 0.0f) continue;
            const float *matrix = bones[b];
            for (int k = 0; k < 3; ++k) {
                skinned_position[k] += weight * (position[0] * matrix[k] + position[1] * matrix[3 + k] +
                                                   position[2] * matrix[6 + k] + matrix[9 + k]);
                skinned_normal[k] += weight * (normal[0] * matrix[k] + normal[1] * matrix[3 + k] +
                                                normal[2] * matrix[6 + k]);
            }
        }
        memcpy(position, skinned_position, 3 * sizeof(float));
        memcpy(normal, skinned_normal, 3 * sizeof(float));
    }
    return 1;
}

static int lit_vertex(const float world[12], const W3dLitPrepared *prepared,
                      int nbones, const float bones[8][12], const W3dLitLayout *layout,
                      const W3dMeshView *mesh, const uint8_t *vertices, uint32_t index, W3dVC *out)
{
    float position[3], normal[3], uv[2];
    if (!lit_decode(nbones, bones, layout, mesh, vertices, index, position, normal, uv)) return 0;
    float world_normal[3];
    for (int k = 0; k < 3; ++k)
        world_normal[k] = normal[0] * world[k] + normal[1] * world[3 + k] + normal[2] * world[6 + k];
    out->u = uv[0];
    out->v = uv[1];
    out->c = lit_vertex_colour(world_normal, prepared);
    out->x = position[0];
    out->y = position[1];
    out->z = position[2];
    return 1;
}

/* One lit vertex into the output: the CPU path (lit colour + convert_vertex), or with gpu set the object-space
   GPU-lit form (position, normal, final UV), screened through near_row (camera x world: clip z + clip w). */
typedef struct { const W3dCam *camera; const float *world; const W3dState *state; const W3dLitPrepared *prepared;
                 int nbones; const float (*bones)[12]; const W3dLitLayout *layout; const W3dMeshView *mesh;
                 const uint8_t *vertices; int gpu; float near_row[4]; int skin; } LitEmit;
static float skin_lo[3], skin_hi[3];          /* bind-pose bounds of the range being emitted (skin mode) */

/* pack the bone weights of vertex v as up to three (bone + weight / 2) values; 0 = more than three non-zero
   weights or a weight outside [0, 2) (the range then takes the CPU skinning path) */
static int skin_pack(const W3dLitLayout *layout, int nbones, const uint8_t *v, float packed[3])
{
    int count = (int)layout->weights_count;
    if (count > nbones) count = nbones;
    if (count > 8) count = 8;
    int used = 0;
    float total = 0.0f;
    packed[0] = packed[1] = packed[2] = 0.0f;
    for (int b = 0; b < count; ++b) {
        const float weight = lit_read_weight(v + layout->weights_off, layout->weights_fmt, (unsigned)b);
        if (weight == 0.0f) continue;
        if (!(weight > 0.0f && weight < 1.999f) || used == 3) return 0;
        packed[used++] = (float)b + weight * 0.5f;
        total += weight;
    }
    /* the conservative near bound needs weights that sum to one (within byte quantisation) */
    /* the near bound (skin_screen) allows for sums in (0.97, 1.03); an all-zero vertex blends to the origin
       whatever the bones, outside that bound, so it takes the CPU path */
    return used > 0 && total > 0.97f && total < 1.03f;
}

static int lit_emit(const LitEmit *e, uint32_t index, W3dC3dVertex *o)
{
    if (!e->gpu) {
        W3dVC vertex;
        if (!lit_vertex(e->world, e->prepared, e->nbones, e->bones, e->layout, e->mesh, e->vertices, index, &vertex))
            return 0;
        convert_vertex(e->camera, e->world, e->state, &vertex, o);
        return 1;
    }
    float p[3], n[3], uv[2];
    if (!lit_decode(e->skin ? 0 : e->nbones, e->bones, e->layout, e->mesh, e->vertices, index, p, n, uv)) return 0;
    float packed[3] = {0.0f, 0.0f, 0.0f};
    if (e->skin && !skin_pack(e->layout, e->nbones, e->vertices + (size_t)index * e->mesh->stride, packed)) return -1;
    o->clip[0] = p[0]; o->clip[1] = p[1]; o->clip[2] = p[2]; o->clip[3] = e->skin ? packed[0] : 1.0f;
    o->color[0] = n[0]; o->color[1] = n[1]; o->color[2] = n[2]; o->color[3] = packed[1];
    o->uv[0] = uv[0] * e->state->uscale + e->state->ubase;
    o->uv[1] = uv[1] * e->state->uscale + e->state->vbase;
    o->view_z = packed[2];
    if (e->skin)
        for (int k = 0; k < 3; ++k) {
            if (p[k] < skin_lo[k]) skin_lo[k] = p[k];
            if (p[k] > skin_hi[k]) skin_hi[k] = p[k];
        }
#if HLE_3DS_GPU_CLIP
    screen_sum += p[0] + p[1] + p[2] + n[0] + n[1] + n[2] + o->uv[0] + o->uv[1];
    if (!e->skin) {
        const float near = e->near_row[0] * p[0] + e->near_row[1] * p[1] + e->near_row[2] * p[2] + e->near_row[3];
        if (!(near >= 0.0f)) screen_behind = 1;
    }
#endif
    return 1;
}

/* skin mode: the conservative near screening (header of HLE_3DS_GPU_SKIN) and the bones' finiteness */
static void skin_screen(const float near_row[4], int nbones, const float bones[8][12], const float lo[3],
                        const float hi[3])
{
#if HLE_3DS_GPU_CLIP
    /* near(x) = n.x + d is affine: for weights w with sum s, near(sum w q) = sum w near(q) + d (1 - s)
       >= s * min near(q) - |s - 1| |d| >= 0.97 * min - 0.03 * |d| for the sums skin_pack accepts */
    float lowest = INFINITY;
    for (int b = 0; b < nbones && b < 8; ++b) {
        const float *m = bones[b];
        for (int i = 0; i < 12; ++i) screen_sum += m[i];
        for (int corner = 0; corner < 8; ++corner) {
            const float c[3] = {corner & 1 ? hi[0] : lo[0], corner & 2 ? hi[1] : lo[1], corner & 4 ? hi[2] : lo[2]};
            float q[3];
            for (int k = 0; k < 3; ++k) q[k] = c[0] * m[k] + c[1] * m[3 + k] + c[2] * m[6 + k] + m[9 + k];
            const float near = near_row[0] * q[0] + near_row[1] * q[1] + near_row[2] * q[2] + near_row[3];
            if (near != near) { screen_behind = 1; return; }   /* NaN: the checked CPU path */
            if (near < lowest) lowest = near;
        }
    }
    if (!(0.97f * lowest - 0.03f * fabsf(near_row[3]) >= 0.0f)) screen_behind = 1;
#else
    (void)near_row; (void)nbones; (void)bones; (void)lo; (void)hi;
#endif
}

static void skin_rows(W3dGpuLight *gpu, int nbones, const float bones[8][12])
{
    memset(gpu->bones, 0, sizeof gpu->bones);
    for (int b = 0; b < nbones && b < 8; ++b)
        for (int k = 0; k < 3; ++k) {
            gpu->bones[3 * b + k][0] = bones[b][k];
            gpu->bones[3 * b + k][1] = bones[b][3 + k];
            gpu->bones[3 * b + k][2] = bones[b][6 + k];
            gpu->bones[3 * b + k][3] = bones[b][9 + k];
        }
}


/* Cache of GPU-lit conversions (HLE_3DS_LIT_CACHE). With GPU lighting a non-skinned lit vertex is object-space data
   (position, normal, UV after the draw's UV scale): it depends only on the mesh bytes and the UV parameters, not on
   the camera, the world matrix or the lights. Vehicles and props are drawn every frame from the same guest mesh, so
   the converted range is kept and copied on the next draw instead of decoded again (the REGTA/librw idea of
   instancing a mesh once). The key holds a hash of the vertex bytes and primitive words actually read, so a model
   streamed into reused memory is a miss, never stale geometry. Only the per-draw near screening is redone on a hit
   (one dot product per vertex), so the CPU-fallback decision is unchanged. Budget 0 = off (set by the backend). */
#ifndef HLE_3DS_LIT_CACHE
#define HLE_3DS_LIT_CACHE 1
#endif
/* GPU skinning (HLE_3DS_GPU_SKIN, GPU-lit path): a skinned range whose vertices have at most three non-zero bone
   weights goes to the shader as bind-pose position / normal plus packed (bone, weight) pairs, with the bone rows as
   uniforms (vshader.v.pica, skinmode). Such output no longer depends on the bones, so it is cached like a non-skinned
   range. The near screening becomes conservative: with non-negative weights the skinned position cannot be nearer
   than the nearest of bone b x a corner of the bind-pose bounds over the bones the range uses. */
#ifndef HLE_3DS_GPU_SKIN
#define HLE_3DS_GPU_SKIN 1
#endif
#if !HLE_3DS_LIT_CACHE
int w3d_citro3d_cache_by_ref;
const W3dC3dVertex *w3d_citro3d_cache_hit;
void w3d_citro3d_cache_new_frame(void) {}
void w3d_citro3d_lit_cache_config(size_t budget_bytes) { (void)budget_bytes; }
void w3d_citro3d_lit_cache_stats(uint64_t out[7], size_t *bytes) { memset(out, 0, 7 * sizeof *out); if (bytes) *bytes = 0; }
void w3d_citro3d_static_cache_stats(uint64_t out[4]) { memset(out, 0, 4 * sizeof *out); }
#endif
#if HLE_3DS_LIT_CACHE
#define LIT_CACHE_SLOTS 2048u            /* power of two */
#define LIT_CACHE_PROBE 8u
typedef struct {
    uintptr_t vertices;
    uint32_t first_vert, count, nprims, vtype, stride, hash, uvbits[3];
    W3dC3dVertex *data;                  /* NULL = empty slot */
    int written;
    uint32_t stamp;
    uint32_t pin;                        /* lc_frame when last served by reference: kept until that frame is presented */
    float sum;                           /* the screen_sum contribution of the stored vertices */
    int nbones, skin;                    /* skin: GPU-skinned (bone-independent) data */
    float lo[3], hi[3];                  /* skin: bind-pose bounds for the conservative near screening */
} LitCacheEntry;
static LitCacheEntry lit_cache[LIT_CACHE_SLOTS];
static size_t lit_cache_budget, lit_cache_bytes;
/* serving by reference (the backend): a hit hands back the stored vertices (w3d_citro3d_cache_hit) instead of copying
   them to the output; the entry is pinned for the current frame (w3d_citro3d_cache_new_frame after the present) */
static uint32_t lc_frame = 1;
int w3d_citro3d_cache_by_ref;
const W3dC3dVertex *w3d_citro3d_cache_hit;
void w3d_citro3d_cache_new_frame(void) { ++lc_frame; }
static uint32_t lit_cache_clock;
static uint64_t lc_hits, lc_misses, lc_hit_verts, lc_miss_verts, lc_skinned_verts, lc_evicted, lc_nomem;
static uint64_t lc_static_hits, lc_static_misses, lc_static_hit_verts, lc_static_miss_verts;
void w3d_citro3d_static_cache_stats(uint64_t out[4])
{
    out[0] = lc_static_hits; out[1] = lc_static_misses; out[2] = lc_static_hit_verts; out[3] = lc_static_miss_verts;
    lc_static_hits = lc_static_misses = lc_static_hit_verts = lc_static_miss_verts = 0;
}

void w3d_citro3d_lit_cache_config(size_t budget_bytes)
{
    lit_cache_budget = budget_bytes;
}

void w3d_citro3d_lit_cache_stats(uint64_t out[7], size_t *bytes)
{
    out[0] = lc_hits; out[1] = lc_misses; out[2] = lc_hit_verts; out[3] = lc_miss_verts;
    out[4] = lc_skinned_verts; out[5] = lc_evicted; out[6] = lc_nomem;
    lc_hits = lc_misses = lc_hit_verts = lc_miss_verts = lc_skinned_verts = lc_evicted = lc_nomem = 0;
    if (bytes) *bytes = lit_cache_bytes;
}

static uint32_t lc_words(uint32_t h, const uint8_t *p, size_t n)
{
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        uint32_t w;
        memcpy(&w, p + i, 4);
        h = (h ^ w) * 0x01000193u;
    }
    for (; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

static uint32_t lc_slot(const LitCacheEntry *k)
{
    uint32_t h = k->hash ^ (uint32_t)k->vertices * 0x9E3779B1u ^ k->first_vert * 0x85EBCA77u ^ k->count * 0xC2B2AE3Du;
    h ^= h >> 15;
    return h & (LIT_CACHE_SLOTS - 1u);
}

static int lc_same(const LitCacheEntry *a, const LitCacheEntry *b)
{
    return a->vertices == b->vertices && a->first_vert == b->first_vert && a->count == b->count &&
           a->nprims == b->nprims && a->vtype == b->vtype && a->stride == b->stride && a->hash == b->hash &&
           a->nbones == b->nbones &&
           !memcmp(a->uvbits, b->uvbits, sizeof a->uvbits);
}

static void lc_drop(LitCacheEntry *e)
{
    if (!e->data) return;
    free(e->data);
    lit_cache_bytes -= (size_t)e->written * sizeof(W3dC3dVertex);
    e->data = NULL;
    ++lc_evicted;
}

/* evict the least recently used entries until the cache is below 3/4 of its budget */
static void lc_shrink(void)
{
    while (lit_cache_bytes > lit_cache_budget / 4u * 3u) {
        LitCacheEntry *oldest = NULL;
        for (uint32_t i = 0; i < LIT_CACHE_SLOTS; ++i)
            if (lit_cache[i].data && lit_cache[i].pin != lc_frame &&
                (!oldest || (int32_t)(lit_cache[i].stamp - oldest->stamp) < 0)) oldest = &lit_cache[i];
        if (!oldest) break;
        lc_drop(oldest);
    }
}

/* the near-plane screening of a stored vertex (lit_emit's, without the decode) */
static void lc_screen(const float near_row[4], const W3dC3dVertex *v, int n)
{
#if HLE_3DS_GPU_CLIP
    for (int i = 0; i < n; ++i) {
        const float near = near_row[0] * v[i].clip[0] + near_row[1] * v[i].clip[1] + near_row[2] * v[i].clip[2] + near_row[3];
        if (!(near >= 0.0f)) { screen_behind = 1; return; }
    }
#else
    (void)near_row; (void)v; (void)n;
#endif
}
#endif

static int lit_range(const W3dCam *camera, const float world[12], const W3dState *state,
                     const W3dState *lit_state, const W3dLights *lights, int nbones,
                     const float bones[8][12], const W3dMeshView *mesh, const uint8_t *verts_override,
                     uint32_t first_vert, const uint32_t *prim_words, uint32_t nprims,
                     W3dC3dVertex *output, int output_capacity, W3dGpuLight *gpu)
{
    if (!camera || !world || !state || !lit_state || !mesh || !prim_words || !nprims || nprims > mesh->nprims ||
        !output || output_capacity < 0 || !mesh->nverts || !mesh->stride ||
        (!verts_override && !mesh->verts) || first_vert >= mesh->nverts)
        return W3D_C3D_ERR_ARGUMENT;
    if (state->textured || state->lights) return W3D_C3D_ERR_UNSUPPORTED_STATE;

    W3dLitLayout layout;
    if (!lit_layout(mesh->vtype, &layout) || layout.size > mesh->stride) return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
    const uint8_t *vertices = verts_override ? verts_override : mesh->verts;
    uint64_t required = 0, consumed = 0;
    for (uint32_t i = 0; i < nprims; ++i) {
        uint32_t input_count, output_count;
        if (!prim_output_count(prim_words[i], &input_count, &output_count)) return W3D_C3D_ERR_UNSUPPORTED_PRIM;
        consumed += input_count;
        required += output_count;
        if (first_vert + consumed > mesh->nverts || required > INT_MAX) return W3D_C3D_ERR_ARGUMENT;
        if ((prim_words[i] >> 24) == 0x0Bu) break;
    }
    if (required > (uint64_t)output_capacity) return W3D_C3D_ERR_CAPACITY;

    const W3dLitPrepared prepared = lit_prepare(lit_state, lights);
    LitEmit emit = {camera, world, state, &prepared, nbones, bones, &layout, mesh, vertices, gpu != NULL, {0}, 0};
    if (gpu) {
        /* near distance = clip z + clip w of camera x world, as a row on the object-space position */
        const float *m = camera->proj;
        for (int c = 0; c < 4; ++c) {
            float v = c == 3 ? m[14] + m[15] : 0.0f;
            for (int j = 0; j < 3; ++j) v += (m[4 * j + 2] + m[4 * j + 3]) * world[c < 3 ? 3 * c + j : 9 + j];
            emit.near_row[c] = v;
        }
        gpu->mode = 0;
        memcpy(gpu->world, world, sizeof gpu->world);
        memcpy(gpu->proj, camera->proj, sizeof gpu->proj);
        memset(gpu->dir, 0, sizeof gpu->dir);
        memset(gpu->col, 0, sizeof gpu->col);
        if (prepared.enabled) {
            for (int c = 0; c < 3; ++c) { gpu->base[c] = prepared.base[c]; gpu->material[c] = prepared.material[c]; }
            gpu->diffuse = prepared.diffuse_scale;
            gpu->alpha = (float)(prepared.alpha >> 24) * (1.0f / 255.0f);
            for (int l = 0; l < prepared.nlights; ++l)
                for (int c = 0; c < 3; ++c) { gpu->dir[l][c] = lights->dir[l][c]; gpu->col[l][c] = lights->col[l][c]; }
        } else {
            for (int c = 0; c < 3; ++c) { gpu->base[c] = 1.0f; gpu->material[c] = 0.0f; }
            gpu->diffuse = 0.0f;
            gpu->alpha = 1.0f;
        }
        /* convert_vertex's apply_simple_light, after the lit colour */
        if (state->light && !HLE_3DS_LIGHTING_DEBUG) {
            for (int c = 0; c < 4; ++c) gpu->post_mul[c] = (float)color_component(state->lamb, c) * (1.0f / 255.0f);
            for (int c = 0; c < 3; ++c) gpu->post_add[c] = (float)color_component(state->lemis, c) * (1.0f / 255.0f);
        } else {
            for (int c = 0; c < 4; ++c) gpu->post_mul[c] = 1.0f;
            for (int c = 0; c < 3; ++c) gpu->post_add[c] = 0.0f;
        }
    }
#if HLE_3DS_LIT_CACHE
    LitCacheEntry key, *slot = NULL;
    if (gpu) gpu->skin = 0;
    const int skin_ok = HLE_3DS_GPU_SKIN && gpu && nbones > 0 && bones && layout.weights_count;
    if (gpu && nbones > 0 && !skin_ok) lc_skinned_verts += required;
    if (gpu && (nbones == 0 || skin_ok) && lit_cache_budget && required) {
        memset(&key, 0, sizeof key);
        key.vertices = (uintptr_t)vertices;
        key.first_vert = first_vert;
        key.count = (uint32_t)consumed;
        key.nprims = nprims;
        key.vtype = mesh->vtype;
        key.stride = mesh->stride;
        key.nbones = nbones;
        memcpy(&key.uvbits[0], &state->uscale, 4);
        memcpy(&key.uvbits[1], &state->ubase, 4);
        memcpy(&key.uvbits[2], &state->vbase, 4);
        uint32_t h = lc_words(0x811C9DC5u, vertices + (size_t)first_vert * mesh->stride, (size_t)consumed * mesh->stride);
        key.hash = lc_words(h, (const uint8_t *)prim_words, (size_t)nprims * 4u);
        const uint32_t home = lc_slot(&key);
        LitCacheEntry *empty = NULL, *oldest = NULL;
        for (uint32_t k = 0; k < LIT_CACHE_PROBE; ++k) {
            LitCacheEntry *e = &lit_cache[(home + k) & (LIT_CACHE_SLOTS - 1u)];
            if (e->data && lc_same(e, &key)) {
                if (e->skin ? 0 : nbones > 0) continue;     /* a CPU-skinned entry cannot exist; never serve one */
                e->stamp = ++lit_cache_clock;
                if (w3d_citro3d_cache_by_ref) { e->pin = lc_frame; w3d_citro3d_cache_hit = e->data; }
                else memcpy(output, e->data, (size_t)e->written * sizeof *output);
#if HLE_3DS_GPU_CLIP
                screen_sum += e->sum;
#endif
                if (e->skin) {
                    gpu->skin = 1;
                    skin_rows(gpu, nbones, bones);
                    skin_screen(emit.near_row, nbones, bones, e->lo, e->hi);
                } else {
                    lc_screen(emit.near_row, e->data, e->written);
                }
                ++lc_hits;
                lc_hit_verts += (uint64_t)e->written;
                return e->written;
            }
            if (!e->data && !empty) empty = e;
            if (e->data && e->pin != lc_frame && (!oldest || (int32_t)(e->stamp - oldest->stamp) < 0)) oldest = e;
        }
        slot = empty ? empty : oldest;   /* filled after the conversion below */
        ++lc_misses;
        lc_miss_verts += required;
    }
#if HLE_3DS_GPU_CLIP
    const float sum_before = screen_sum;
#endif
#endif
#if HLE_3DS_GPU_SKIN
    const float sum_start = screen_sum;
    const unsigned behind_start = screen_behind;
    if (gpu) gpu->skin = 0;
    if (gpu && nbones > 0 && bones && layout.weights_count) {
        emit.skin = 1;
        for (int k = 0; k < 3; ++k) { skin_lo[k] = INFINITY; skin_hi[k] = -INFINITY; }
    }
skin_retry:;
#endif
    int written = 0;
    uint32_t vertex_base = 0;
    for (uint32_t i = 0; i < nprims; ++i) {
        const uint32_t word = prim_words[i];
        uint32_t input_count, output_count;
        if (!prim_output_count(word, &input_count, &output_count)) return W3D_C3D_ERR_UNSUPPORTED_PRIM;
        if ((word >> 24) == 0x0Bu) break;
        const uint32_t prim = (word >> 16) & 7u;
        if (prim == 3u) {
            for (uint32_t j = 0; j < output_count; ++j)
                { const int r = lit_emit(&emit, first_vert + vertex_base + j, &output[written++]);
                  if (r < 0) goto skin_fail;
                  if (!r) return W3D_C3D_ERR_UNSUPPORTED_FORMAT; }
        } else if (input_count <= W3D_C3D_STRIP_CACHE) {
            for (uint32_t j = 0; j < input_count; ++j)
                { const int r = lit_emit(&emit, first_vert + vertex_base + j, &strip_cache[j]);
                  if (r < 0) goto skin_fail;
                  if (!r) return W3D_C3D_ERR_UNSUPPORTED_FORMAT; }
            for (uint32_t j = 2; j < input_count; ++j) {
                uint32_t a = j - 2u, b = j - 1u;
                if ((j - 2u) & 1u) { const uint32_t temporary = a; a = b; b = temporary; }
                output[written++] = strip_cache[a];
                output[written++] = strip_cache[b];
                output[written++] = strip_cache[j];
            }
        } else {
            for (uint32_t j = 2; j < input_count; ++j) {
                uint32_t a = j - 2u, b = j - 1u;
                if ((j - 2u) & 1u) { const uint32_t temporary = a; a = b; b = temporary; }
                const int ra = lit_emit(&emit, first_vert + vertex_base + a, &output[written++]);
                const int rb = ra > 0 ? lit_emit(&emit, first_vert + vertex_base + b, &output[written++]) : ra;
                const int rc = rb > 0 ? lit_emit(&emit, first_vert + vertex_base + j, &output[written++]) : rb;
                if (rc < 0) goto skin_fail;
                if (!rc) return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
            }
        }
        vertex_base += input_count;
    }
#if HLE_3DS_GPU_SKIN
    if (emit.skin) {
        gpu->skin = 1;
        skin_rows(gpu, nbones, bones);
        skin_screen(emit.near_row, nbones, bones, skin_lo, skin_hi);
    }
#endif
#if HLE_3DS_LIT_CACHE
    if (slot && written > 0 && (nbones == 0 || emit.skin)) {
        const size_t bytes = (size_t)written * sizeof(W3dC3dVertex);
        if (bytes <= lit_cache_budget / 8u) {
            lc_drop(slot);
            if (lit_cache_bytes + bytes > lit_cache_budget) lc_shrink();
            W3dC3dVertex *data = (W3dC3dVertex *)malloc(bytes);
            if (data) {
                *slot = key;
                slot->data = data;
                slot->written = written;
                slot->stamp = ++lit_cache_clock;
                slot->skin = emit.skin;
                if (emit.skin) { memcpy(slot->lo, skin_lo, sizeof slot->lo); memcpy(slot->hi, skin_hi, sizeof slot->hi); }
#if HLE_3DS_GPU_CLIP
                slot->sum = screen_sum - sum_before;
#else
                slot->sum = 0.0f;
#endif
                memcpy(data, output, bytes);
                lit_cache_bytes += bytes;
            } else {
                ++lc_nomem;
            }
        }
    }
#endif
    return written;
#if HLE_3DS_GPU_SKIN
skin_fail:
    /* a vertex with more than three weights: CPU skinning into object space, as before GPU skinning */
    emit.skin = 0;
    gpu->skin = 0;
    screen_sum = sum_start;
    screen_behind = behind_start;
#if HLE_3DS_LIT_CACHE
    lc_skinned_verts += required;
#endif
    goto skin_retry;
#else
skin_fail:                                  /* unreachable: lit_emit returns -1 only in skin mode */
    return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
#endif
}

/* GPU static path (HLE_3DS_GPU_LIGHT): a static (prelit) mesh range as object-space vertices - position (w 1), the
   vertex colour bytes / 255, UV after the draw's scale - with the camera x world matrix and convert_vertex's
   apply_simple_light (post_mul / post_add, byte-rounded) applied by the shader (objmode). The output depends only on
   the mesh bytes and the UV parameters, so it is cached like a lit range (key marked static). Screening: the same
   per-vertex near row as the GPU-lit path, so the caller keeps its CPU fallback for fogged ranges behind the near
   plane and for non-finite data. */
static int static_emit(const W3dMeshView *mesh, const uint8_t *verts, uint32_t index, const W3dState *state,
                       const float near_row[4], W3dC3dVertex *o)
{
    W3dVC v;
    if (!mesh_vertex(mesh, verts, index, &v)) return 0;
    o->clip[0] = v.x; o->clip[1] = v.y; o->clip[2] = v.z; o->clip[3] = 1.0f;
    for (int channel = 0; channel < 4; ++channel)        /* apply_simple_light's debug mode: opaque white */
        o->color[channel] = HLE_3DS_LIGHTING_DEBUG ? 1.0f : (float)color_component(v.c, channel) * (1.0f / 255.0f);
    o->uv[0] = v.u * state->uscale + state->ubase;
    o->uv[1] = v.v * state->uscale + state->vbase;
    o->view_z = 0.0f;
#if HLE_3DS_GPU_CLIP
    screen_sum += v.x + v.y + v.z + o->uv[0] + o->uv[1];
    const float near = near_row[0] * v.x + near_row[1] * v.y + near_row[2] * v.z + near_row[3];
    if (!(near >= 0.0f)) screen_behind = 1;
#else
    (void)near_row;
#endif
    return 1;
}

int w3d_citro3d_convert_mesh_range_gpu(const W3dCam *camera, const float world[12], const W3dState *state,
                                      const W3dMeshView *mesh, const uint8_t *verts_override, uint32_t first_vert,
                                      const uint32_t *prim_words, uint32_t nprims, W3dC3dVertex *output,
                                      int output_capacity, W3dGpuLight *gpu)
{
    if (!camera || !world || !state || !mesh || !prim_words || !nprims || nprims > mesh->nprims || !gpu ||
        !output || output_capacity < 0 ||
        mesh->nverts == 0 || mesh->stride == 0 || (!verts_override && !mesh->verts) || first_vert >= mesh->nverts)
        return W3D_C3D_ERR_ARGUMENT;
    if (state->textured || state->lights) return W3D_C3D_ERR_UNSUPPORTED_STATE;
    if (mesh->fmt != W3D_FMT_UV16 && mesh->fmt != W3D_FMT_UVF) return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
    if ((mesh->fmt == W3D_FMT_UV16 && mesh->stride < sizeof(W3dV16)) ||
        (mesh->fmt == W3D_FMT_UVF && mesh->stride < sizeof(W3dVF)))
        return W3D_C3D_ERR_ARGUMENT;
    uint64_t need = 0, consumed = 0;
    for (uint32_t i = 0; i < nprims; ++i) {
        uint32_t in_count, out_count;
        if (!prim_output_count(prim_words[i], &in_count, &out_count)) return W3D_C3D_ERR_UNSUPPORTED_PRIM;
        consumed += in_count;
        need += out_count;
        if (first_vert + consumed > mesh->nverts || need > INT_MAX) return W3D_C3D_ERR_ARGUMENT;
        if ((prim_words[i] >> 24) == 0x0B) break;
    }
    if (need > (uint64_t)output_capacity) return W3D_C3D_ERR_CAPACITY;
    const uint8_t *verts = verts_override ? verts_override : mesh->verts;
    w3d_citro3d_cache_hit = NULL;

    /* the batch's uniforms: camera x world, the material step of apply_simple_light */
    memset(gpu, 0, sizeof *gpu);
    gpu->mode = 1;
    memcpy(gpu->world, world, sizeof gpu->world);
    memcpy(gpu->proj, camera->proj, sizeof gpu->proj);
    if (state->light && !HLE_3DS_LIGHTING_DEBUG) {
        for (int c = 0; c < 4; ++c) gpu->post_mul[c] = (float)color_component(state->lamb, c) * (1.0f / 255.0f);
        for (int c = 0; c < 3; ++c) gpu->post_add[c] = (float)color_component(state->lemis, c) * (1.0f / 255.0f);
    } else {
        for (int c = 0; c < 4; ++c) gpu->post_mul[c] = 1.0f;
    }
    float near_row[4];
    {
        const float *m = camera->proj;
        for (int c = 0; c < 4; ++c) {
            float v = c == 3 ? m[14] + m[15] : 0.0f;
            for (int j = 0; j < 3; ++j) v += (m[4 * j + 2] + m[4 * j + 3]) * world[c < 3 ? 3 * c + j : 9 + j];
            near_row[c] = v;
        }
    }

#if HLE_3DS_LIT_CACHE
    LitCacheEntry key, *slot = NULL;
    if (lit_cache_budget && need) {
        memset(&key, 0, sizeof key);
        key.vertices = (uintptr_t)verts;
        key.first_vert = first_vert;
        key.count = (uint32_t)consumed;
        key.nprims = nprims;
        key.vtype = 0x40000000u | (uint32_t)mesh->fmt;     /* static data: never equal to a lit vertex type */
        key.stride = mesh->stride;
        key.nbones = -1;
        memcpy(&key.uvbits[0], &state->uscale, 4);
        memcpy(&key.uvbits[1], &state->ubase, 4);
        memcpy(&key.uvbits[2], &state->vbase, 4);
        uint32_t h = lc_words(0x811C9DC5u, verts + (size_t)first_vert * mesh->stride, (size_t)consumed * mesh->stride);
        key.hash = lc_words(h, (const uint8_t *)prim_words, (size_t)nprims * 4u);
        const uint32_t home = lc_slot(&key);
        LitCacheEntry *empty = NULL, *oldest = NULL;
        for (uint32_t k = 0; k < LIT_CACHE_PROBE; ++k) {
            LitCacheEntry *e = &lit_cache[(home + k) & (LIT_CACHE_SLOTS - 1u)];
            if (e->data && lc_same(e, &key)) {
                e->stamp = ++lit_cache_clock;
                if (w3d_citro3d_cache_by_ref) { e->pin = lc_frame; w3d_citro3d_cache_hit = e->data; }
                else memcpy(output, e->data, (size_t)e->written * sizeof *output);
#if HLE_3DS_GPU_CLIP
                screen_sum += e->sum;
#endif
                lc_screen(near_row, e->data, e->written);
                ++lc_static_hits;
                lc_static_hit_verts += (uint64_t)e->written;
                return e->written;
            }
            if (!e->data && !empty) empty = e;
            if (e->data && e->pin != lc_frame && (!oldest || (int32_t)(e->stamp - oldest->stamp) < 0)) oldest = e;
        }
        slot = empty ? empty : oldest;
        ++lc_static_misses;
        lc_static_miss_verts += need;
    }
#if HLE_3DS_GPU_CLIP
    const float sum_before = screen_sum;
#endif
#endif

    uint32_t at = 0;
    int written = 0;
    for (uint32_t i = 0; i < nprims; ++i) {
        const uint32_t word = prim_words[i];
        uint32_t in_count, out_count;
        if (!prim_output_count(word, &in_count, &out_count)) return W3D_C3D_ERR_UNSUPPORTED_PRIM;
        if ((word >> 24) == 0x0B) break;
        const uint32_t prim = (word >> 16) & 7u;
        if (prim == 3) {
            for (uint32_t j = 0; j < out_count; ++j)
                if (!static_emit(mesh, verts, first_vert + at + j, state, near_row, &output[written++]))
                    return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
        } else if (in_count <= W3D_C3D_STRIP_CACHE) {
            for (uint32_t j = 0; j < in_count; ++j)
                if (!static_emit(mesh, verts, first_vert + at + j, state, near_row, &strip_cache[j]))
                    return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
            for (uint32_t j = 2; j < in_count; ++j) {
                uint32_t a = j - 2u, b = j - 1u;
                if ((j - 2u) & 1u) { const uint32_t tmp = a; a = b; b = tmp; }
                output[written++] = strip_cache[a];
                output[written++] = strip_cache[b];
                output[written++] = strip_cache[j];
            }
        } else {
            for (uint32_t j = 2; j < in_count; ++j) {
                uint32_t a = j - 2u, b = j - 1u, c = j;
                if ((j - 2u) & 1u) { const uint32_t tmp = a; a = b; b = tmp; }
                if (!static_emit(mesh, verts, first_vert + at + a, state, near_row, &output[written++]) ||
                    !static_emit(mesh, verts, first_vert + at + b, state, near_row, &output[written++]) ||
                    !static_emit(mesh, verts, first_vert + at + c, state, near_row, &output[written++]))
                    return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
            }
        }
        at += in_count;
    }
#if HLE_3DS_LIT_CACHE
    if (slot && written > 0) {
        const size_t bytes = (size_t)written * sizeof(W3dC3dVertex);
        if (bytes <= lit_cache_budget / 8u) {
            lc_drop(slot);
            if (lit_cache_bytes + bytes > lit_cache_budget) lc_shrink();
            W3dC3dVertex *data = (W3dC3dVertex *)malloc(bytes);
            if (data) {
                *slot = key;
                slot->data = data;
                slot->written = written;
                slot->stamp = ++lit_cache_clock;
#if HLE_3DS_GPU_CLIP
                slot->sum = screen_sum - sum_before;
#endif
                memcpy(data, output, bytes);
                lit_cache_bytes += bytes;
            } else {
                ++lc_nomem;
            }
        }
    }
#endif
    return written;
}

int w3d_citro3d_convert_lit_range(const W3dCam *camera, const float world[12], const W3dState *state,
                                 const W3dState *lit_state, const W3dLights *lights, int nbones,
                                 const float bones[8][12], const W3dMeshView *mesh, const uint8_t *verts_override,
                                 uint32_t first_vert, const uint32_t *prim_words, uint32_t nprims,
                                 W3dC3dVertex *output, int output_capacity)
{
    return lit_range(camera, world, state, lit_state, lights, nbones, bones, mesh, verts_override, first_vert,
                     prim_words, nprims, output, output_capacity, NULL);
}

int w3d_citro3d_convert_lit_range_gpu(const W3dCam *camera, const float world[12], const W3dState *state,
                                     const W3dState *lit_state, const W3dLights *lights, int nbones,
                                     const float bones[8][12], const W3dMeshView *mesh, const uint8_t *verts_override,
                                     uint32_t first_vert, const uint32_t *prim_words, uint32_t nprims,
                                     W3dC3dVertex *output, int output_capacity, W3dGpuLight *gpu)
{
    if (!gpu) return W3D_C3D_ERR_ARGUMENT;
    w3d_citro3d_cache_hit = NULL;
    return lit_range(camera, world, state, lit_state, lights, nbones, bones, mesh, verts_override, first_vert,
                     prim_words, nprims, output, output_capacity, gpu);
}
