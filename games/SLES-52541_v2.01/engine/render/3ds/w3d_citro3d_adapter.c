#include "w3d_citro3d_adapter.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
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

static int lit_vertex(const float world[12], const W3dLitPrepared *prepared,
                      int nbones, const float bones[8][12], const W3dLitLayout *layout,
                      const W3dMeshView *mesh, const uint8_t *vertices, uint32_t index, W3dVC *out)
{
    if (index >= mesh->nverts) return 0;
    const uint8_t *v = vertices + (size_t)index * mesh->stride;
    float position[3], normal[3] = {0.0f, 0.0f, 1.0f};
    float uv[2] = {0.0f, 0.0f};
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
        memcpy(position, skinned_position, sizeof(position));
        memcpy(normal, skinned_normal, sizeof(normal));
    }

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

int w3d_citro3d_convert_lit_range(const W3dCam *camera, const float world[12], const W3dState *state,
                                 const W3dState *lit_state, const W3dLights *lights, int nbones,
                                 const float bones[8][12], const W3dMeshView *mesh, const uint8_t *verts_override,
                                 uint32_t first_vert, const uint32_t *prim_words, uint32_t nprims,
                                 W3dC3dVertex *output, int output_capacity)
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
    int written = 0;
    uint32_t vertex_base = 0;
    for (uint32_t i = 0; i < nprims; ++i) {
        const uint32_t word = prim_words[i];
        uint32_t input_count, output_count;
        if (!prim_output_count(word, &input_count, &output_count)) return W3D_C3D_ERR_UNSUPPORTED_PRIM;
        if ((word >> 24) == 0x0Bu) break;
        const uint32_t prim = (word >> 16) & 7u;
        if (prim == 3u) {
            for (uint32_t j = 0; j < output_count; ++j) {
                W3dVC vertex;
                if (!lit_vertex(world, &prepared, nbones, bones, &layout, mesh, vertices,
                                first_vert + vertex_base + j, &vertex)) return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
                convert_vertex(camera, world, state, &vertex, &output[written++]);
            }
        } else if (input_count <= W3D_C3D_STRIP_CACHE) {
            for (uint32_t j = 0; j < input_count; ++j) {
                W3dVC vertex;
                if (!lit_vertex(world, &prepared, nbones, bones, &layout, mesh, vertices,
                                first_vert + vertex_base + j, &vertex)) return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
                convert_vertex(camera, world, state, &vertex, &strip_cache[j]);
            }
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
                W3dVC va, vb, vc;
                if (!lit_vertex(world, &prepared, nbones, bones, &layout, mesh, vertices,
                                first_vert + vertex_base + a, &va) ||
                    !lit_vertex(world, &prepared, nbones, bones, &layout, mesh, vertices,
                                first_vert + vertex_base + b, &vb) ||
                    !lit_vertex(world, &prepared, nbones, bones, &layout, mesh, vertices,
                                first_vert + vertex_base + j, &vc)) return W3D_C3D_ERR_UNSUPPORTED_FORMAT;
                convert_vertex(camera, world, state, &va, &output[written++]);
                convert_vertex(camera, world, state, &vb, &output[written++]);
                convert_vertex(camera, world, state, &vc, &output[written++]);
            }
        }
        vertex_base += input_count;
    }
    return written;
}
