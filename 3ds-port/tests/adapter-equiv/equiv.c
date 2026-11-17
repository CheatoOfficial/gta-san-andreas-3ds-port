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

    /* GPU skinning: the shader's skin procedure (vshader.v.pica), emulated here from the packed output and the bone
       rows, must give sum weight * bone * (p, 1) for every vertex; a vertex with four weights must take the CPU path */
    int skin_bad = 0, skin_cases = 0, skin_rejects = 0;
    w3d_citro3d_lit_cache_config(1u << 20);
    for (int iter = 0; iter < 300; iter++) {
        W3dCam cam;
        for (int i = 0; i < 16; i++) cam.proj[i] = rf();
        cam.near_z = .3f; cam.far_z = 450; cam.fog_z = 100; cam.camera = 0;
        float world[12];
        for (int i = 0; i < 12; i++) world[i] = rf();
        W3dState st;
        memset(&st, 0, sizeof st);
        st.uscale = 1;
        W3dState lit = st;
        lit.lights = 1;
        W3dLights L;
        memset(&L, 0, sizeof L);
        float bones[8][12];
        for (int bn = 0; bn < 8; bn++)
            for (int i = 0; i < 12; i++) bones[bn][i] = rf();
        const int four = iter % 10 == 9;              /* every tenth mesh: four weights on one vertex */
        const int nb = 4 + rand() % 5;
        int nverts = 3 * (1 + rand() % 60);
        /* GE layout: 4 float weights (+0), float UV (+16), float normal (+24), float position (+36): 48 bytes */
        uint8_t *vb = calloc((size_t)nverts, 48);
        for (int v = 0; v < nverts; v++) {
            float *f = (float *)(vb + v * 48);
            const int b0 = rand() % 4, b1 = (b0 + 1 + rand() % 3) % 4;
            const float w = (float)(rand() % 129) / 128.0f;
            f[b0] = w; f[b1] = 1.0f - w;
            if (four && v == nverts / 2) { f[0] = .25f; f[1] = .25f; f[2] = .25f; f[3] = .25f; }
            for (int k = 4; k < 12; k++) f[k] = rf();
        }
        uint32_t words[3] = {0x04000000u | (3u << 16) | (uint32_t)nverts, 0x0B000000u, 0};
        W3dMeshView mv;
        memset(&mv, 0, sizeof mv);
        mv.verts = vb; mv.nverts = (uint32_t)nverts; mv.nprims = 2; mv.stride = 48; mv.fmt = W3D_FMT_LIT;
        mv.vtype = 3u | (3u << 5) | (3u << 7) | (3u << 9) | (3u << 14);
        W3dGpuLight g;
        const int r1 = w3d_citro3d_convert_lit_range_gpu(&cam, world, &st, &lit, &L, nb, bones, &mv, NULL, 0, words, 2, a, 8000, &g);
        (void)w3d_citro3d_take_screen();
        skin_cases++;
        if (r1 != nverts) { if (skin_bad < 4) printf("skin iter %d: written %d of %d\n", iter, r1, nverts); skin_bad++; free(vb); continue; }
        if (four) {
            skin_rejects += !g.skin;
            if (g.skin) { if (skin_bad < 4) printf("skin iter %d: four weights not rejected\n", iter); skin_bad++; }
            free(vb);
            continue;
        }
        if (!g.skin) { if (skin_bad < 4) printf("skin iter %d: not GPU-skinned\n", iter); skin_bad++; free(vb); continue; }
        for (int v = 0; v < nverts && skin_bad < 1000; v++) {
            const float *f = (const float *)(vb + v * 48);
            float want[3] = {0, 0, 0}, got[3] = {0, 0, 0};
            for (int b = 0; b < 4; b++)
                for (int k = 0; k < 3; k++)
                    want[k] += f[b] * (f[9] * bones[b][k] + f[10] * bones[b][3 + k] + f[11] * bones[b][6 + k] + bones[b][9 + k]);
            const float packed[3] = {a[v].clip[3], a[v].color[3], a[v].view_z};
            for (int q = 0; q < 3; q++) {
                const float bi = floorf(packed[q]), w = (packed[q] - bi) * 2.0f;
                const int row = 3 * (int)bi;
                for (int k = 0; k < 3; k++)
                    got[k] += w * (g.bones[row + k][0] * a[v].clip[0] + g.bones[row + k][1] * a[v].clip[1] +
                                   g.bones[row + k][2] * a[v].clip[2] + g.bones[row + k][3]);
            }
            for (int k = 0; k < 3; k++)
                if (fabsf(got[k] - want[k]) > 1e-4f * (1.0f + fabsf(want[k]))) {
                    if (skin_bad < 4) printf("skin iter %d vertex %d: %f vs %f\n", iter, v, got[k], want[k]);
                    skin_bad++;
                    break;
                }
        }
        /* second draw with other bones: a cache hit, the same vertex data, the new bone rows */
        for (int bn = 0; bn < 8; bn++) bones[bn][9] += 1.0f;
        const int r2 = w3d_citro3d_convert_lit_range_gpu(&cam, world, &st, &lit, &L, nb, bones, &mv, NULL, 0, words, 2, b, 8000, &g);
        (void)w3d_citro3d_take_screen();
        if (r2 != r1 || memcmp(a, b, (size_t)r1 * sizeof *a) || !g.skin || g.bones[0][3] != bones[0][9]) {
            if (skin_bad < 4) printf("skin iter %d: cached redraw differs\n", iter);
            skin_bad++;
        }
        free(vb);
    }
    w3d_citro3d_lit_cache_config(0);
    printf("gpu skin cases=%d mismatches=%d (four-weight rejects %d)\n", skin_cases, skin_bad, skin_rejects);

    /* GPU static path: the shader's objmode (camera x world on the object-space position, the material step on the
       vertex colour, emulated here) must give the CPU static conversion's clip position, colour and UV; a second
       draw is a cache hit with the same data */
    int static_bad = 0, static_cases = 0;
    w3d_citro3d_lit_cache_config(1u << 20);
    for (int iter = 0; iter < 300; iter++) {
        W3dCam cam;
        for (int i = 0; i < 16; i++) cam.proj[i] = rf();
        cam.near_z = .3f; cam.far_z = 450; cam.fog_z = 100; cam.camera = 0;
        float world[12];
        for (int i = 0; i < 12; i++) world[i] = rf();
        W3dState st;
        memset(&st, 0, sizeof st);
        st.uscale = 1; st.ubase = .25f; st.vbase = .5f; st.light = iter & 1; st.lamb = rand(); st.lemis = rand();
        const int strip = rand() & 1;
        int nverts = 3 + rand() % 300;
        if (!strip) nverts = (nverts / 3) * 3;
        if (nverts < 3) continue;
        uint8_t *vb = calloc((size_t)nverts, 64);
        for (int v = 0; v < nverts; v++) {
            float *f = (float *)(vb + v * 64);
            for (int k = 0; k < 14; k++) f[k] = rf();
            ((uint32_t *)f)[2] = (uint32_t)rand() ^ ((uint32_t)rand() << 16);   /* vertex colour bytes */
        }
        uint32_t words[3] = {0x04000000u | ((strip ? 4u : 3u) << 16) | (uint32_t)nverts, 0x0B000000u, 0};
        W3dMeshView mv;
        memset(&mv, 0, sizeof mv);
        mv.verts = vb; mv.nverts = (uint32_t)nverts; mv.nprims = 2; mv.stride = 64; mv.fmt = W3D_FMT_UVF;
        W3dGpuLight g;
        const int rc = w3d_citro3d_convert_mesh_range(&cam, world, &st, &mv, NULL, 0, words, 2, r, 8000);
        (void)w3d_citro3d_take_screen();
        const int rg = w3d_citro3d_convert_mesh_range_gpu(&cam, world, &st, &mv, NULL, 0, words, 2, a, 8000, &g);
        (void)w3d_citro3d_take_screen();
        const int rh = w3d_citro3d_convert_mesh_range_gpu(&cam, world, &st, &mv, NULL, 0, words, 2, b, 8000, &g);
        (void)w3d_citro3d_take_screen();
        static_cases++;
        if (rc <= 0 || rg != rc || rh != rc || memcmp(a, b, (size_t)rc * sizeof *a) || g.mode != 1) {
            if (static_bad < 4) printf("static iter %d: written %d/%d/%d mode %d\n", iter, rc, rg, rh, g.mode);
            static_bad++;
            free(vb);
            continue;
        }
        for (int v = 0; v < rc; v++) {
            /* clip = proj x (world x p): the backend folds them into one matrix; emulate in the CPU's order */
            const float *p = a[v].clip, *w = g.world, *m = g.proj;
            const float x = p[0] * w[0] + p[1] * w[3] + p[2] * w[6] + w[9];
            const float y = p[0] * w[1] + p[1] * w[4] + p[2] * w[7] + w[10];
            const float z = p[0] * w[2] + p[1] * w[5] + p[2] * w[8] + w[11];
            float clip[4];
            for (int i = 0; i < 4; i++) clip[i] = x * m[i] + y * m[4 + i] + z * m[8 + i] + m[12 + i];
            int ok = 1;
            for (int i = 0; i < 4; i++) ok &= fabsf(clip[i] - r[v].clip[i]) <= 1e-4f * (1.0f + fabsf(r[v].clip[i]));
            for (int c = 0; c < 4; c++) {
                float q = a[v].color[c] * g.post_mul[c];
                q = floorf(q * 255.0f + 0.5f) / 255.0f;
                if (c < 3) q += g.post_add[c];
                if (q > 1.0f) q = 1.0f;
                ok &= fabsf(q - r[v].color[c]) <= 0.0045f;
            }
            ok &= fabsf(a[v].uv[0] - r[v].uv[0]) <= 1e-6f && fabsf(a[v].uv[1] - r[v].uv[1]) <= 1e-6f;
            ok &= fabsf(z - r[v].view_z) <= 1e-4f * (1.0f + fabsf(z));
            if (!ok) {
                if (static_bad < 4) printf("static iter %d vertex %d differs\n", iter, v);
                static_bad++;
                break;
            }
        }
        free(vb);
    }
    w3d_citro3d_lit_cache_config(0);
    printf("gpu static cases=%d mismatches=%d\n", static_cases, static_bad);

    /* by reference: a hit leaves the output alone and hands back the stored vertices; an entry served this way stays
       (is not evicted) until the next frame even when the cache is over budget */
    int ref_bad = 0;
    {
        W3dCam cam;
        for (int i = 0; i < 16; i++) cam.proj[i] = rf();
        cam.near_z = .3f; cam.far_z = 450; cam.fog_z = 100; cam.camera = 0;
        float world[12];
        for (int i = 0; i < 12; i++) world[i] = rf();
        W3dState st;
        memset(&st, 0, sizeof st);
        st.uscale = 1;
        enum { MESHES = 40, NV = 300 };
        uint8_t *vbs[MESHES];
        W3dMeshView mvs[MESHES];
        uint32_t words[3] = {0x04000000u | (3u << 16) | NV, 0x0B000000u, 0};
        for (int m = 0; m < MESHES; m++) {
            vbs[m] = calloc(NV, 64);
            for (int v = 0; v < NV; v++)
                for (int k = 0; k < 14; k++) ((float *)(vbs[m] + v * 64))[k] = rf();
            memset(&mvs[m], 0, sizeof mvs[m]);
            mvs[m].verts = vbs[m]; mvs[m].nverts = NV; mvs[m].nprims = 2; mvs[m].stride = 64; mvs[m].fmt = W3D_FMT_UVF;
        }
        W3dGpuLight g;
        /* budget for ~4 meshes: 300 verts x 44 bytes each */
        w3d_citro3d_lit_cache_config((size_t)NV * sizeof(W3dC3dVertex) * 8u + 1024u);
        w3d_citro3d_cache_by_ref = 1;
        int r0 = w3d_citro3d_convert_mesh_range_gpu(&cam, world, &st, &mvs[0], NULL, 0, words, 2, r, 8000, &g);
        (void)w3d_citro3d_take_screen();
        if (r0 != NV || w3d_citro3d_cache_hit) { printf("by-ref: first draw not a miss\n"); ref_bad++; }
        memset(a, 0x5A, sizeof(W3dC3dVertex) * NV);
        int r1 = w3d_citro3d_convert_mesh_range_gpu(&cam, world, &st, &mvs[0], NULL, 0, words, 2, a, 8000, &g);
        (void)w3d_citro3d_take_screen();
        const W3dC3dVertex *pinned = w3d_citro3d_cache_hit;
        if (r1 != NV || !pinned || memcmp(pinned, r, sizeof(W3dC3dVertex) * NV) || ((uint8_t *)a)[0] != 0x5A) {
            printf("by-ref: hit not served by reference\n"); ref_bad++;
        }
        for (int m = 1; m < MESHES; m++) {      /* pressure in the same frame */
            (void)w3d_citro3d_convert_mesh_range_gpu(&cam, world, &st, &mvs[m], NULL, 0, words, 2, b, 8000, &g);
            (void)w3d_citro3d_take_screen();
        }
        int r2 = w3d_citro3d_convert_mesh_range_gpu(&cam, world, &st, &mvs[0], NULL, 0, words, 2, a, 8000, &g);
        (void)w3d_citro3d_take_screen();
        if (r2 != NV || w3d_citro3d_cache_hit != pinned || memcmp(pinned, r, sizeof(W3dC3dVertex) * NV)) {
            printf("by-ref: pinned entry evicted within its frame\n"); ref_bad++;
        }
        w3d_citro3d_cache_new_frame();
        for (int m = 1; m < MESHES; m++) {      /* next frame: the old entry may go */
            (void)w3d_citro3d_convert_mesh_range_gpu(&cam, world, &st, &mvs[m], NULL, 0, words, 2, b, 8000, &g);
            (void)w3d_citro3d_take_screen();
        }
        w3d_citro3d_cache_by_ref = 0;
        w3d_citro3d_lit_cache_config(0);
        for (int m = 0; m < MESHES; m++) free(vbs[m]);
    }
    printf("by-reference cache mismatches=%d\n", ref_bad);
    return bad != 0 || cache_bad != 0 || skin_bad != 0 || static_bad != 0 || ref_bad != 0;
}
