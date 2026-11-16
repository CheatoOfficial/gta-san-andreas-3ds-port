/* Frozen pre-preparation colour math as an independent exact-rounding oracle. Synthetic inputs only. */
#include "w3d_citro3d_adapter.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint32_t reference_colour(const float normal[3], const W3dState *state, const W3dLights *lights)
{
#if HLE_3DS_LIGHTING_DEBUG
    (void)normal;
    (void)state;
    (void)lights;
    return 0xFFFFFFFFu;
#else
    if (!state->lights || !lights) return 0xFFFFFFFFu;
    const float normal_length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    uint32_t colour = state->lamb & 0xFF000000u;
    const unsigned ambient = state->lemis & 0xFFu;
    const unsigned diffuse = (state->lemis >> 8) & 0xFFu;
    int nlights = lights->n;
    if (nlights < 0) nlights = 0;
    if (nlights > 4) nlights = 4;
    if (nlights > state->lights - 1) nlights = state->lights - 1;
    const float inverse_length = normal_length > 0.0f ? 1.0f / normal_length : 0.0f;
    const float diffuse_scale = (float)diffuse * (1.0f / 255.0f);
    const float ambient_scale = (float)ambient * (1.0f / 255.0f);
    float light_factor[4];
    for (int light = 0; light < nlights; ++light) {
        const float direction = (normal[0] * lights->dir[light][0] + normal[1] * lights->dir[light][1] +
                                 normal[2] * lights->dir[light][2]) * inverse_length;
        light_factor[light] = direction > 0.0f ? diffuse_scale * direction : 0.0f;
    }
    for (int channel = 0; channel < 3; ++channel) {
        const float material = (float)((state->lamb >> (8 * channel)) & 0xFFu) * (1.0f / 255.0f);
        float value = (float)((state->lemit >> (8 * channel)) & 0xFFu) * (1.0f / 255.0f) +
                      material * ambient_scale * lights->amb[channel];
        for (int light = 0; light < nlights; ++light)
            if (light_factor[light] > 0.0f) value += material * light_factor[light] * lights->col[light][channel];
        int component = (int)(value * 255.0f + 0.5f);
        if (component < 0) component = 0;
        if (component > 255) component = 255;
        colour |= (uint32_t)component << (8 * channel);
    }
    return colour;
#endif
}

static uint32_t rng = 17;
static uint32_t random_word(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static float random_float(void) { return (float)(random_word() & 65535) / 16384.0f - 2.0f; }
int main(void)
{
    W3dCam camera = {0};
    camera.proj[0] = camera.proj[5] = camera.proj[10] = camera.proj[15] = 1;
    W3dState state = {0}; state.uscale = 1;
    W3dMeshView mesh = {0};
    mesh.fmt = W3D_FMT_LIT; mesh.vtype = 3u | (3u << 5) | (3u << 7);
    mesh.nverts = 3; mesh.nprims = 1; mesh.stride = 8 * sizeof(float);
    float vertices[3][8]; mesh.verts = (const uint8_t *)vertices;
    W3dC3dVertex output[3];
    for (int test = 0; test < 12000; ++test) {
        W3dState material = {0};
        material.lamb = random_word(); material.lemis = random_word(); material.lemit = random_word();
        material.lights = test % 9 - 2;
        W3dLights lights = {0}; lights.n = test % 9 - 2;
        for (int c = 0; c < 3; ++c) lights.amb[c] = random_float();
        for (int l = 0; l < 4; ++l)
            for (int c = 0; c < 3; ++c) { lights.col[l][c] = random_float(); lights.dir[l][c] = random_float(); }
        float world[12];
        for (int i = 0; i < 12; ++i) world[i] = random_float();
        for (int v = 0; v < 3; ++v)
            for (int k = 0; k < 8; ++k) vertices[v][k] = test % 19 ? random_float() : 0;
        const W3dLights *active = test % 17 ? &lights : NULL;
        const uint32_t primitive = 0x04000003u | ((test & 1 ? 4u : 3u) << 16);
        assert(w3d_citro3d_convert_lit_range(&camera, world, &state, &material, active, 0, NULL,
                                           &mesh, NULL, 0, &primitive, 1, output, 3) == 3);
        for (int v = 0; v < 3; ++v) {
            float normal[3];
            for (int c = 0; c < 3; ++c)
                normal[c] = vertices[v][2] * world[c] + vertices[v][3] * world[3+c] + vertices[v][4] * world[6+c];
            const uint32_t expected = reference_colour(normal, &material, active);
            for (int c = 0; c < 4; ++c) {
                const float component = (float)((expected >> (c*8)) & 255u) * (1.0f/255.0f);
                assert(memcmp(&component, &output[v].color[c], sizeof(component)) == 0);
            }
        }
    }
    puts("12000 lighting calls: exact colour bytes, changing state/counts, null lights, clamps and zero normals");
    return 0;
}
