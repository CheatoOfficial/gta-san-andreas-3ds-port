/* The shader's per-batch fog clamp(a*z + b, 0, 1) must reproduce hle_3ds_fog_factor. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "hle_3ds_fog.h"

static float shader_fog(int enabled, float z, float far_z, float fog_z)
{
    float a, b;
    hle_3ds_fog_linear(enabled, far_z, fog_z, &a, &b);
    const float f = a * z + b;
    return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

int main(void)
{
    /* Disabled fog and an empty fog range are exact. */
    assert(shader_fog(0, 123.0f, 450.0f, 36.3f) == 1.0f);
    assert(shader_fog(1, 123.0f, 50.0f, 50.0f) == 0.0f);
    assert(shader_fog(1, 123.0f, 40.0f, 50.0f) == 0.0f);
    assert(shader_fog(1, 123.0f, NAN, 50.0f) == 0.0f);

    /* Camera ranges from the game, plus a sweep through and beyond the fog band. */
    static const float ranges[][2] = {{450.0f, 36.297218f}, {100.0f, 50.0f}, {2000.0f, 0.3f}, {1.0f, 0.5f}};
    float worst = 0.0f;
    for (unsigned r = 0; r < sizeof ranges / sizeof ranges[0]; ++r) {
        const float far_z = ranges[r][0], fog_z = ranges[r][1];
        for (int i = -200; i <= 1200; ++i) {
            const float z = far_z * (float)i / 1000.0f;
            const float expected = hle_3ds_fog_factor(1, z, far_z, fog_z);
            const float got = shader_fog(1, z, far_z, fog_z);
            const float delta = fabsf(expected - got);
            if (delta > worst) worst = delta;
            /* Clamped regions are exact; inside the band the factor differs by float rounding only. */
            if (expected == 0.0f || expected == 1.0f) assert(fabsf(got - expected) <= 1.0e-5f);
            assert(delta <= 1.0e-5f);
        }
    }
    printf("fog linear form: worst difference %.3g\n", worst);
    return 0;
}
