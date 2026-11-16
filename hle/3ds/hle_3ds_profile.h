/* Startup-selected, immutable renderer resource profile. */
#ifndef HLE_3DS_PROFILE_H
#define HLE_3DS_PROFILE_H
#include <stddef.h>
#include <stdint.h>
#ifndef HLE_3DS_OLD_MODEL
#define HLE_3DS_OLD_MODEL 0
#endif
#ifndef HLE_3DS_AUTO_MODEL
#define HLE_3DS_AUTO_MODEL 0
#endif
#define HLE_3DS_MIB (1024u * 1024u)
#define HLE_3DS_LINEAR_HEAP_BYTES (8u * HLE_3DS_MIB)
typedef struct {
    const char *name;
    unsigned compact, frame_vertices, temp_vertices, batches, overlay_vertices, overlay_batches;
    unsigned texture_slots, texture_bytes, decode_bytes, static_budget, min_heap;
} Hle3dsProfile;
extern const Hle3dsProfile *hle_3ds_active_profile;
extern int hle_3ds_extra_controls;
/* Select only before any renderer allocation. Never change the profile during a live session. */
void hle_3ds_profile_select(int new_hardware, size_t normal_heap, size_t linear_heap);
void hle_3ds_heap_plan(uint32_t available, uint32_t *normal, uint32_t *linear);
#define HLE_3DS_COMPACT (hle_3ds_active_profile->compact)
#define HLE_3DS_PROFILE_NAME (hle_3ds_active_profile->name)
#define W3D_C3D_FRAME_VERTEX_CAPACITY (hle_3ds_active_profile->frame_vertices)
#define W3D_C3D_TEMP_VERTEX_CAPACITY (hle_3ds_active_profile->temp_vertices)
#define W3D_C3D_BATCH_CAPACITY (hle_3ds_active_profile->batches)
#define HLE_3DS_2D_VERTEX_CAPACITY (hle_3ds_active_profile->overlay_vertices)
#define HLE_3DS_2D_BATCH_CAPACITY (hle_3ds_active_profile->overlay_batches)
#define HLE_3DS_DEFAULT_TEX_SLOTS (hle_3ds_active_profile->texture_slots)
#define HLE_3DS_DEFAULT_TEX_BYTES (hle_3ds_active_profile->texture_bytes)
#define HLE_3DS_SKY_VERTEX_CAPACITY 1024u
#define HLE_3DS_SKY_BATCH_CAPACITY 32u
#endif
