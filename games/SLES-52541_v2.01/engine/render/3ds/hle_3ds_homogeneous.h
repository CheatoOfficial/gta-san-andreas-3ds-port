#ifndef HLE_3DS_HOMOGENEOUS_H
#define HLE_3DS_HOMOGENEOUS_H
#include <math.h>
/* Screen coordinates scaled by W for the existing orthographic projection. */
static inline int hle_3ds_homogeneous_position(const float clip[4], float out[4])
{
    for (int i = 0; i < 4; ++i) if (!isfinite(clip[i])) return 0;
    const float w = clip[3];
    if (w <= 0.00001f) return 0;
    out[0] = (clip[0] + w) * 200.0f;
    out[1] = (clip[1] + w) * 120.0f;
    /* OrthoTilt and DepthMap invert this input to yield GE reversed depth. */
    out[2] = fminf(w, fmaxf(0.0f, (w + clip[2]) * 0.5f));
    out[3] = w;
    for (int i = 0; i < 4; ++i) if (!isfinite(out[i])) return 0;
    return 1;
}
/* GPU-clip prototype (HLE_3DS_GPU_CLIP): row-major matrix that the vertex shader applies to raw GE clip
   coordinates before the OrthoTilt projection. X, Y and W match hle_3ds_homogeneous_position(). Depth is
   flipped to z' = (w - z) / 2 so the GE near plane (z = -w) lands on OrthoTilt clip z = 0, the plane
   PICA clips in hardware; DepthMap(1, 1) then restores GE reversed window depth (near 1, far 0).
   Unlike the CPU path, geometry beyond the GE far plane is clipped by the GPU instead of clamped. */
static inline void hle_3ds_gpu_clip_rows(float m[4][4])
{
    static const float rows[4][4] = {
        {200.0f, 0.0f, 0.0f, 200.0f},
        {0.0f, 120.0f, 0.0f, 120.0f},
        {0.0f, 0.0f, -0.5f, 0.5f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) m[r][c] = rows[r][c];
}
/* RndVtx coordinates start at the top left; OrthoTilt's screen Y points up. */
static inline void hle_3ds_screen_position(float x, float y, float width, float height,
                                           float depth, float out[4])
{
    out[0] = x * (400.0f / width);
    out[1] = (height - y) * (240.0f / height);
    out[2] = depth;
    out[3] = 1.0f;
}
typedef enum { HLE_DEPTH_ALWAYS, HLE_DEPTH_GEQUAL, HLE_DEPTH_GREATER } HleDepthFunc;
typedef struct { int enabled, write; HleDepthFunc func; } HleDepthState;
static inline HleDepthState hle_3ds_depth_state(int ztest, int zwrite)
{
    HleDepthState state = { ztest != 0 || zwrite != 0, zwrite != 0,
        ztest == 2 ? HLE_DEPTH_GREATER : ztest ? HLE_DEPTH_GEQUAL : HLE_DEPTH_ALWAYS };
    return state;
}
#endif
