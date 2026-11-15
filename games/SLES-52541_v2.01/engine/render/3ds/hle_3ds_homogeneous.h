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
