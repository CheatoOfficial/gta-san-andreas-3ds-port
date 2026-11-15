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
#endif
