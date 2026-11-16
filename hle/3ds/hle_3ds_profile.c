/* Memory allowance chooses the heap split; hardware plus both heaps choose render quality. */
#include "hle_3ds_profile.h"
#ifdef HLE_3DS_PROF
#define NEW_STATIC_BUDGET 20000u
#else
#define NEW_STATIC_BUDGET 0u
#endif
static const Hle3dsProfile compact_profile = {
    .name = "compact", .compact = 1,
    .frame_vertices = 65536, .temp_vertices = 8192, .batches = 2048,
    .overlay_vertices = 4096, .overlay_batches = 512,
    .texture_slots = 512, .texture_bytes = 2u * HLE_3DS_MIB,
    .decode_bytes = 256u * 256u * 4u + 4096u,     /* 256 px 2D textures (fonts); was 128 px */
    .static_budget = 10000, .min_heap = 40u * HLE_3DS_MIB
};
static const Hle3dsProfile full_profile = {
    .name = "full", .compact = 0,
    .frame_vertices = 196608, .temp_vertices = 32768, .batches = 4096,
    .overlay_vertices = 16384, .overlay_batches = 2048,
    .texture_slots = 384, .texture_bytes = 20u * HLE_3DS_MIB,
    .decode_bytes = 1024u * 1024u + 4096u,
    .static_budget = NEW_STATIC_BUDGET, .min_heap = 56u * HLE_3DS_MIB
};
#if HLE_3DS_OLD_MODEL || HLE_3DS_AUTO_MODEL
const Hle3dsProfile *hle_3ds_active_profile = &compact_profile;
#else
const Hle3dsProfile *hle_3ds_active_profile = &full_profile;
#endif
int hle_3ds_extra_controls;
void hle_3ds_profile_select(int new_hardware, size_t normal_heap, size_t linear_heap)
{
#if HLE_3DS_AUTO_MODEL
    hle_3ds_active_profile = new_hardware && normal_heap >= full_profile.min_heap && linear_heap >= 32u*HLE_3DS_MIB
        ? &full_profile : &compact_profile;
    hle_3ds_extra_controls = !!new_hardware;
#else
    (void)normal_heap; (void)linear_heap;
    hle_3ds_active_profile = HLE_3DS_OLD_MODEL ? &compact_profile : &full_profile;
    hle_3ds_extra_controls = new_hardware && !HLE_3DS_OLD_MODEL;
#endif
}
void hle_3ds_heap_plan(uint32_t available, uint32_t *normal, uint32_t *linear)
{
    available &= ~4095u;
    uint32_t gpu = available >= 88u*HLE_3DS_MIB ? 32u*HLE_3DS_MIB : 8u*HLE_3DS_MIB;
    if (gpu >= available) gpu = (available/4u) & ~4095u;
    uint32_t app = available-gpu;
    if (app > 96u*HLE_3DS_MIB) app = 96u*HLE_3DS_MIB;
    *normal=app; *linear=gpu;
}
