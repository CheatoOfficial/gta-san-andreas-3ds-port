/* Distance priority for the static vertex budget (compact profile): when a frame's static draws exceed the
   budget, the next frame skips the farthest ones instead of whichever the game happens to submit last. */
#ifndef W3D_CITRO3D_DEPTH_H
#define W3D_CITRO3D_DEPTH_H
#include <stdint.h>

#define W3D_C3D_DEPTH_BAND_M 10.0f
#define W3D_C3D_DEPTH_BANDS 64u          /* 0..640 m in 10 m bands; the last band also holds everything farther */
#define W3D_C3D_FOG_STEP_M 8.0f          /* fog distance follows the cut at most this far per frame */
#define W3D_C3D_FOG_UNLIMITED 1.0e6f
#ifndef W3D_C3D_FOG_FLOOR_M
#define W3D_C3D_FOG_FLOOR_M 100.0f       /* nearest full-fog distance the cut may impose (judged on Old captures) */
#endif

/* Band of a draw's origin depth (the model-to-view z translation). Behind-camera origins are band 0, so large
   meshes around the camera are never cut. */
static inline unsigned w3d_citro3d_depth_band(float depth)
{
    if (!(depth > 0.0f)) return 0u;
    const float band = depth / W3D_C3D_DEPTH_BAND_M;
    return band >= (float)(W3D_C3D_DEPTH_BANDS - 1u) ? W3D_C3D_DEPTH_BANDS - 1u : (unsigned)band;
}

/* First band at which the cumulative estimated vertices of a frame's static draw attempts exceed budget. Next
   frame, bands beyond it are skipped and the cut band itself may use only what the nearer bands leave
   (*allowance, in estimate units), so it is filled in draw order without displacing nearer meshes.
   W3D_C3D_DEPTH_BANDS means no cut. Band 0 is never cut, and the attempts include the skipped draws, so a still
   camera gives the same cut every frame. */
static inline unsigned w3d_citro3d_static_cut_band(const uint32_t estimate[W3D_C3D_DEPTH_BANDS], unsigned budget,
                                                   uint64_t *allowance)
{
    *allowance = 0;
    if (!budget) return W3D_C3D_DEPTH_BANDS;
    uint64_t total = 0;
    for (unsigned band = 0; band < W3D_C3D_DEPTH_BANDS; ++band) {
        if (total + estimate[band] > budget) {
            if (!band) return 1u;                          /* band 0 alone overflows: keep it, cut the rest */
            *allowance = budget - total;
            return band;
        }
        total += estimate[band];
    }
    return W3D_C3D_DEPTH_BANDS;
}

/* Whether a static draw passes the cut: nearer bands always, the cut band while its allowance lasts. */
static inline int w3d_citro3d_static_admit(unsigned band, unsigned cut_band, uint64_t estimate,
                                           uint64_t allowance, uint64_t *cut_band_used)
{
    if (band < cut_band) return 1;
    if (band > cut_band || *cut_band_used + estimate > allowance) return 0;
    *cut_band_used += estimate;
    return 1;
}

/* The per-draw estimate (triangles x 3, scaled for partial ranges) is not the converted count. Express the
   budget in estimate units using last frame's drawn meshes: estimated/written, clamped to 0.5..4. */
static inline unsigned w3d_citro3d_estimate_budget(unsigned budget, uint64_t estimated, uint64_t written)
{
    if (!budget || !estimated || !written) return budget;
    uint64_t scaled = (uint64_t)budget * estimated / written;
    if (scaled > (uint64_t)budget * 4u) scaled = (uint64_t)budget * 4u;
    if (scaled < budget / 2u) scaled = budget / 2u;
    return scaled > 0xFFFFFFFFu ? 0xFFFFFFFFu : (unsigned)scaled;
}

/* Fog far distance that hides the cut: moves toward the cut's start by at most W3D_C3D_FOG_STEP_M per frame,
   starting from the camera's far plane, and returns to unlimited (by the same steps) when there is no cut. */
static inline float w3d_citro3d_fog_limit_step(float current, unsigned cut_band, float camera_far)
{
    const int cut = cut_band < W3D_C3D_DEPTH_BANDS;
    if (!(camera_far > 0.0f)) camera_far = 1000.0f;
    if (current >= camera_far) {
        if (!cut) return W3D_C3D_FOG_UNLIMITED;
        current = camera_far;
    }
    const float target = cut ? (float)cut_band * W3D_C3D_DEPTH_BAND_M : camera_far;
    float next = target > current + W3D_C3D_FOG_STEP_M ? current + W3D_C3D_FOG_STEP_M
               : target < current - W3D_C3D_FOG_STEP_M ? current - W3D_C3D_FOG_STEP_M : target;
    return !cut && next >= camera_far ? W3D_C3D_FOG_UNLIMITED : next;
}
#endif
