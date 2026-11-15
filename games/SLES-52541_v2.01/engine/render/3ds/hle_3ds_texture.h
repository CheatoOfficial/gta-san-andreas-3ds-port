#ifndef HLE_3DS_TEXTURE_H
#define HLE_3DS_TEXTURE_H

#include <3ds.h>
#include <citro3d.h>

typedef struct {
    C3D_Tex tex;
    uint32_t raster;
    uint16_t image_width, image_height;
    float uv_scale_u, uv_scale_v;
    uint8_t alpha_class;
} Hle3dsTexture;

int hle_3ds_texture_prepare(uint32_t raster, int *alpha_class);
const Hle3dsTexture *hle_3ds_texture_get(uint32_t raster);
void hle_3ds_texture_frame_reset(void);
void hle_3ds_texture_forget(uint32_t raster);
void hle_3ds_texture_report(unsigned frame);
void hle_3ds_texture_shutdown(void);

#endif
