#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "w3d_citro3d_adapter.h"

int old_mesh(const W3dCam*, const float*, const W3dState*, const W3dMeshView*, const uint8_t*, uint32_t,
             const uint32_t*, uint32_t, W3dC3dVertex*, int);
int old_lit(const W3dCam*, const float*, const W3dState*, const W3dState*, const W3dLights*, int,
            const float (*)[12], const W3dMeshView*, const uint8_t*, uint32_t, const uint32_t*, uint32_t,
            W3dC3dVertex*, int);

static float rf(void) { return (float)rand() / RAND_MAX * 2.0f - 1.0f; }

static int same(const W3dC3dVertex *a, const W3dC3dVertex *b, int n, int *colour_diffs)
{
    for (int i = 0; i < n; i++) {
        const float *x = (const float *)&a[i], *y = (const float *)&b[i];
        for (int k = 0; k < 11; k++) {
            const int colour = k >= 4 && k < 8;
            const float tol = colour ? 0.0045f : 1e-4f * (1.0f + fabsf(x[k]));
            if (fabsf(x[k] - y[k]) > tol) return 0;
            if (colour && x[k] != y[k]) (*colour_diffs)++;
        }
    }
    return 1;
}

int main(void)
{
    srand(7);
    int bad = 0, colour_diffs = 0, lit_cases = 0, mesh_cases = 0;
    static W3dC3dVertex a[8000], b[8000];
    for (int iter = 0; iter < 3000; iter++) {
        W3dCam cam;
        for (int i = 0; i < 16; i++) cam.proj[i] = rf();
        cam.near_z = .3f; cam.far_z = 450; cam.fog_z = 100; cam.camera = 0;
        float world[12];
        for (int i = 0; i < 12; i++) world[i] = rf();
        W3dState st;
        memset(&st, 0, sizeof st);
        st.uscale = 1; st.ubase = .25f; st.vbase = .5f; st.light = iter & 1; st.lamb = rand(); st.lemis = rand();
        W3dState lit = st;
        lit.lights = 1 + (rand() % 5); lit.lemit = rand();
        W3dLights L;
        L.n = rand() % 5;
        for (int i = 0; i < 3; i++) L.amb[i] = (rf() + 1) / 2;
        for (int l = 0; l < 4; l++)
            for (int i = 0; i < 3; i++) { L.dir[l][i] = rf(); L.col[l][i] = (rf() + 1) / 2; }
        float bones[8][12];
        for (int bn = 0; bn < 8; bn++)
            for (int i = 0; i < 12; i++) bones[bn][i] = rf();
        const int big = iter % 50 == 0;
        const int strip = rand() & 1;
        int nverts = 3 + rand() % (big ? 700 : 40);
        if (!strip) nverts = (nverts / 3) * 3;
        if (nverts < 3) continue;
        uint8_t *vb = calloc((size_t)nverts, 64);
        for (int v = 0; v < nverts; v++) {
            float *f = (float *)(vb + v * 64);
            for (int k = 0; k < 14; k++) f[k] = rf();
        }
        uint32_t words[3] = {0x04000000u | ((strip ? 4u : 3u) << 16) | (uint32_t)nverts, 0x0B000000u, 0};
        W3dMeshView mv;
        memset(&mv, 0, sizeof mv);
        mv.verts = vb; mv.nverts = (uint32_t)nverts; mv.nprims = 2; mv.stride = 64; mv.fmt = W3D_FMT_UVF;
        int ra = old_mesh(&cam, world, &st, &mv, NULL, 0, words, 2, a, 8000);
        int rb = w3d_citro3d_convert_mesh_range(&cam, world, &st, &mv, NULL, 0, words, 2, b, 8000);
        mesh_cases++;
        if (ra != rb || (ra > 0 && !same(a, b, ra, &colour_diffs))) {
            if (bad < 4) printf("mesh iter %d strip %d nverts %d: old %d new %d\n", iter, strip, nverts, ra, rb);
            bad++;
        }
        mv.fmt = W3D_FMT_LIT;
        mv.vtype = 3u | (3u << 5) | (3u << 7) | (3u << 9) | (1u << 14);
        const int nb = 2 + rand() % 7;
        ra = old_lit(&cam, world, &st, &lit, &L, nb, bones, &mv, NULL, 0, words, 2, a, 8000);
        rb = w3d_citro3d_convert_lit_range(&cam, world, &st, &lit, &L, nb, bones, &mv, NULL, 0, words, 2, b, 8000);
        lit_cases++;
        if (ra != rb || (ra > 0 && !same(a, b, ra, &colour_diffs))) {
            if (bad < 4) printf("lit iter %d strip %d nverts %d: old %d new %d\n", iter, strip, nverts, ra, rb);
            bad++;
        }
        free(vb);
    }
    printf("cases mesh=%d lit=%d mismatches=%d (lit colour 1-level rounding diffs=%d)\n", mesh_cases, lit_cases, bad,
           colour_diffs);

    /* GPU-lit conversion cache: a miss, a hit and the uncached conversion must be bit-identical, and changed vertex
       bytes at the same address must be a miss (fresh output), never the stored data */
    int cache_bad = 0, cache_cases = 0;
    static W3dC3dVertex r[8000];
    uint64_t st7[7];
    for (int iter = 0; iter < 300; iter++) {
        W3dCam cam;
        for (int i = 0; i < 16; i++) cam.proj[i] = rf();
        cam.near_z = .3f; cam.far_z = 450; cam.fog_z = 100; cam.camera = 0;
        float world[12];
        for (int i = 0; i < 12; i++) world[i] = rf();
        W3dState st;
        memset(&st, 0, sizeof st);
        st.uscale = 1; st.ubase = .25f; st.vbase = .5f; st.light = iter & 1; st.lamb = rand(); st.lemis = rand();
        W3dState lit = st;
        lit.lights = 1 + (rand() % 5); lit.lemit = rand();
        W3dLights L;
        memset(&L, 0, sizeof L);
        L.n = rand() % 5;
        const int strip = rand() & 1;
        int nverts = 3 + rand() % 300;
        if (!strip) nverts = (nverts / 3) * 3;
        if (nverts < 3) continue;
        uint8_t *vb = calloc((size_t)nverts, 64);
        for (int v = 0; v < nverts; v++) {
            float *f = (float *)(vb + v * 64);
            for (int k = 0; k < 14; k++) f[k] = rf();
        }
        uint32_t words[3] = {0x04000000u | ((strip ? 4u : 3u) << 16) | (uint32_t)nverts, 0x0B000000u, 0};
        W3dMeshView mv;
        memset(&mv, 0, sizeof mv);
        mv.verts = vb; mv.nverts = (uint32_t)nverts; mv.nprims = 2; mv.stride = 64; mv.fmt = W3D_FMT_LIT;
        mv.vtype = 3u | (3u << 5) | (3u << 7) | (3u << 9) | (1u << 14);
        W3dGpuLight g;
        w3d_citro3d_lit_cache_config(0);
        const int r0 = w3d_citro3d_convert_lit_range_gpu(&cam, world, &st, &lit, &L, 0, NULL, &mv, NULL, 0, words, 2, r, 8000, &g);
        const unsigned s0 = w3d_citro3d_take_screen();
        w3d_citro3d_lit_cache_config(1u << 20);
        (void)w3d_citro3d_lit_cache_stats(st7, NULL);
        const int r1 = w3d_citro3d_convert_lit_range_gpu(&cam, world, &st, &lit, &L, 0, NULL, &mv, NULL, 0, words, 2, a, 8000, &g);
        const unsigned s1 = w3d_citro3d_take_screen();
        const int r2 = w3d_citro3d_convert_lit_range_gpu(&cam, world, &st, &lit, &L, 0, NULL, &mv, NULL, 0, words, 2, b, 8000, &g);
        const unsigned s2 = w3d_citro3d_take_screen();
        w3d_citro3d_lit_cache_stats(st7, NULL);
        cache_cases++;
        if (r0 <= 0 || r1 != r0 || r2 != r0 || memcmp(a, r, (size_t)r0 * sizeof *r) || memcmp(b, r, (size_t)r0 * sizeof *r) ||
            s1 != s0 || s2 != s0 || st7[0] != 1 || st7[1] != 1) {
            if (cache_bad < 4) printf("cache iter %d: r %d/%d/%d hits %llu misses %llu screen %u/%u/%u\n", iter, r0, r1, r2,
                                      (unsigned long long)st7[0], (unsigned long long)st7[1], s0, s1, s2);
            cache_bad++;
        }
        /* same address, one vertex changed: the cached output must not come back */
        ((float *)(vb + (size_t)(rand() % nverts) * 64))[rand() % 3] += 0.5f;
        w3d_citro3d_lit_cache_config(0);
        const int r3 = w3d_citro3d_convert_lit_range_gpu(&cam, world, &st, &lit, &L, 0, NULL, &mv, NULL, 0, words, 2, r, 8000, &g);
        w3d_citro3d_lit_cache_config(1u << 20);
        const int r4 = w3d_citro3d_convert_lit_range_gpu(&cam, world, &st, &lit, &L, 0, NULL, &mv, NULL, 0, words, 2, a, 8000, &g);
        (void)w3d_citro3d_take_screen();
        w3d_citro3d_lit_cache_stats(st7, NULL);
        if (r3 != r4 || memcmp(a, r, (size_t)r3 * sizeof *r) || st7[0] != 0) {
            if (cache_bad < 4) printf("cache iter %d: changed vertex served from the cache (hits %llu)\n", iter,
                                      (unsigned long long)st7[0]);
            cache_bad++;
        }
        free(vb);
    }
    printf("lit cache cases=%d mismatches=%d\n", cache_cases, cache_bad);
    return bad != 0 || cache_bad != 0;
}
