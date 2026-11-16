/* PS2 GIF-packet raster conversion to a bounded Citro3D texture cache.
   texconv.c is the pack's validated PS2 raster decoder; this target asks it for a linear PSP-format intermediate,
   expands that to RGBA, then tiles it for the PICA texture unit. */
#include "hle_3ds_texture.h"
#include "hle_3ds_texture_layout.h"
#include "hle_3ds_texture_pack.h"

#include "hle.h"
#include "hle_plat.h"
#include "hle_3ds_profile.h"
#include "texconv.h"

#include <3ds/allocator/linear.h>
#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#ifndef HLE_3DS_TEXTURE_WHITE_DEBUG
#define HLE_3DS_TEXTURE_WHITE_DEBUG 0
#endif

#ifndef HLE_3DS_TEX_SLOTS
#define HLE_3DS_TEX_SLOTS HLE_3DS_DEFAULT_TEX_SLOTS
#endif
#ifndef HLE_3DS_TEX_MAX_BYTES
#define HLE_3DS_TEX_MAX_BYTES HLE_3DS_DEFAULT_TEX_BYTES
#endif
/* One converted base level plus palette/padding; Old policy never produces a level above 128x128. */
#define HLE_3DS_TEX_DECODE_BYTES (hle_3ds_active_profile->decode_bytes)

typedef struct {
    Hle3dsTexture texture;
    uint32_t pix, pal, opix, last_use, pinned_frame;
    uint32_t width, height, depth, format;
    size_t bytes;
    uint8_t valid, dirty, overlay;
} TextureSlot;

typedef struct {
    uint32_t raster, pix, pal, opix;
    uint32_t width, height, depth, format;
    TcPs2Raster source;
    TcPs2Info decoded;
    TcPolicy policy;
    TcPspTex plan;
} RasterInfo;

static TextureSlot *slots;
static uint8_t *decode_buffer;
static uint32_t decode_buffer_bytes;
static size_t cache_bytes, cache_peak_bytes;
static uint32_t use_clock, frame_serial = 1, gpu_done_serial; /* frames up to gpu_done_serial have finished on the GPU */
static unsigned decoded_count, decode_failures;
static unsigned fail_bad_pointer, fail_bad_packet, fail_oversize;
static unsigned fail_pinned_cache, fail_no_slot, fail_tex_init, fail_convert;
static unsigned scaled_count, invalidated_count;

int hle_3ds_texture_init(void)
{
    if (slots) return 1;
    slots=calloc(HLE_3DS_TEX_SLOTS,sizeof(*slots));
    decode_buffer_bytes=HLE_3DS_TEX_DECODE_BYTES;
    decode_buffer=memalign(16,decode_buffer_bytes);
    if (slots && decode_buffer) return 1;
    free(slots); slots=NULL; free(decode_buffer); decode_buffer=NULL; return 0;
}
static uint32_t next_power_of_two(uint32_t value)
{
    uint32_t result = 8u;
    while (result < value && result < 1024u) result <<= 1;
    return result;
}

static int raster_info(uint32_t raster, RasterInfo *info, int *error, int overlay)
{
    if (error) *error = TC_ERR_ARG;
    if (!raster || raster >= hle_ram_end || hle_ram_end - raster < 0x28u) return 0;
    memset(info, 0, sizeof(*info));
    info->raster = raster;
    info->pix = gw(raster + 0x04u);
    info->pal = gw(raster + 0x08u);
    info->width = gw(raster + 0x0Cu);
    info->height = gw(raster + 0x10u);
    info->depth = gw(raster + 0x14u);
    info->opix = gw(raster + 0x24u);
    /* The guest keeps the high byte of RenderWare's 16-bit format at +0x23. */
    info->format = (uint32_t)gb(raster + 0x23u) << 8;
    if (!info->pix || info->pix < 0x50u || info->pix >= hle_ram_end ||
        (info->pal && (info->pal < 0x50u || info->pal >= hle_ram_end || info->pal - 0x50u > hle_ram_end - 0x450u)) ||
        info->width == 0 || info->height == 0 || info->width > 1024u || info->height > 1024u)
        return 0;

    const uint32_t upload_address = info->pix - 0x50u;
    const uint8_t *pixel_upload = hle_ram + upload_address;
    const uint8_t *palette_upload = info->pal ? hle_ram + info->pal - 0x50u : NULL;
    int result = tc_ps2_from_packets(pixel_upload, hle_ram_end - upload_address, palette_upload,
                                     (int32_t)info->width, (int32_t)info->height, (int32_t)info->depth,
                                     info->format, &info->source);
    if (!result) result = tc_ps2_inspect(&info->source, &info->decoded);
    if (!result) {
        tc_policy_default(&info->policy);
        info->policy.max_dim = HLE_3DS_COMPACT ? (overlay ? 128 : 64) : 512;
        info->policy.max_levels = 1;
        info->policy.cache_bytes = 0;
        info->policy.swizzle = 0;       /* expand a simple linear intermediate to RGBA8 below */
        info->policy.decimate = HLE_3DS_COMPACT;
        info->policy.alpha = TC_AC_CLUT;
        info->policy.direct32 = TC_D32_8888;
        result = tc_plan(&info->decoded, &info->policy, &info->plan);
        /* Admit smaller complete images rather than spending the entire cache on a few large rasters.
           Keep 2D atlases separately so the world policy cannot force HUD text to world resolution. */
        if (HLE_3DS_COMPACT) {
            const size_t limit = overlay ? 32768u : 8192u;
            while (!result && (size_t)next_power_of_two(info->plan.level[0].width) *
                     next_power_of_two(info->plan.level[0].height) * 2u > limit && info->policy.max_dim > 8) {
                info->policy.max_dim /= 2;
                result = tc_plan(&info->decoded, &info->policy, &info->plan);
            }
        }
    }
    if (result) {
        if (error) *error = result;
        return 0;
    }
    if (error) *error = TC_OK;
    return 1;
}

static void psp_5551(uint16_t value, uint8_t out[4])
{
    const uint32_t r = value & 31u;
    const uint32_t g = (value >> 5) & 31u;
    const uint32_t b = (value >> 10) & 31u;
    out[0] = (uint8_t)((r << 3) | (r >> 2));
    out[1] = (uint8_t)((g << 3) | (g >> 2));
    out[2] = (uint8_t)((b << 3) | (b >> 2));
    out[3] = (value & 0x8000u) ? 255u : 0u;
}

static int psp_texel_rgba(const RasterInfo *raster, const uint8_t *buffer, uint32_t x, uint32_t y, uint8_t out[4])
{
    const TcPspLevel *level = &raster->plan.level[0];
    if (x >= level->width || y >= level->height || !level->rows || level->bytes % level->rows) return 0;
    const uint32_t row_bytes = level->bytes / level->rows;
    const uint8_t *row = buffer + level->offset + (size_t)y * row_bytes;
    switch (raster->plan.psm) {
    case TC_GU_T8: {
        if (x >= level->bufw || raster->plan.clut_psm == TC_GU_NONE) return 0;
        const uint32_t index = row[x];
        const uint8_t *clut = buffer + raster->plan.clut_offset;
        if (raster->plan.clut_psm == TC_GU_8888) {
            memcpy(out, clut + index * 4u, 4u);
            return 1;
        }
        uint16_t value;
        memcpy(&value, clut + index * 2u, sizeof(value));
        psp_5551(value, out);
        return 1;
    }
    case TC_GU_T4: {
        if (x >= level->bufw || raster->plan.clut_psm == TC_GU_NONE) return 0;
        const uint8_t packed = row[x >> 1];
        const uint32_t index = (x & 1u) ? packed >> 4 : packed & 15u;
        const uint8_t *clut = buffer + raster->plan.clut_offset;
        if (raster->plan.clut_psm == TC_GU_8888) {
            memcpy(out, clut + index * 4u, 4u);
            return 1;
        }
        uint16_t value;
        memcpy(&value, clut + index * 2u, sizeof(value));
        psp_5551(value, out);
        return 1;
    }
    case TC_GU_8888:
        if ((size_t)x * 4u + 4u > row_bytes) return 0;
        memcpy(out, row + x * 4u, 4u);
        return 1;
    case TC_GU_5650: {
        if ((size_t)x * 2u + 2u > row_bytes) return 0;
        uint16_t value;
        memcpy(&value, row + x * 2u, sizeof(value));
        const uint32_t r = value & 31u, g = (value >> 5) & 63u, b = (value >> 11) & 31u;
        out[0] = (uint8_t)((r << 3) | (r >> 2));
        out[1] = (uint8_t)((g << 2) | (g >> 4));
        out[2] = (uint8_t)((b << 3) | (b >> 2));
        out[3] = 255u;
        return 1;
    }
    case TC_GU_5551: {
        if ((size_t)x * 2u + 2u > row_bytes) return 0;
        uint16_t value;
        memcpy(&value, row + x * 2u, sizeof(value));
        psp_5551(value, out);
        return 1;
    }
    case TC_GU_4444: {
        if ((size_t)x * 2u + 2u > row_bytes) return 0;
        uint16_t value;
        memcpy(&value, row + x * 2u, sizeof(value));
        out[0] = (uint8_t)((value & 15u) * 17u);
        out[1] = (uint8_t)(((value >> 4) & 15u) * 17u);
        out[2] = (uint8_t)(((value >> 8) & 15u) * 17u);
        out[3] = (uint8_t)(((value >> 12) & 15u) * 17u);
        return 1;
    }
    default:
        return 0;
    }
}

static int decode_to_texture(RasterInfo *raster, C3D_Tex *texture, int *alpha_class)
{
    uint8_t *destination = (uint8_t *)C3D_Tex2DGetImagePtr(texture, 0, NULL);
    if (!destination) return TC_ERR_ARG;
    const TcPspLevel *level = &raster->plan.level[0];
    for (uint32_t y = 0; y < texture->height; ++y) {
        const uint32_t source_y = y < level->height ? y : level->height - 1u;
        for (uint32_t x = 0; x < texture->width; ++x) {
            const uint32_t source_x = x < level->width ? x : level->width - 1u;
            uint8_t color[4];
            if (!psp_texel_rgba(raster, decode_buffer, source_x, source_y, color)) return TC_ERR_FORMAT;
#if HLE_3DS_TEXTURE_WHITE_DEBUG
            /* Diagnostic-only: replace converted texels to prove texture fetch/binding. */
            color[0] = 255u;
            color[1] = 255u;
            color[2] = 255u;
            color[3] = 255u;
#endif
            const uint32_t index = hle_3ds_texture_texel_index(x, y, texture->width, texture->height);
            if (HLE_3DS_COMPACT) {
                uint16_t packed = texture->fmt == GPU_RGB565 ? hle_3ds_pack_rgb565(color) :
                    texture->fmt == GPU_RGBA5551 ? hle_3ds_pack_rgba5551(color) : hle_3ds_pack_rgba4(color);
                memcpy(destination + index * 2u, &packed, sizeof(packed));
                continue;
            }
            const uint32_t offset = index * 4u;
            /* PICA GPU_RGBA8 image memory uses ABGR byte order (Epic REGTA's upload convention). */
            destination[offset + 0u] = color[3];
            destination[offset + 1u] = color[2];
            destination[offset + 2u] = color[1];
            destination[offset + 3u] = color[0];
        }
    }
    *alpha_class = raster->plan.alpha_class;
    ++decoded_count;
    if (level->width < raster->width || level->height < raster->height) ++scaled_count;
    if (decoded_count <= 8u) {
        LOG("Citro3D texture decoded %u: raster=%08x %ux%u fmt=%04x kind=%u psm=%u swz=%u -> %ux%u "
            "alpha=%u gpu_format=%u\n",
            decoded_count, raster->raster, raster->width, raster->height, raster->format, raster->decoded.kind,
            raster->plan.psm, raster->plan.swizzled, level->width, level->height, raster->plan.alpha_class,
            (unsigned)texture->fmt);
        hle_plat_log_flush();
    }
    return TC_OK;
}

static void slot_drop(TextureSlot *slot)
{
    if (!slot->valid) return;
    C3D_TexDelete(&slot->texture.tex);
    if (cache_bytes >= slot->bytes) cache_bytes -= slot->bytes;
    memset(slot, 0, sizeof(*slot));
}

/* A slot is busy while this frame uses it or while a submitted frame that used it may still be on the GPU. */
static int slot_busy(const TextureSlot *slot)
{
    return slot->pinned_frame == frame_serial || (int32_t)(slot->pinned_frame - gpu_done_serial) > 0;
}

static TextureSlot *find_slot(uint32_t raster, int overlay)
{
    for (unsigned i = 0; slots && i < HLE_3DS_TEX_SLOTS; ++i)
        if (slots[i].valid && slots[i].texture.raster == raster && slots[i].overlay == overlay) return &slots[i];
    return NULL;
}

static TextureSlot *choose_slot(void)
{
    TextureSlot *oldest = NULL;
    for (unsigned i = 0; slots && i < HLE_3DS_TEX_SLOTS; ++i) {
        if (!slots[i].valid) return &slots[i];
        if (slot_busy(&slots[i])) continue;
        if (!oldest || slots[i].last_use < oldest->last_use) oldest = &slots[i];
    }
    return oldest;
}

/* Byte-budget eviction must ignore empty slots; deleting an empty slot makes no progress. */
static TextureSlot *choose_victim(void)
{
    TextureSlot *oldest = NULL;
    for (unsigned i = 0; slots && i < HLE_3DS_TEX_SLOTS; ++i) {
        if (!slots[i].valid || slot_busy(&slots[i])) continue;
        if (!oldest || slots[i].last_use < oldest->last_use) oldest = &slots[i];
    }
    return oldest;
}

static int texture_prepare(uint32_t raster, int *alpha_class, int overlay)
{
    if (!hle_3ds_texture_init()) return 0;
    if (!HLE_3DS_COMPACT) overlay = 0;
    if (alpha_class) *alpha_class = 0;
    if (!raster) return 1;
    if (raster >= hle_ram_end || hle_ram_end - raster < 0x28u) {
        ++decode_failures; ++fail_bad_pointer; return 0;
    }

    const uint32_t pix = gw(raster + 0x04u), pal = gw(raster + 0x08u), opix = gw(raster + 0x24u);
    TextureSlot *slot = find_slot(raster, overlay);
    if (slot && !slot->dirty && slot->pix == pix && slot->pal == pal && slot->opix == opix &&
        slot->width == gw(raster + 0x0Cu) && slot->height == gw(raster + 0x10u) &&
        slot->depth == gw(raster + 0x14u) && slot->format == ((uint32_t)gb(raster + 0x23u) << 8)) {
        slot->last_use = ++use_clock;
        slot->pinned_frame = frame_serial;
        if (alpha_class) *alpha_class = slot->texture.alpha_class;
        return 1;
    }
    if (slot && slot_busy(slot)) { ++decode_failures; ++fail_pinned_cache; return 0; }
    if (slot) slot_drop(slot);

    RasterInfo info;
    int error = 0;
    if (!raster_info(raster, &info, &error, overlay)) {
        ++decode_failures; ++fail_bad_packet;
        if (decode_failures <= 8u) {
            const uint32_t raster_pix = gw(raster + 0x04u);
            const uint32_t raster_pal = gw(raster + 0x08u);
            const uint32_t raster_opix = gw(raster + 0x24u);
            LOG("Citro3D texture packet rejected: raster=%08x %ux%u depth=%u fmt=%04x rc=%d\n",
                raster, gw(raster + 0x0Cu), gw(raster + 0x10u), gw(raster + 0x14u),
                (uint32_t)gb(raster + 0x23u) << 8, error);
            LOG("Citro3D texture reject detail: pix=%08x pal=%08x opix=%08x ram_end=%08x\n",
                raster_pix, raster_pal, raster_opix, hle_ram_end);
            hle_plat_log_flush();
        }
        return 0;
    }

    const uint32_t texture_width = next_power_of_two(info.plan.level[0].width);
    const uint32_t texture_height = next_power_of_two(info.plan.level[0].height);
    const size_t bytes = (size_t)texture_width * texture_height * (HLE_3DS_COMPACT ? 2u : 4u);
    if (!texture_width || !texture_height || bytes > HLE_3DS_TEX_MAX_BYTES) {
        ++decode_failures; ++fail_oversize; return 0;
    }

    while (cache_bytes + bytes > HLE_3DS_TEX_MAX_BYTES) {
        const size_t bytes_before = cache_bytes;
        TextureSlot *victim = choose_victim();
        if (!victim) { ++decode_failures; ++fail_pinned_cache; return 0; }
        slot_drop(victim);
        if (cache_bytes >= bytes_before) { ++decode_failures; ++fail_pinned_cache; return 0; }
    }
    slot = choose_slot();
    if (!slot) { ++decode_failures; ++fail_no_slot; return 0; }
    if (slot->valid) slot_drop(slot);
    error = info.plan.total_bytes > decode_buffer_bytes ? TC_ERR_SIZE :
        tc_convert(&info.decoded, &info.policy, &info.plan, decode_buffer, decode_buffer_bytes);
    if (error) { ++decode_failures; ++fail_convert; return 0; }
    const GPU_TEXCOLOR gpu_format = !HLE_3DS_COMPACT ? GPU_RGBA8 :
        info.plan.alpha_class == TC_ALPHA_OPAQUE ? GPU_RGB565 :
        info.plan.alpha_class == TC_ALPHA_CUTOUT ? GPU_RGBA5551 : GPU_RGBA4;
    if (!C3D_TexInit(&slot->texture.tex, (u16)texture_width, (u16)texture_height, gpu_format)) {
        ++decode_failures; ++fail_tex_init;
        return 0;
    }
    int alpha = 0;
    error = decode_to_texture(&info, &slot->texture.tex, &alpha);
    if (error) {
        C3D_TexDelete(&slot->texture.tex);
        memset(slot, 0, sizeof(*slot));
        ++decode_failures; ++fail_convert;
        if (decode_failures <= 8u) {
            LOG("Citro3D texture conversion failed: raster=%08x rc=%d\n", raster, error);
            hle_plat_log_flush();
        }
        return 0;
    }
    C3D_TexFlush(&slot->texture.tex);
    slot->texture.raster = raster;
    slot->texture.image_width = info.plan.level[0].width;
    slot->texture.image_height = info.plan.level[0].height;
    slot->texture.uv_scale_u = (float)info.plan.level[0].width / (float)texture_width;
    slot->texture.uv_scale_v = (float)info.plan.level[0].height / (float)texture_height;
    slot->texture.alpha_class = (uint8_t)alpha;
    slot->pix = info.pix;
    slot->pal = info.pal;
    slot->opix = info.opix;
    slot->width = info.width; slot->height = info.height; slot->depth = info.depth; slot->format = info.format;
    slot->overlay = (uint8_t)overlay;
    slot->bytes = bytes;
    slot->last_use = ++use_clock;
    slot->pinned_frame = frame_serial;
    slot->valid = 1;
    cache_bytes += bytes;
    if (cache_bytes > cache_peak_bytes) cache_peak_bytes = cache_bytes;
    if (alpha_class) *alpha_class = alpha;
    return 1;
}

const Hle3dsTexture *hle_3ds_texture_get(uint32_t raster)
{
    TextureSlot *slot = find_slot(raster, 0);
    if (!slot) return NULL;
    slot->last_use = ++use_clock;
    return &slot->texture;
}

int hle_3ds_texture_prepare(uint32_t raster, int *alpha_class) { return texture_prepare(raster, alpha_class, 0); }
int hle_3ds_texture_prepare_2d(uint32_t raster) { return texture_prepare(raster, NULL, 1); }
const Hle3dsTexture *hle_3ds_texture_get_2d(uint32_t raster)
{
    TextureSlot *slot = find_slot(raster, HLE_3DS_COMPACT ? 1 : 0);
    if (!slot) return NULL;
    slot->last_use = ++use_clock;
    return &slot->texture;
}

void hle_3ds_texture_frame_reset(void)
{
    ++frame_serial;
}

/* Called once the previous frame has finished on the GPU (right after C3D_FrameBegin returns). */
void hle_3ds_texture_gpu_synced(void)
{
    gpu_done_serial = frame_serial - 1u;
    for (unsigned i = 0; slots && i < HLE_3DS_TEX_SLOTS; ++i)
        if (slots[i].valid && slots[i].dirty && !slot_busy(&slots[i])) slot_drop(&slots[i]);
}

void hle_3ds_texture_forget(uint32_t raster)
{
    for (unsigned i = 0; slots && i < HLE_3DS_TEX_SLOTS; ++i) {
        TextureSlot *slot = &slots[i];
        if (!slot->valid || slot->texture.raster != raster) continue;
        slot->dirty = 1;
        ++invalidated_count;
        /* Keep the old snapshot for batches that already reference it. New prepare calls must not reuse it. */
        if (!slot_busy(slot)) slot_drop(slot);
    }
}

void hle_3ds_texture_report(unsigned frame)
{
    unsigned valid = 0, pinned = 0;
    for (unsigned i = 0; slots && i < HLE_3DS_TEX_SLOTS; ++i) {
        if (!slots[i].valid) continue;
        ++valid;
        if (slots[i].pinned_frame == frame_serial) ++pinned;
    }
    LOG("Citro3D texture pressure frame %u: slots=%u/%u pinned=%u cache=%u/%u KiB peak=%u KiB linear_free=%u KiB ",
        frame, valid, HLE_3DS_TEX_SLOTS, pinned,
        (unsigned)(cache_bytes / 1024u), (unsigned)(HLE_3DS_TEX_MAX_BYTES / 1024u),
        (unsigned)(cache_peak_bytes / 1024u), (unsigned)(linearSpaceFree() / 1024u));
    LOG("decoded=%u failures=%u badptr=%u badpacket=%u oversize=%u pinned_fail=%u noslot=%u texinit=%u convert=%u scaled=%u invalidated=%u\n",
        decoded_count, decode_failures, fail_bad_pointer, fail_bad_packet, fail_oversize,
        fail_pinned_cache, fail_no_slot, fail_tex_init, fail_convert, scaled_count, invalidated_count);
}

void hle_3ds_texture_shutdown(void)
{
    for (unsigned i = 0; slots && i < HLE_3DS_TEX_SLOTS; ++i) slot_drop(&slots[i]);
    free(slots); slots=NULL; free(decode_buffer); decode_buffer=NULL;
    cache_bytes = 0;
    LOG("Citro3D texture cache: decoded=%u failures=%u\n", decoded_count, decode_failures);
}
