#ifndef TEXTURE_TEST_CITRO3D_H
#define TEXTURE_TEST_CITRO3D_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { GPU_RGBA8, GPU_RGB8, GPU_RGBA5551, GPU_RGB565, GPU_RGBA4 } GPU_TEXCOLOR;
typedef struct { uint16_t width, height; GPU_TEXCOLOR fmt; void *data; } C3D_Tex;
bool C3D_TexInit(C3D_Tex *, uint16_t, uint16_t, GPU_TEXCOLOR);
void C3D_TexDelete(C3D_Tex *);
void C3D_TexFlush(C3D_Tex *);
void *C3D_Tex2DGetImagePtr(C3D_Tex *, int, void *);
#endif
