/* Minimal live Citro3D frame path for PS2RC's world mesh adapter. */
#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hle.h"
#include "hle_3ds_gfx.h"
#include "hle_3ds_homogeneous.h"
#include "hle_3ds_world_clip.h"
#include "hle_3ds_fog.h"
#include "hle_3ds_texture.h"
#include "hle_render.h"
#include "hle_hud.h"
#include "w3d_citro3d_backend.h"
#include "vshader_shbin.h"

/* These switches are local renderer probes; normal rendering leaves them disabled. */
#ifndef HLE_3DS_TEXTURE_COLOR_DEBUG
#define HLE_3DS_TEXTURE_COLOR_DEBUG 0
#endif
#ifndef HLE_3DS_DISABLE_DEPTH_DEBUG
#define HLE_3DS_DISABLE_DEPTH_DEBUG 0
#endif
#ifndef HLE_3DS_CULL_DIAG
#define HLE_3DS_CULL_DIAG 0
#endif
#ifndef HLE_3DS_DISABLE_CULL_DEBUG
#define HLE_3DS_DISABLE_CULL_DEBUG 0
#endif
#ifndef HLE_3DS_RENDER_DIAG
#define HLE_3DS_RENDER_DIAG 0
#endif
#ifndef HLE_3DS_SKY_PASS
#define HLE_3DS_SKY_PASS 1
#endif
#ifndef HLE_3DS_CAPTURE_START_FRAME
#define HLE_3DS_CAPTURE_START_FRAME 0
#endif
#ifndef HLE_3DS_CAPTURE_MAX
#define HLE_3DS_CAPTURE_MAX 3u
#endif
#ifndef HLE_3DS_CAPTURE_INTERVAL
#define HLE_3DS_CAPTURE_INTERVAL 120u
#endif
/* Prototype: let PICA near-clip raw GE clip coordinates instead of clipping and scaling them on the CPU. */
#ifndef HLE_3DS_GPU_LIGHT
#define HLE_3DS_GPU_LIGHT 0
#endif
#ifndef HLE_3DS_GPU_CLIP
#define HLE_3DS_GPU_CLIP 0
#endif
#ifndef HLE_3DS_CMDBUF_SIZE
#define HLE_3DS_CMDBUF_SIZE (C3D_DEFAULT_CMDBUF_SIZE * 4u)
#endif

/* C3D_RenderTargetClear takes 0xRRGGBBAA for an RGBA8 target (the memory fill writes the word as-is and the PICA
   stores RGBA8 as A, B, G, R bytes). The previous r-in-the-low-byte order cleared black to red. */
#define HLE_3DS_RGBA8(r, g, b, a) ((uint32_t)(r) << 24 | (uint32_t)(g) << 16 | (uint32_t)(b) << 8 | (uint32_t)(a))
#define CLEAR_COLOR HLE_3DS_RGBA8(0x10, 0x18, 0x20, 0xFF)
#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

/* view_z feeds the shader's per-batch fog (fog_uniform); UVs are scaled by uv_scale_uniform. World vertices are
   therefore a plain copy of W3dC3dVertex, whose layout this must keep. */
typedef struct { float position[4]; float color[4]; float uv[2]; float view_z; } Hle3dsVertex;
_Static_assert(sizeof(Hle3dsVertex) == sizeof(W3dC3dVertex), "GPU vertex must match converted vertex");
_Static_assert(offsetof(Hle3dsVertex, color) == offsetof(W3dC3dVertex, color), "colour offset");
_Static_assert(offsetof(Hle3dsVertex, uv) == offsetof(W3dC3dVertex, uv), "UV offset");
_Static_assert(offsetof(Hle3dsVertex, view_z) == offsetof(W3dC3dVertex, view_z), "view z offset");

typedef struct { unsigned first, count; } Hle3dsBatchRange;
/* screen: 0 top, 1 bottom (the dual-screen HUD layout, hle_hud.h) */
typedef struct { unsigned first, count; RndState state; uint8_t lines, screen, radar; } Hle3ds2dBatch;

static C3D_RenderTarget *top, *bottom;
static C3D_Mtx bottom_projection;
static int bottom_active;
static C3D_Tex fog_ramp;
static int fog_ramp_ready;
static DVLB_s *shader_dvlb;
static shaderProgram_s shader_program;
static C3D_Mtx display_projection;
#if HLE_3DS_GPU_CLIP
static C3D_Mtx world_projection;
#endif
static int projection_uniform = -1, fog_uniform = -1, uv_scale_uniform = -1;
/* GPU lighting (HLE_3DS_GPU_LIGHT): the shader's lit-batch uniforms */
static int lit_bool = -1, viewz_uniform = -1, nrow_uniform = -1, ldir_uniform = -1, lcol_uniform = -1;
static int material_uniform = -1, base_uniform = -1, options_uniform = -1, postmul_uniform = -1, postadd_uniform = -1;
static int bones_uniform = -1, skin_bool = -1, obj_bool = -1;
static uint64_t submit_draw_ticks;           /* world draw-call loop, summed for the 120-frame timing line */
static Hle3dsVertex *vertex_buffer;
static Hle3dsVertex *overlay_vertex_buffer;
static Hle3dsVertex *sky_vertex_buffer;
static Hle3dsBatchRange *batch_ranges;
static RndVtx *overlay_source;
static Hle3ds2dBatch *overlay_batches;
static RndVtx sky_source[HLE_3DS_SKY_VERTEX_CAPACITY];
static Hle3ds2dBatch sky_batches[HLE_3DS_SKY_BATCH_CAPACITY];
static unsigned overlay_vertex_count, overlay_batch_count, overlay_dropped_vertices;
static unsigned overlay_texture_omissions, overlay_feedback_skipped;
static unsigned sky_vertex_count, sky_batch_count, sky_dropped_vertices;
static int overlay_screen_width = 640, overlay_screen_height = 448;
static unsigned present_count, input_triangles, drawn_triangles, dropped_w;
static unsigned near_clipped_triangles, near_rejected_triangles, capacity_dropped_triangles;
static unsigned capture_count;
static int overlay_diag_logged;
static u8 *capture_rgb;
static int gfx_ready, c3d_initialized;
static uint32_t current_clear_color = CLEAR_COLOR;

unsigned hle_3ds_gfx_frame_for_draw(void) { return present_count + 1u; }

static GPU_CULLMODE batch_cull_mode(const W3dState *state)
{
#if HLE_3DS_DISABLE_CULL_DEBUG
    (void)state;
    return GPU_CULL_NONE;
#else
    if (!w3d_opt_cull || !state->cull) return GPU_CULL_NONE;
    int cull_front = state->cull == 2;
    if (w3d_opt_front) cull_front = !cull_front;
    return cull_front ? GPU_CULL_FRONT_CCW : GPU_CULL_BACK_CCW;
#endif
}

#if HLE_3DS_TEXTURE_COLOR_DEBUG
static int uv_probe_logged;
#endif

static void save_target(C3D_RenderTarget *target, unsigned height, const char *kind, unsigned frame);
static void save_debug_frame(unsigned frame)
{
    save_target(top, 400u, "frame", frame);
    if (bottom_active) save_target(bottom, 320u, "bottom", frame);
    ++capture_count;
    LOG("Citro3D capture: frame %u (%u of %u)%s\n", frame, capture_count, (unsigned)HLE_3DS_CAPTURE_MAX,
        bottom_active ? " with the bottom screen" : "");
    hle_plat_log_flush();
}

static void save_target(C3D_RenderTarget *target, unsigned height, const char *kind, unsigned frame)
{
    if (!target || !target->frameBuf.colorBuf || !capture_rgb) return;
    C3D_FrameSync();
    const u32 flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                      GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
                      GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    C3D_SyncDisplayTransfer((u32 *)target->frameBuf.colorBuf, GX_BUFFER_DIM(240, height),
                            (u32 *)capture_rgb, GX_BUFFER_DIM(240, height), flags);
    GSPGPU_InvalidateDataCache(capture_rgb, 240u * height * 3u);
    char path[96];
    snprintf(path, sizeof(path), "sdmc:/3ds/ps2rc-sa/gpu-%s-%06u.ppm", kind, frame);
    FILE *file = fopen(path, "wb");
    if (!file) {
        LOG("Citro3D capture: fopen failed for %s\n", path);
        hle_plat_log_flush();
        return;
    }
    fprintf(file, "P6\n240 %u\n255\n", height);
    /* RGB8 transfer memory is BGR; P6 files require RGB byte order. Keep the
       GPU destination read-only so a later transfer cannot race dirty lines. */
    u8 rgb_row[240u * 3u];
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < 240u; ++x) {
            const u8 *bgr = capture_rgb + (y * 240u + x) * 3u;
            rgb_row[x * 3u] = bgr[2];
            rgb_row[x * 3u + 1u] = bgr[1];
            rgb_row[x * 3u + 2u] = bgr[0];
        }
        fwrite(rgb_row, 1, sizeof(rgb_row), file);
    }
    fclose(file);
}

static unsigned fog_morton8(unsigned x, unsigned y)
{
    unsigned r = 0;
    for (unsigned b = 0; b < 3; ++b) { r |= ((x >> b) & 1u) << (2*b); r |= ((y >> b) & 1u) << (2*b+1); }
    return r;
}

static void fog_environment(int enabled, uint32_t color)
{
    C3D_TexEnv *env = C3D_GetTexEnv(1);
    C3D_TexEnvInit(env);
    C3D_TexBind(0, enabled ? &fog_ramp : NULL);
    if (enabled) {
        C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_TEXTURE0);
        C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
        C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
        C3D_TexEnvColor(env, color);
    }
    C3D_DirtyTexEnv(env);
}

int hle_3ds_gfx_init(void)
{
    if (!w3d_citro3d_init() || !hle_3ds_texture_init()) goto fail;
    batch_ranges=calloc(W3D_C3D_BATCH_CAPACITY,sizeof(*batch_ranges));
    overlay_source=calloc(HLE_3DS_2D_VERTEX_CAPACITY,sizeof(*overlay_source));
    overlay_batches=calloc(HLE_3DS_2D_BATCH_CAPACITY,sizeof(*overlay_batches));
    if (!batch_ranges || !overlay_source || !overlay_batches) goto fail;
    if (!C3D_Init(HLE_3DS_CMDBUF_SIZE)) goto fail;
    c3d_initialized=1;
    LOG("Citro3D command buffer: %u bytes\n", (unsigned)HLE_3DS_CMDBUF_SIZE);
    top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!top) goto fail;
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    shader_dvlb = DVLB_ParseFile((u32 *)vshader_shbin, vshader_shbin_size);
    if (!shader_dvlb) goto fail;
    shaderProgramInit(&shader_program);
    shaderProgramSetVsh(&shader_program, &shader_dvlb->DVLE[0]);
    C3D_BindProgram(&shader_program);
    projection_uniform = shaderInstanceGetUniformLocation(shader_program.vertexShader, "projection");
    fog_uniform = shaderInstanceGetUniformLocation(shader_program.vertexShader, "fogparams");
    uv_scale_uniform = shaderInstanceGetUniformLocation(shader_program.vertexShader, "uvscale");
    {
        shaderInstance_s *vs = shader_program.vertexShader;
        lit_bool = shaderInstanceGetUniformLocation(vs, "litmode");
        viewz_uniform = shaderInstanceGetUniformLocation(vs, "viewz");
        nrow_uniform = shaderInstanceGetUniformLocation(vs, "nrow");
        ldir_uniform = shaderInstanceGetUniformLocation(vs, "ldir");
        lcol_uniform = shaderInstanceGetUniformLocation(vs, "lcol");
        material_uniform = shaderInstanceGetUniformLocation(vs, "material");
        base_uniform = shaderInstanceGetUniformLocation(vs, "base");
        options_uniform = shaderInstanceGetUniformLocation(vs, "options");
        postmul_uniform = shaderInstanceGetUniformLocation(vs, "postmul");
        postadd_uniform = shaderInstanceGetUniformLocation(vs, "postadd");
        bones_uniform = shaderInstanceGetUniformLocation(vs, "bones");
        skin_bool = shaderInstanceGetUniformLocation(vs, "skinmode");
        obj_bool = shaderInstanceGetUniformLocation(vs, "objmode");
        if (lit_bool < 0 || viewz_uniform < 0 || nrow_uniform < 0 || ldir_uniform < 0 || lcol_uniform < 0 ||
            material_uniform < 0 || base_uniform < 0 || options_uniform < 0 || postmul_uniform < 0 ||
            postadd_uniform < 0 || bones_uniform < 0 || skin_bool < 0 || obj_bool < 0) goto fail;
        C3D_BoolUnifSet(GPU_VERTEX_SHADER, lit_bool, false);
        C3D_BoolUnifSet(GPU_VERTEX_SHADER, skin_bool, false);
        C3D_BoolUnifSet(GPU_VERTEX_SHADER, obj_bool, false);
    }
    if (projection_uniform < 0 || fog_uniform < 0 || uv_scale_uniform < 0) goto fail;
    C3D_AttrInfo *attributes = C3D_GetAttrInfo();
    AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 4);
    AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 4);
    AttrInfo_AddLoader(attributes, 2, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attributes, 3, GPU_FLOAT, 1);
    if (!C3D_TexInitWithParams(&fog_ramp, NULL, (C3D_TexInitParams){256, 8, 0, GPU_RGBA8, GPU_TEX_PROJECTION, false})) goto fail;
    fog_ramp_ready = 1;
    u8 *ramp = C3D_Tex2DGetImagePtr(&fog_ramp, 0, NULL);
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 256; ++x) {
        unsigned o = 4u * ((x / 8u) * 64u + fog_morton8(x % 8u, y));
        ramp[o] = (u8)x; ramp[o+1] = ramp[o+2] = ramp[o+3] = 255;
    }
    C3D_TexSetFilter(&fog_ramp, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&fog_ramp, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexFlush(&fog_ramp);
    vertex_buffer = linearAlloc(sizeof(Hle3dsVertex) * W3D_C3D_FRAME_VERTEX_CAPACITY);
    overlay_vertex_buffer = linearAlloc(sizeof(Hle3dsVertex) * HLE_3DS_2D_VERTEX_CAPACITY);
    sky_vertex_buffer = linearAlloc(sizeof(Hle3dsVertex) * HLE_3DS_SKY_VERTEX_CAPACITY);
    if (!vertex_buffer || !overlay_vertex_buffer || !sky_vertex_buffer) goto fail;
#if HLE_3DS_CAPTURE_MAX > 0
    capture_rgb = linearAlloc(240u * 400u * 3u);
#endif
    C3D_BufInfo *buffers = C3D_GetBufInfo();
    BufInfo_Init(buffers);
    BufInfo_Add(buffers, vertex_buffer, sizeof(Hle3dsVertex), 4, 0x3210);
    C3D_TexEnv *environment = C3D_GetTexEnv(0);
    C3D_TexEnvInit(environment);
    C3D_TexEnvSrc(environment, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
    C3D_TexEnvFunc(environment, C3D_Both, GPU_REPLACE);
    Mtx_OrthoTilt(&display_projection, 0.0f, 400.0f, 0.0f, 240.0f, 0.0f, 1.0f, true);
#if HLE_3DS_GPU_CLIP
    {
        float rows[4][4];
        C3D_Mtx clip_to_screen;
        hle_3ds_gpu_clip_rows(rows);
        Mtx_Zeros(&clip_to_screen);
        for (int r = 0; r < 4; ++r) {
            clip_to_screen.r[r].x = rows[r][0];
            clip_to_screen.r[r].y = rows[r][1];
            clip_to_screen.r[r].z = rows[r][2];
            clip_to_screen.r[r].w = rows[r][3];
        }
        Mtx_Multiply(&world_projection, &display_projection, &clip_to_screen);
        LOG("Citro3D GPU clip prototype enabled\n");
    }
#endif
    gfx_ready = 1;
    w3d_citro3d_frame_reset();
    return 1;

fail:
    hle_3ds_gfx_shutdown();
    return 0;
}

int hle_3ds_gfx_ready(void) { return gfx_ready; }

/* The dual-screen HUD takes the bottom screen from the text console on its first bottom-screen element (so startup
   messages stay readable until the game draws its HUD); hle_3ds_gfx_release_bottom hands it back for exit text. */
static int enable_bottom(void)
{
    if (bottom_active) return 1;
    if (!gfx_ready) return 0;
    if (!bottom) {
        bottom = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, -1);
        if (!bottom) { LOG("Citro3D bottom HUD: render target allocation failed; HUD stays on the top screen\n"); return 0; }
    }
    gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);
    C3D_RenderTargetSetOutput(bottom, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    Mtx_OrthoTilt(&bottom_projection, 0.0f, 320.0f, 0.0f, 240.0f, 0.0f, 1.0f, true);
    bottom_active = 1;
    LOG("Citro3D bottom HUD: bottom screen taken from the console (frame %u)\n", present_count);
    return 1;
}

int hle_3ds_gfx_bottom_owned(void) { return bottom_active; }

void hle_3ds_gfx_release_bottom(void)
{
    if (!bottom_active) return;
    bottom_active = 0;
    C3D_FrameSync();
    if (bottom) C3D_RenderTargetSetOutput(NULL, GFX_BOTTOM, GFX_LEFT, 0);
    consoleInit(GFX_BOTTOM, NULL);                         /* RGB565, single-buffered text console again */
}

void hle_3ds_gfx_clear(const uint8_t rgba[4], int mode)
{
    if (!rgba || !(mode & 1)) return;
    current_clear_color = HLE_3DS_RGBA8(rgba[0], rgba[1], rgba[2], rgba[3]);
}

void hle_3ds_gfx_2d_set_screen(int width, int height)
{
    if (width > 0 && width <= 4096) overlay_screen_width = width;
    if (height > 0 && height <= 4096) overlay_screen_height = height;
}

static void queue_2d(const RndVtx *vertices, int count, const RndState *state, int lines)
{
    if (!gfx_ready || !vertices || !state || count <= 0) return;
    if (overlay_batch_count >= HLE_3DS_2D_BATCH_CAPACITY) {
        overlay_dropped_vertices += (unsigned)count;
        return;
    }
    const unsigned capacity = HLE_3DS_2D_VERTEX_CAPACITY - overlay_vertex_count;
    Hle3ds2dBatch *batch = &overlay_batches[overlay_batch_count++];
    batch->first = overlay_vertex_count;
    batch->state = *state;
    batch->lines = 0;
    if (hle_hud_screen && !bottom_active) enable_bottom();   /* (normally taken at the game's first frame) */
    batch->screen = (uint8_t)(bottom_active && hle_hud_screen);
    batch->radar = (uint8_t)(batch->screen && hle_hud_group == HLE_HUD_GROUP_RADAR);   /* clipped to its panel */
    if (hle_hud_group >= 0) {                                   /* per-group guest bounds (layout diagnostics) */
        float x0 = vertices[0].x, x1 = x0, y0 = vertices[0].y, y1 = y0;
        for (int i = 1; i < count; ++i) {
            if (vertices[i].x < x0) x0 = vertices[i].x;
            if (vertices[i].x > x1) x1 = vertices[i].x;
            if (vertices[i].y < y0) y0 = vertices[i].y;
            if (vertices[i].y > y1) y1 = vertices[i].y;
        }
        hle_hud_note_bounds(x0, y0, x1, y1);
    }
    if (!lines) {
        unsigned n = (unsigned)count - (unsigned)count % 3u;
        if (n > capacity) n = capacity / 3u * 3u;
        if (!n) {
            --overlay_batch_count;
            overlay_dropped_vertices += (unsigned)count;
            return;
        }
        batch->count = n;
        memcpy(overlay_source + overlay_vertex_count, vertices, (size_t)n * sizeof(*vertices));
        overlay_vertex_count += n;
        overlay_dropped_vertices += (unsigned)count - n;
        return;
    }

    /* PICA200/Citro3D has no line-list primitive. Expand each source segment
       into a one-pixel screen-space quad so all overlay draws use triangles. */
    const unsigned requested_segments = (unsigned)count / 2u;
    unsigned segments = capacity / 6u;
    if (segments > requested_segments) segments = requested_segments;
    if (!segments) {
        --overlay_batch_count;
        overlay_dropped_vertices += (unsigned)count;
        return;
    }
    for (unsigned i = 0; i < segments; ++i) {
        const RndVtx *a = &vertices[i * 2u], *b = &vertices[i * 2u + 1u];
        const float screen_w = batch->screen ? 320.0f : 400.0f;
        const float dx = (b->x - a->x) * screen_w / (float)overlay_screen_width;
        const float dy = (b->y - a->y) * 240.0f / (float)overlay_screen_height;
        const float length = sqrtf(dx * dx + dy * dy);
        const float half_width = 0.5f;
        const float ox = length > 1.0e-5f ? -dy * (half_width / length) * overlay_screen_width / screen_w : 0.0f;
        const float oy = length > 1.0e-5f ?  dx * (half_width / length) * overlay_screen_height / 240.0f : 0.5f;
        RndVtx q[4] = {
            {a->x + ox, a->y + oy, a->u, a->v, a->r, a->g, a->b, a->a},
            {b->x + ox, b->y + oy, b->u, b->v, b->r, b->g, b->b, b->a},
            {b->x - ox, b->y - oy, b->u, b->v, b->r, b->g, b->b, b->a},
            {a->x - ox, a->y - oy, a->u, a->v, a->r, a->g, a->b, a->a}
        };
        static const unsigned tri[6] = {0, 1, 2, 0, 2, 3};
        for (unsigned j = 0; j < 6; ++j)
            overlay_source[overlay_vertex_count++] = q[tri[j]];
    }
    batch->count = segments * 6u;
    overlay_dropped_vertices += (unsigned)count - segments * 2u;
}

void hle_3ds_gfx_2d_tris(const RndVtx *vertices, int count, const RndState *state)
{
    queue_2d(vertices, count, state, 0);
}

void hle_3ds_gfx_2d_lines(const RndVtx *vertices, int count, const RndState *state)
{
    queue_2d(vertices, count, state, 1);
}

void hle_3ds_gfx_sky_tris(const RndVtx *vertices, int count, const RndState *state)
{
    if (!gfx_ready || !vertices || !state || count <= 0) return;
    if (sky_batch_count >= HLE_3DS_SKY_BATCH_CAPACITY) {
        sky_dropped_vertices += (unsigned)count;
        return;
    }
    const unsigned capacity = HLE_3DS_SKY_VERTEX_CAPACITY - sky_vertex_count;
    unsigned n = (unsigned)count - (unsigned)count % 3u;
    if (n > capacity) n = capacity / 3u * 3u;
    if (!n) {
        sky_dropped_vertices += (unsigned)count;
        return;
    }
    Hle3ds2dBatch *batch = &sky_batches[sky_batch_count++];
    batch->first = sky_vertex_count;
    batch->count = n;
    batch->state = *state;
    batch->lines = 0;
    memcpy(sky_source + sky_vertex_count, vertices, (size_t)n * sizeof(*vertices));
    sky_vertex_count += n;
    sky_dropped_vertices += (unsigned)count - n;
}

static float colour_255(float value)
{
    return fminf(1.0f, fmaxf(0.0f, value * (1.0f / 255.0f)));
}

/* Exact equivalent of isfinite() on every attribute and near distance of a triangle: x - x is 0 for finite x
   and NaN for an infinity or NaN, and NaN survives the sum. Needs IEEE semantics (the 3DS build uses
   -fno-fast-math), and is much cheaper than 36 classification calls per triangle. */
static int triangle_finite(const W3dC3dVertex v[3])
{
    float zero = 0.0f;
    for (int j = 0; j < 3; ++j) {
        const float near = near_plane_distance(&v[j]);
        zero += (v[j].clip[0] - v[j].clip[0]) + (v[j].clip[1] - v[j].clip[1]) +
                (v[j].clip[2] - v[j].clip[2]) + (v[j].clip[3] - v[j].clip[3]) +
                (v[j].color[0] - v[j].color[0]) + (v[j].color[1] - v[j].color[1]) +
                (v[j].color[2] - v[j].color[2]) + (v[j].color[3] - v[j].color[3]) +
                (v[j].uv[0] - v[j].uv[0]) + (v[j].uv[1] - v[j].uv[1]) +
                (v[j].view_z - v[j].view_z) + (near - near);
    }
    return zero == 0.0f;
}

/* The shader applies the batch's UV scale and fog, so a world vertex is the converted vertex as is,
   with the position replaced where the CPU projected or clipped it. */
static inline void write_world_vertex(Hle3dsVertex *o, const float position[4], const W3dC3dVertex *v)
{
    memcpy(o, v, sizeof(*o));
    if (position != v->clip) memcpy(o->position, position, sizeof(o->position));
}

#if HLE_3DS_GPU_LIGHT
/* A GPU-lit batch: projection = screen x camera x world, plus the lighting of w3d_citro3d_convert_lit_range_gpu. */
static int lit_uniforms_active;
static void set_lit_uniforms(const W3dGpuLight *g)
{
    const float *w = g->world, *m = g->proj;
    /* world rows (model to view): row j = (w[j], w[3 + j], w[6 + j], w[9 + j]) */
    float wr[3][4];
    for (int j = 0; j < 3; ++j) { wr[j][0] = w[j]; wr[j][1] = w[3 + j]; wr[j][2] = w[6 + j]; wr[j][3] = w[9 + j]; }
    C3D_Mtx camera_world, combined;
    for (int i = 0; i < 4; ++i) {
        float r[4];
        for (int c = 0; c < 4; ++c)
            r[c] = m[i] * wr[0][c] + m[4 + i] * wr[1][c] + m[8 + i] * wr[2][c] + (c == 3 ? m[12 + i] : 0.0f);
        camera_world.r[i].x = r[0]; camera_world.r[i].y = r[1]; camera_world.r[i].z = r[2]; camera_world.r[i].w = r[3];
    }
    Mtx_Multiply(&combined, &world_projection, &camera_world);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_uniform, &combined);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, viewz_uniform, wr[2][0], wr[2][1], wr[2][2], wr[2][3]);
    if (g->mode == 1) {                         /* GPU static: vertex colour + the material step only */
        C3D_FVUnifSet(GPU_VERTEX_SHADER, postmul_uniform, g->post_mul[0], g->post_mul[1], g->post_mul[2], g->post_mul[3]);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, postadd_uniform, g->post_add[0], g->post_add[1], g->post_add[2], 0.0f);
        C3D_BoolUnifSet(GPU_VERTEX_SHADER, lit_bool, false);
        C3D_BoolUnifSet(GPU_VERTEX_SHADER, skin_bool, false);
        C3D_BoolUnifSet(GPU_VERTEX_SHADER, obj_bool, true);
        lit_uniforms_active = 1;
        return;
    }
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, obj_bool, false);
    for (int j = 0; j < 3; ++j)
        C3D_FVUnifSet(GPU_VERTEX_SHADER, nrow_uniform + j, wr[j][0], wr[j][1], wr[j][2], 0.0f);
    for (int l = 0; l < 4; ++l) {
        C3D_FVUnifSet(GPU_VERTEX_SHADER, ldir_uniform + l, g->dir[l][0], g->dir[l][1], g->dir[l][2], 0.0f);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, lcol_uniform + l, g->col[l][0], g->col[l][1], g->col[l][2], 0.0f);
    }
    C3D_FVUnifSet(GPU_VERTEX_SHADER, material_uniform, g->material[0], g->material[1], g->material[2], 0.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, base_uniform, g->base[0], g->base[1], g->base[2], 0.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, options_uniform, g->diffuse, g->alpha, 0.0f, 0.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, postmul_uniform, g->post_mul[0], g->post_mul[1], g->post_mul[2], g->post_mul[3]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, postadd_uniform, g->post_add[0], g->post_add[1], g->post_add[2], 0.0f);
    if (g->skin) {
        /* 8 bones x 3 rows; a whole write is one 24-vector upload */
        C3D_FVec *rows = C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, bones_uniform, 24);
        for (int r = 0; r < 24; ++r) {
            rows[r].x = g->bones[r][0]; rows[r].y = g->bones[r][1];
            rows[r].z = g->bones[r][2]; rows[r].w = g->bones[r][3];
        }
    }
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, skin_bool, g->skin != 0);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, lit_bool, true);
    lit_uniforms_active = 1;
}
static void clear_lit_uniforms(void)
{
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_uniform, &world_projection);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, lit_bool, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, skin_bool, false);
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, obj_bool, false);
    lit_uniforms_active = 0;
}
#endif

static void set_batch_uniforms(float fog_a, float fog_b, float scale_u, float scale_v)
{
    C3D_FVUnifSet(GPU_VERTEX_SHADER, fog_uniform, fog_a, fog_b, 0.0f, 0.0f);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, uv_scale_uniform, scale_u, scale_v, 1.0f, 1.0f);
}

/* 2D state carries blend factors independently of vertex_alpha. */
static void apply_2d_blend(const RndState *state)
{
    static const GPU_BLENDFACTOR factors[12] = {
        GPU_ZERO, GPU_ZERO, GPU_ONE, GPU_SRC_COLOR, GPU_ONE_MINUS_SRC_COLOR, GPU_SRC_ALPHA,
        GPU_ONE_MINUS_SRC_ALPHA, GPU_DST_ALPHA, GPU_ONE_MINUS_DST_ALPHA, GPU_DST_COLOR,
        GPU_ONE_MINUS_DST_COLOR, GPU_SRC_ALPHA_SATURATE
    };
    const int src = state->src_blend >= 1 && state->src_blend <= 11 ? state->src_blend : 5;
    const int dst = state->dst_blend >= 1 && state->dst_blend <= 11 ? state->dst_blend : 6;
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, factors[src], factors[dst], factors[src], factors[dst]);
}

/* One screen's 2D overlay batches (screen 0 top, 1 bottom), in submission order. */
static void draw_overlay_batches(C3D_TexEnv *environment, unsigned screen)
{
    for (unsigned b = 0; b < overlay_batch_count; ++b) {
        const Hle3ds2dBatch *batch = &overlay_batches[b];
        if (batch->screen != screen) continue;
        const Hle3dsTexture *texture = NULL;
        if (batch->state.raster) {
            if (batch->state.raster >= hle_ram_end || hle_ram_end - batch->state.raster < 0x28u) {
                ++overlay_texture_omissions;
                continue;
            }
            /* Framebuffer feedback rasters have no sampleable PS2 pixels here.
               Omit them like the host renderer until feedback is implemented. */
            if (!gw(batch->state.raster + 0x04u)) {
                ++overlay_feedback_skipped;
                continue;
            }
            if (!hle_3ds_texture_prepare_2d(batch->state.raster)) {
                ++overlay_texture_omissions;
                continue;
            }
            texture = hle_3ds_texture_get_2d(batch->state.raster);
            if (!texture) {
                ++overlay_texture_omissions;
                continue;
            }
        }
        if (texture) {
            C3D_TexSetFilter((C3D_Tex *)&texture->tex,
                             (batch->state.filter == 2 || batch->state.filter == 4 || batch->state.filter == 6)
                                 ? GPU_LINEAR : GPU_NEAREST,
                             (batch->state.filter == 2 || batch->state.filter == 4 || batch->state.filter == 6)
                                 ? GPU_LINEAR : GPU_NEAREST);
            const GPU_TEXTURE_WRAP_PARAM wrap_u = batch->state.addr_u == 2 ? GPU_MIRRORED_REPEAT :
                batch->state.addr_u == 3 || batch->state.addr_u == 4 ? GPU_CLAMP_TO_EDGE : GPU_REPEAT;
            const GPU_TEXTURE_WRAP_PARAM wrap_v = batch->state.addr_v == 2 ? GPU_MIRRORED_REPEAT :
                batch->state.addr_v == 3 || batch->state.addr_v == 4 ? GPU_CLAMP_TO_EDGE : GPU_REPEAT;
            C3D_TexSetWrap((C3D_Tex *)&texture->tex, wrap_u, wrap_v);
            C3D_TexBind(1, (C3D_Tex *)&texture->tex);
            C3D_TexEnvSrc(environment, C3D_Both, GPU_TEXTURE1, GPU_PRIMARY_COLOR, 0);
            C3D_TexEnvFunc(environment, C3D_Both, GPU_MODULATE);
            for (unsigned i = batch->first; i < batch->first + batch->count; ++i) {
                Hle3dsVertex *v = &overlay_vertex_buffer[i];
                v->uv[0] *= texture->uv_scale_u;
                v->uv[1] *= texture->uv_scale_v;
            }
        } else {
            C3D_TexBind(1, NULL);
            C3D_TexEnvSrc(environment, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
            C3D_TexEnvFunc(environment, C3D_Both, GPU_REPLACE);
        }
        apply_2d_blend(&batch->state);
        C3D_DirtyTexEnv(environment);
        /* The bottom radar panel is the left 240x240 px; the framebuffer is portrait (240 x 320), so screen x is
           framebuffer y counted from the right edge (fb y = 320 - screen x). */
        if (batch->radar) C3D_SetScissor(GPU_SCISSOR_NORMAL, 0, 80, 240, 320);
        C3D_DrawArrays(GPU_TRIANGLES, (int)batch->first, (int)batch->count);
        if (batch->radar) C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    }
}

void hle_3ds_gfx_present(void)
{
    if (!gfx_ready) return;
    /* Wait for the previous frame's GPU work BEFORE the CPU rewrites the shared vertex buffers below. */
    const uint64_t tick_pre = svcGetSystemTick();
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    const uint64_t tick_begin = svcGetSystemTick();
    hle_3ds_texture_gpu_synced();
    const W3dC3dVertex *source = w3d_citro3d_frame_vertices();
    const int source_count = w3d_citro3d_frame_vertex_count();
    const W3dC3dBatch *batches = w3d_citro3d_frame_batches();
    const int batch_count = w3d_citro3d_frame_batch_count();
#if HLE_3DS_CULL_DIAG
    unsigned cull_none_batches = 0, cull_back_batches = 0, cull_front_batches = 0, cull_other_batches = 0;
    unsigned cull_triangles = 0;
    for (int b = 0; b < batch_count; ++b) {
        const W3dC3dBatch *batch = &batches[b];
        if (!batch->vertex_count) continue;
        if (batch->state.cull == 0) ++cull_none_batches;
        else if (batch->state.cull == 1) ++cull_back_batches;
        else if (batch->state.cull == 2) ++cull_front_batches;
        else ++cull_other_batches;
        if (batch->state.cull) cull_triangles += batch->vertex_count / 3u;
    }
#endif
    input_triangles = source_count > 0 ? (unsigned)source_count / 3u : 0;
    drawn_triangles = dropped_w = 0;
    near_clipped_triangles = near_rejected_triangles = capacity_dropped_triangles = 0;
#if HLE_3DS_TEXTURE_COLOR_DEBUG
    if (input_triangles && !uv_probe_logged) {
        const int probe_batches = batch_count < 8 ? batch_count : 8;
        for (int b = 0; b < probe_batches; ++b) {
            const W3dC3dBatch *batch = &batches[b];
            unsigned end = batch->first_vertex + batch->vertex_count;
            if (end > (unsigned)source_count) end = (unsigned)source_count;
            float umin = 1.0e30f, umax = -1.0e30f, vmin = 1.0e30f, vmax = -1.0e30f;
            unsigned rmin = 255, rmax = 0, gmin = 255, gmax = 0, bmin = 255, bmax = 0;
            for (unsigned i = batch->first_vertex; i < end; ++i) {
                if (source[i].uv[0] < umin) umin = source[i].uv[0];
                if (source[i].uv[0] > umax) umax = source[i].uv[0];
                if (source[i].uv[1] < vmin) vmin = source[i].uv[1];
                if (source[i].uv[1] > vmax) vmax = source[i].uv[1];
                const unsigned r = (unsigned)(fminf(1.0f, fmaxf(0.0f, source[i].color[0])) * 255.0f + 0.5f);
                const unsigned g = (unsigned)(fminf(1.0f, fmaxf(0.0f, source[i].color[1])) * 255.0f + 0.5f);
                const unsigned bl = (unsigned)(fminf(1.0f, fmaxf(0.0f, source[i].color[2])) * 255.0f + 0.5f);
                if (r < rmin) rmin = r;
                if (r > rmax) rmax = r;
                if (g < gmin) gmin = g;
                if (g > gmax) gmax = g;
                if (bl < bmin) bmin = bl;
                if (bl > bmax) bmax = bl;
            }
            const Hle3dsTexture *texture = batch->raster ? hle_3ds_texture_get(batch->raster) : NULL;
            LOG("Citro3D UV probe %d: raster=%08x n=%u uv=[%.5f..%.5f, %.5f..%.5f] "
                "vcol=%u-%u/%u-%u/%u-%u tex=%ux%u uvscale=%.3f/%.3f\n",
                b, batch->raster, end - batch->first_vertex, umin, umax, vmin, vmax,
                rmin, rmax, gmin, gmax, bmin, bmax,
                texture ? texture->image_width : 0u, texture ? texture->image_height : 0u,
                texture ? texture->uv_scale_u : 0.0f, texture ? texture->uv_scale_v : 0.0f);
        }
        uv_probe_logged = 1;
        hle_plat_log_flush();
    }
#endif
    int output_count = 0;
    for (int b = 0; b < batch_count; ++b) {
        const W3dC3dBatch *batch = &batches[b];
        unsigned source_end = batch->first_vertex + batch->vertex_count;
        if (source_end > (unsigned)source_count) source_end = (unsigned)source_count;
        const unsigned output_first = (unsigned)output_count;
#if HLE_3DS_GPU_CLIP
        /* A batch whose conversion saw only finite values, and no vertex behind the near plane when fogged, would
           take the copy-as-is branch below for every triangle: copy it in one block instead. */
        if (gpu_batch_copies_as_is(batch->screen, batch->state.fog) && source_end > batch->first_vertex) {
            const unsigned n = (source_end - batch->first_vertex) / 3u * 3u;
            if (n <= W3D_C3D_FRAME_VERTEX_CAPACITY - (unsigned)output_count) {
                memcpy(&vertex_buffer[output_count], &source[batch->first_vertex], (size_t)n * sizeof(*vertex_buffer));
                output_count += (int)n;
                drawn_triangles += n / 3u;
                batch_ranges[b].first = output_first;
                batch_ranges[b].count = n;
                continue;
            }
        }
        if (batch->gpu_light >= 0) {                     /* no room: GPU-lit vertices are not clip coordinates */
            capacity_dropped_triangles += batch->vertex_count / 3u;
            batch_ranges[b].first = output_first;
            batch_ranges[b].count = 0;
            continue;
        }
#endif
        for (unsigned i = batch->first_vertex; i + 2u < source_end; i += 3u) {
            const W3dC3dVertex *in = &source[i];
            if (!triangle_finite(in)) { ++dropped_w; continue; }
#if HLE_3DS_GPU_CLIP
            /* Fog is a projective texture coordinate built from fog(view_z)*W in the shader.
               GPU-generated near intersections cannot recompute it. Preserve CPU fog there. */
            if (!gpu_fog_needs_cpu_clip(batch->state.fog, near_plane_distance(&in[0]),
                                       near_plane_distance(&in[1]), near_plane_distance(&in[2]))) {
                if (output_count + 3 > W3D_C3D_FRAME_VERTEX_CAPACITY) { ++capacity_dropped_triangles; continue; }
                for (int j = 0; j < 3; ++j)
                    write_world_vertex(&vertex_buffer[output_count++], in[j].clip, &in[j]);
                ++drawn_triangles;
                continue;
            }
#else
            if (near_plane_distance(&in[0]) >= 0.0f && near_plane_distance(&in[1]) >= 0.0f &&
                near_plane_distance(&in[2]) >= 0.0f) {
                /* Common case: wholly in front of the near plane. */
                if (output_count + 3 > W3D_C3D_FRAME_VERTEX_CAPACITY) { ++capacity_dropped_triangles; continue; }
                float positions[3][4];
                if (!hle_3ds_homogeneous_position(in[0].clip, positions[0]) ||
                    !hle_3ds_homogeneous_position(in[1].clip, positions[1]) ||
                    !hle_3ds_homogeneous_position(in[2].clip, positions[2])) { ++dropped_w; continue; }
                for (int j = 0; j < 3; ++j)
                    write_world_vertex(&vertex_buffer[output_count++], positions[j], &in[j]);
                ++drawn_triangles;
                continue;
            }
#endif
            W3dC3dVertex triangle[3] = {in[0], in[1], in[2]};
            W3dC3dVertex clipped[4];
            int crossed = 0;
            const unsigned clipped_count = clip_triangle_near(triangle, clipped, &crossed);
            if (!clipped_count) {
                ++near_rejected_triangles;
                continue;
            }
            if (crossed) ++near_clipped_triangles;
            for (unsigned fan = 1; fan + 1u < clipped_count; ++fan) {
                const W3dC3dVertex *fan_vertices[3] = {&clipped[0], &clipped[fan], &clipped[fan + 1u]};
                if (output_count + 3 > W3D_C3D_FRAME_VERTEX_CAPACITY) {
                    ++capacity_dropped_triangles;
                    continue;
                }
                int valid = 1;
                float positions[3][4];
                for (int j = 0; j < 3; ++j)
                    if (!hle_3ds_homogeneous_position(fan_vertices[j]->clip, positions[j])) valid = 0;
                if (!valid) {
                    ++dropped_w;
                    continue;
                }
                for (int j = 0; j < 3; ++j) {
#if HLE_3DS_GPU_CLIP
                    const float *position = fan_vertices[j]->clip;
#else
                    const float *position = positions[j];
#endif
                    write_world_vertex(&vertex_buffer[output_count++], position, fan_vertices[j]);
                }
                ++drawn_triangles;
            }
        }
        batch_ranges[b].first = output_first;
        batch_ranges[b].count = (unsigned)output_count - output_first;
    }

    for (unsigned i = 0; i < sky_vertex_count; ++i) {
        const RndVtx *source_2d = &sky_source[i];
        Hle3dsVertex *target = &sky_vertex_buffer[i];
        /* OrthoTilt and DepthMap map input one to far window depth zero. */
        hle_3ds_screen_position(source_2d->x, source_2d->y, (float)overlay_screen_width,
                                (float)overlay_screen_height, 1.0f, target->position);
        target->color[0] = colour_255(source_2d->r);
        target->color[1] = colour_255(source_2d->g);
        target->color[2] = colour_255(source_2d->b);
        target->color[3] = colour_255(source_2d->a);
        target->uv[0] = source_2d->u;
        target->uv[1] = source_2d->v;
        target->view_z = 0.0f;
    }
    for (unsigned b = 0; b < overlay_batch_count; ++b)
    for (unsigned i = overlay_batches[b].first; i < overlay_batches[b].first + overlay_batches[b].count; ++i) {
        const RndVtx *source_2d = &overlay_source[i];
        Hle3dsVertex *target = &overlay_vertex_buffer[i];
        hle_3ds_screen_position(source_2d->x, source_2d->y, (float)overlay_screen_width,
                                (float)overlay_screen_height, 0.5f, target->position);
        if (overlay_batches[b].screen) target->position[0] *= 320.0f / 400.0f;   /* bottom screen: 320 px wide */
        target->color[0] = colour_255(source_2d->r);
        target->color[1] = colour_255(source_2d->g);
        target->color[2] = colour_255(source_2d->b);
        target->color[3] = colour_255(source_2d->a);
        target->uv[0] = source_2d->u;
        target->uv[1] = source_2d->v;
        target->view_z = 0.0f;
    }

    C3D_RenderTargetClear(top, C3D_CLEAR_ALL, current_clear_color, 0);
    C3D_FrameDrawOn(top);
    C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
    fog_environment(0, 0);
    C3D_BufInfo *buffers = C3D_GetBufInfo();
    C3D_CullFace(GPU_CULL_NONE);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_uniform, &display_projection);
    set_batch_uniforms(0.0f, 1.0f, 1.0f, 1.0f);              /* 2D passes: no fog, UVs as given */
    C3D_TexEnv *environment = C3D_GetTexEnv(0);
    if (sky_vertex_count && HLE_3DS_SKY_PASS) {
        BufInfo_Init(buffers);
        BufInfo_Add(buffers, sky_vertex_buffer, sizeof(Hle3dsVertex), 4, 0x3210);
        C3D_SetBufInfo(buffers);
        C3D_CullFace(GPU_CULL_NONE);
        /* Let the gradient fill only untouched far-depth pixels; this remains correct if Citro3D
           submits this queued pass after opaque world batches. */
        C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_COLOR);
        C3D_AlphaTest(false, GPU_GREATER, 0);
        C3D_TexBind(1, NULL);
        C3D_TexEnvSrc(environment, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
        C3D_TexEnvFunc(environment, C3D_Both, GPU_REPLACE);
        for (unsigned b = 0; b < sky_batch_count; ++b) {
            const Hle3ds2dBatch *batch = &sky_batches[b];
            apply_2d_blend(&batch->state);
            C3D_DirtyTexEnv(environment);
            C3D_DrawArrays(GPU_TRIANGLES, (int)batch->first, (int)batch->count);
        }
    }
    const uint64_t tick_draw0 = svcGetSystemTick();   /* split of the submit time: world draw calls vs the rest */
    BufInfo_Init(buffers);
    BufInfo_Add(buffers, vertex_buffer, sizeof(Hle3dsVertex), 4, 0x3210);
    /* Init/Add mutate the descriptor; Set marks the new binding dirty after
       the preceding draw consumed GetBufInfo's original dirty flag. */
    C3D_SetBufInfo(buffers);
#if HLE_3DS_GPU_CLIP
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_uniform, &world_projection);
    C3D_DepthMap(true, 1.0f, 1.0f);
#endif
    for (int b = 0; b < batch_count; ++b) {
        const W3dC3dBatch *batch = &batches[b];
        const Hle3dsBatchRange range = batch_ranges[b];
        if (!range.count) continue;

        C3D_CullFace(batch_cull_mode(&batch->state));
        const HleDepthState depth = hle_3ds_depth_state(batch->state.ztest, batch->state.zwrite);
        const GPU_TESTFUNC depth_func = depth.func == HLE_DEPTH_GREATER ? GPU_GREATER :
            depth.func == HLE_DEPTH_GEQUAL ? GPU_GEQUAL : GPU_ALWAYS;
        C3D_DepthTest(depth.enabled, depth_func, depth.write ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
        C3D_AlphaTest(batch->state.atest != 0,
                      batch->state.atest == 2 ? GPU_GEQUAL : batch->state.atest == 3 ? GPU_LESS : GPU_GREATER,
                      batch->state.atest == 2 ? 128 : batch->state.atest == 3 ? 128 : 0);
        if (batch->state.blend) {
            static const GPU_BLENDFACTOR factor_map[12] = {
                GPU_ZERO, GPU_ZERO, GPU_ONE, GPU_SRC_COLOR, GPU_ONE_MINUS_SRC_COLOR, GPU_SRC_ALPHA,
                GPU_ONE_MINUS_SRC_ALPHA, GPU_DST_ALPHA, GPU_ONE_MINUS_DST_ALPHA, GPU_DST_COLOR,
                GPU_ONE_MINUS_DST_COLOR, GPU_SRC_ALPHA_SATURATE
            };
            const int src = batch->state.src_blend >= 1 && batch->state.src_blend <= 11 ? batch->state.src_blend : 5;
            const int dst = batch->state.dst_blend >= 1 && batch->state.dst_blend <= 11 ? batch->state.dst_blend : 6;
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, factor_map[src], factor_map[dst],
                           factor_map[src], factor_map[dst]);
        } else {
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        }
#if HLE_3DS_TEXTURE_COLOR_DEBUG
        /* Diagnostic capture mode: isolate decoded texture sampling from PS2 vertex/material colour. */
        C3D_AlphaTest(false, GPU_GREATER, 0);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
#endif
#if HLE_3DS_DISABLE_DEPTH_DEBUG
        /* Diagnostic capture mode: keep hidden source triangles visible while checking the GPU texture path. */
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
#endif

        const Hle3dsTexture *texture = batch->raster ? hle_3ds_texture_get(batch->raster) : NULL;
        if (texture) {
            C3D_TexSetFilter((C3D_Tex *)&texture->tex,
                             (batch->filter == 2 || batch->filter == 4 || batch->filter == 6) ? GPU_LINEAR : GPU_NEAREST,
                             (batch->filter == 2 || batch->filter == 4 || batch->filter == 6) ? GPU_LINEAR : GPU_NEAREST);
            const GPU_TEXTURE_WRAP_PARAM wrap_u = batch->address_u == 2 ? GPU_MIRRORED_REPEAT :
                batch->address_u == 3 || batch->address_u == 4 ? GPU_CLAMP_TO_EDGE : GPU_REPEAT;
            const GPU_TEXTURE_WRAP_PARAM wrap_v = batch->address_v == 2 ? GPU_MIRRORED_REPEAT :
                batch->address_v == 3 || batch->address_v == 4 ? GPU_CLAMP_TO_EDGE : GPU_REPEAT;
            C3D_TexSetWrap((C3D_Tex *)&texture->tex, wrap_u, wrap_v);
            /* C3D_TexBind takes unit index 0..2; GPU_TEXUNIT0 is a bit mask (1). */
            C3D_TexBind(1, (C3D_Tex *)&texture->tex);
#if HLE_3DS_TEXTURE_COLOR_DEBUG
            C3D_TexEnvSrc(environment, C3D_Both, GPU_TEXTURE1, 0, 0);
            C3D_TexEnvFunc(environment, C3D_Both, GPU_REPLACE);
#else
            C3D_TexEnvSrc(environment, C3D_Both, GPU_TEXTURE1, GPU_PRIMARY_COLOR, 0);
            C3D_TexEnvFunc(environment, C3D_Both, GPU_MODULATE);
#endif
        } else {
            C3D_TexBind(1, NULL);
            C3D_TexEnvSrc(environment, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
            C3D_TexEnvFunc(environment, C3D_Both, GPU_REPLACE);
        }
        /* GetTexEnv marks it dirty once; subsequent direct edits need to invalidate the stage again. */
        C3D_DirtyTexEnv(environment);
#if HLE_3DS_TEXTURE_COLOR_DEBUG
        fog_environment(0, 0);
#else
        fog_environment(batch->state.fog, batch->state.fog_color);
#endif
        float fog_a, fog_b;
        hle_3ds_fog_linear(batch->state.fog, batch->far_z, batch->fog_z, &fog_a, &fog_b);
        set_batch_uniforms(fog_a, fog_b, texture ? texture->uv_scale_u : 1.0f, texture ? texture->uv_scale_v : 1.0f);
#if HLE_3DS_GPU_LIGHT
        const W3dGpuLight *light = w3d_citro3d_frame_gpu_light(batch->gpu_light);
        if (light) set_lit_uniforms(light);
        else if (lit_uniforms_active) clear_lit_uniforms();
#endif
        C3D_DrawArrays(GPU_TRIANGLES, (int)range.first, (int)range.count);
    }
#if HLE_3DS_GPU_LIGHT
    if (lit_uniforms_active) clear_lit_uniforms();
#endif
    submit_draw_ticks += svcGetSystemTick() - tick_draw0;

    fog_environment(0, 0);
    set_batch_uniforms(0.0f, 1.0f, 1.0f, 1.0f);
#if HLE_3DS_GPU_CLIP
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_uniform, &display_projection);
    C3D_DepthMap(true, -1.0f, 0.0f);
#endif
    C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_AlphaTest(false, GPU_GREATER, 0);
    /* Keep 2D array indices small. A busy PS2 frame can already use over 65K
       world vertices, beyond the practical index range used by PICA draws. */
    BufInfo_Init(buffers);
    BufInfo_Add(buffers, overlay_vertex_buffer, sizeof(Hle3dsVertex), 4, 0x3210);
    C3D_SetBufInfo(buffers);
    draw_overlay_batches(environment, 0);
    if (bottom_active) {
        /* The dual-screen HUD: cleared every frame, so screens without HUD (cutscenes, menus) leave it black. */
        C3D_RenderTargetClear(bottom, C3D_CLEAR_ALL, HLE_3DS_RGBA8(0, 0, 0, 0xFF), 0);
        C3D_FrameDrawOn(bottom);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_uniform, &bottom_projection);
        draw_overlay_batches(environment, 1);
    }
    if (!overlay_diag_logged && present_count >= (HLE_3DS_CAPTURE_START_FRAME ? HLE_3DS_CAPTURE_START_FRAME : 1680u)) {
        const unsigned limit = overlay_batch_count < 12u ? overlay_batch_count : 12u;
        for (unsigned b = 0; b < limit; ++b) {
            const Hle3ds2dBatch *batch = &overlay_batches[b];
            if (!batch->count) continue;
            const RndVtx *v = &overlay_source[batch->first];
            LOG("Citro3D 2D probe batch %u/%u: n=%u raster=%08x blend=%u/%u alpha=%u filter=%u "
                "first=(%.2f,%.2f uv %.3f,%.3f rgba %.1f,%.1f,%.1f,%.1f)\n",
                b, overlay_batch_count, batch->count, batch->state.raster, batch->state.src_blend,
                batch->state.dst_blend, batch->state.vertex_alpha, batch->state.filter,
                v->x, v->y, v->u, v->v, v->r, v->g, v->b, v->a);
        }
        overlay_diag_logged = 1;
    }
    if (present_count >= 1678u && present_count <= 1682u) {
        const float cmd_usage = C3D_GetCmdBufUsage();
        const unsigned cmd_usage_milli = cmd_usage > 0.0f ? (unsigned)(cmd_usage * 1000.0f) : 0u;
        LOG("Citro3D boundary frame %u pre-end: cmd=%u/1000 world=%d batches=%d overlay=%u/%u\n",
            present_count, cmd_usage_milli, source_count, batch_count, overlay_vertex_count, overlay_batch_count);
        hle_plat_log_flush();
    }
    const uint64_t tick_submit = svcGetSystemTick();
    C3D_FrameEnd(0);
    {
        static uint64_t acc_wait, acc_submit, acc_end, acc_gap, last_end; static unsigned acc_n;
        const uint64_t tick_end = svcGetSystemTick();
        acc_wait += tick_begin - tick_pre; acc_submit += tick_submit - tick_begin; acc_end += tick_end - tick_submit;
        if (last_end) acc_gap += tick_pre - last_end;
        last_end = tick_end;
        if (++acc_n == 120u) {
            const double ms = 1000.0 / (double)SYSCLOCK_ARM11 / 120.0;
            LOG("Citro3D timing (120 frames, ms/frame): between-presents %.1f FrameBegin-wait %.1f submit %.1f (world draw "
                "calls %.1f) FrameEnd %.1f\n", (double)acc_gap * ms, (double)acc_wait * ms, (double)acc_submit * ms,
                (double)submit_draw_ticks * ms, (double)acc_end * ms);
            submit_draw_ticks = 0;
            uint64_t ct[2], cv[2], cc[2];
            w3d_citro3d_take_conv_stats(ct, cv, cc);
            LOG("Citro3D convert (120 frames, per frame): static %.1f ms %llu verts %llu calls | lit %.1f ms %llu verts %llu calls\n",
                (double)ct[0] * ms, (unsigned long long)(cv[0] / 120u), (unsigned long long)(cc[0] / 120u),
                (double)ct[1] * ms, (unsigned long long)(cv[1] / 120u), (unsigned long long)(cc[1] / 120u));
            LOG("Citro3D budget skips (120 frames, per frame): %u static meshes\n", w3d_citro3d_budget_skipped() / 120u);
            w3d_citro3d_log_static_cut(120u);
#if HLE_3DS_GPU_LIGHT
            {
                uint64_t lc[7];
                size_t lc_bytes = 0;
                w3d_citro3d_lit_cache_stats(lc, &lc_bytes);
                LOG("Citro3D lit cache (120 frames, per frame): %llu hits %llu verts | %llu misses %llu verts | skinned %llu verts | "
                    "evicted %llu, no memory %llu, %u KiB held\n", (unsigned long long)(lc[0] / 120u),
                    (unsigned long long)(lc[2] / 120u), (unsigned long long)(lc[1] / 120u), (unsigned long long)(lc[3] / 120u),
                    (unsigned long long)(lc[4] / 120u), (unsigned long long)lc[5], (unsigned long long)lc[6],
                    (unsigned)(lc_bytes / 1024u));
            }
#endif
            hle_plat_log_flush();
            acc_wait = acc_submit = acc_end = acc_gap = 0; acc_n = 0;
        }
    }
    if (present_count >= 1678u && present_count <= 1682u) {
        const float cmd_usage = C3D_GetCmdBufUsage();
        const unsigned cmd_usage_milli = cmd_usage > 0.0f ? (unsigned)(cmd_usage * 1000.0f) : 0u;
        LOG("Citro3D boundary frame %u post-end: cmd=%u/1000\n", present_count, cmd_usage_milli);
        hle_plat_log_flush();
    }

    ++present_count;
    const int error = w3d_citro3d_frame_error();
    if (present_count <= 12 || present_count % 120 == 0 || present_count == HLE_3DS_CAPTURE_START_FRAME) {
        LOG("Citro3D sky frame %u: %u batches, %u vertices, dropped=%u clear=%08x\n",
            present_count, sky_batch_count, sky_vertex_count, sky_dropped_vertices, current_clear_color);
        hle_plat_log_flush();
    }
    if (present_count <= 12 || present_count % 120 == 0 || error) {
        LOG("Citro3D frame %u: %u/%u triangles, batches=%d, near-clipped=%u, near-rejected=%u, W-dropped=%u, "
            "capacity-dropped=%u, texture omissions=%u, state simplifications=%u, "
            "PSP segments=%u, meshes skipped=%u, adapter error=%d\n",
            present_count, drawn_triangles, input_triangles, batch_count, near_clipped_triangles,
            near_rejected_triangles, dropped_w, capacity_dropped_triangles,
            w3d_citro3d_frame_textures_skipped(), w3d_citro3d_frame_states_simplified(),
            w3d_citro3d_frame_segments_skipped(), w3d_citro3d_frame_meshes_skipped(), error);
        hle_plat_log_flush();
    }
#if HLE_3DS_CULL_DIAG
    if (present_count <= 12 || present_count % 120 == 0) {
        LOG("Citro3D cull frame %u: forced-off=%d options=%d/%d batches none/back/front/other=%u/%u/%u/%u culled-state triangles=%u\n",
            present_count, HLE_3DS_DISABLE_CULL_DEBUG, w3d_opt_cull, w3d_opt_front, cull_none_batches, cull_back_batches,
            cull_front_batches, cull_other_batches, cull_triangles);
        hle_plat_log_flush();
    }
#endif
    if (present_count % 120 == 0) hle_3ds_texture_report(present_count);
    if (present_count % 120 == 0) hle_hud_log_bounds(present_count);
    if (present_count <= 12 || present_count % 120 == 0 || overlay_dropped_vertices) {
        LOG("Citro3D 2D overlay frame %u: %u batches, %u vertices, dropped=%u, texture omissions=%u, feedback skipped=%u (%dx%d source)\n",
            present_count, overlay_batch_count, overlay_vertex_count, overlay_dropped_vertices,
            overlay_texture_omissions, overlay_feedback_skipped, overlay_screen_width, overlay_screen_height);
    }
    /* The dual-screen layout takes the bottom screen once the game draws (boot messages stay readable until then),
       so intro movies and cutscenes leave it black like the showcase rather than showing the console. */
    if (!bottom_active && (input_triangles || overlay_batch_count) && hle_hud_bottom_layout()) enable_bottom();
    const int capture_due = HLE_3DS_CAPTURE_START_FRAME
        ? (present_count >= HLE_3DS_CAPTURE_START_FRAME &&
           (present_count - HLE_3DS_CAPTURE_START_FRAME) % HLE_3DS_CAPTURE_INTERVAL == 0)
        : (capture_count == 0 || present_count % HLE_3DS_CAPTURE_INTERVAL == 0);
    if ((input_triangles || overlay_batch_count) && capture_count < HLE_3DS_CAPTURE_MAX && capture_due) {
        save_debug_frame(present_count);
    }
    overlay_vertex_count = overlay_batch_count = overlay_dropped_vertices = overlay_texture_omissions = overlay_feedback_skipped = 0;
    sky_vertex_count = sky_batch_count = sky_dropped_vertices = 0;
    w3d_citro3d_frame_reset();
}

void hle_3ds_gfx_shutdown(void)
{
    gfx_ready = 0;
    /* Fini waits for queued work and destroys render targets before GPU-backed images/buffers are freed. */
    if (c3d_initialized) { C3D_Fini(); c3d_initialized=0; top=NULL; bottom=NULL; bottom_active=0; }
    hle_3ds_texture_shutdown();
    w3d_citro3d_shutdown();
    free(batch_ranges); batch_ranges=NULL;
    free(overlay_source); overlay_source=NULL;
    free(overlay_batches); overlay_batches=NULL;
    if (fog_ramp_ready) { C3D_TexDelete(&fog_ramp); fog_ramp_ready = 0; }
    if (capture_rgb) { linearFree(capture_rgb); capture_rgb = NULL; }
    if (vertex_buffer) { linearFree(vertex_buffer); vertex_buffer = NULL; }
    if (overlay_vertex_buffer) { linearFree(overlay_vertex_buffer); overlay_vertex_buffer = NULL; }
    if (sky_vertex_buffer) { linearFree(sky_vertex_buffer); sky_vertex_buffer = NULL; }
    if (shader_dvlb) { shaderProgramFree(&shader_program); DVLB_Free(shader_dvlb); shader_dvlb = NULL; }
    top=NULL;
}

/* The host-only CPU 2D raster is intentionally not built here. Keep its hooks inert while the GPU 3D path is brought
   up; PS2RC's translated driver functions still execute so diagnostics can identify the rest of the frame path. */
int hle_raster_open(const char *dir, int every, int max_frames) { (void)dir; (void)every; (void)max_frames; return 0; }
void hle_raster_set_screen(int w, int h) { (void)w; (void)h; }
void hle_raster_clear(const uint8_t rgba[4], int mode) { (void)rgba; (void)mode; }
void hle_raster_tris(const RndVtx *v, int nverts, const RndState *st) { (void)v; (void)nverts; (void)st; }
void hle_raster_lines(const RndVtx *v, int nverts, const RndState *st) { (void)v; (void)nverts; (void)st; }
void hle_raster_tris3d(const Rnd3Vtx *v, int nverts, const Rnd3State *st) { (void)v; (void)nverts; (void)st; }
int hle_raster_frame_end(uint64_t flip, const char *tag) { (void)flip; (void)tag; return 0; }
int hle_raster_is_open(void) { return 0; }
int hle_raster_alpha_class(uint32_t raster) { (void)raster; return -1; }
void hle_raster_forget(uint32_t raster) { hle_3ds_texture_forget(raster); }
void hle_raster_texture_name(uint32_t raster, const char *name) { (void)raster; (void)name; }
void hle_raster_close(void) {}
int hle_raster_postfx_mul(const float k[3]) { (void)k; return 0; }
int hle_raster_postfx_radiosity(int limit, int filter_passes, int render_passes, int intensity)
{ (void)limit; (void)filter_passes; (void)render_passes; (void)intensity; return 0; }
