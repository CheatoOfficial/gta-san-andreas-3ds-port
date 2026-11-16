#ifndef HLE_3DS_GFX_H
#define HLE_3DS_GFX_H

#include "hle_render.h"

int hle_3ds_gfx_init(void);
int hle_3ds_gfx_ready(void);
int hle_3ds_gfx_bottom_owned(void);      /* the dual-screen HUD owns the bottom screen (no console text) */
void hle_3ds_gfx_release_bottom(void);   /* give the bottom screen back to the text console (exit messages) */
void hle_3ds_gfx_clear(const uint8_t rgba[4], int mode);
void hle_3ds_gfx_2d_set_screen(int width, int height);
void hle_3ds_gfx_2d_tris(const RndVtx *vertices, int count, const RndState *state);
void hle_3ds_gfx_2d_lines(const RndVtx *vertices, int count, const RndState *state);
void hle_3ds_gfx_sky_tris(const RndVtx *vertices, int count, const RndState *state);
void hle_3ds_gfx_present(void);
unsigned hle_3ds_gfx_frame_for_draw(void);
void hle_3ds_gfx_shutdown(void);

#endif
