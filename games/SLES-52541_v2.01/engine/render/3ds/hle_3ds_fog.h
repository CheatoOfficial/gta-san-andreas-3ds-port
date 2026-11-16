#ifndef HLE_3DS_FOG_H
#define HLE_3DS_FOG_H
/* Host renderer evaluates fog after near clipping, in view space. */
static inline float hle_3ds_fog_factor(int enabled, float z, float far_z, float fog_z)
{
    if (!enabled) return 1.0f;
    if (!(far_z > fog_z)) return 0.0f;
    float f = (far_z - z) / (far_z - fog_z);
    return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

/* The vertex shader evaluates the same factor per batch as clamp(a * z + b, 0, 1). Disabled fog is a = 0, b = 1;
   an empty fog range is a = 0, b = 0. Equal to hle_3ds_fog_factor up to float rounding (the PICA uses float24). */
static inline void hle_3ds_fog_linear(int enabled, float far_z, float fog_z, float *a, float *b)
{
    if (!enabled) { *a = 0.0f; *b = 1.0f; return; }
    if (!(far_z > fog_z)) { *a = 0.0f; *b = 0.0f; return; }
    const float inverse_range = 1.0f / (far_z - fog_z);
    *a = -inverse_range;
    *b = far_z * inverse_range;
}
#endif
