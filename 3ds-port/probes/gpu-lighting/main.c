/* Game-free numeric probe. Compare GPU directional lighting with production-order CPU math. */
#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vshader_shbin.h"
#define FLAGS (GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_RAW_COPY(0)|GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8)|GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
typedef struct {float p[4], n[4];} Vertex;
static unsigned seed=19;
static float random_f(void){seed=seed*1664525u+1013904223u;return ((seed>>8)&65535)/16384.f-2.f;}
int main(void){
 gfxInitDefault(); FILE *log=fopen("sdmc:/gpu-lighting-probe.log","w");if(!log)return 1;
 fprintf(log,"status=started\n");fflush(log);
 if(!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))return 2;
 C3D_RenderTarget *target=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,GPU_RB_DEPTH24_STENCIL8);if(!target)return 3;
 C3D_RenderTargetSetOutput(target,GFX_TOP,GFX_LEFT,FLAGS);
 DVLB_s *d=DVLB_ParseFile((u32*)vshader_shbin,vshader_shbin_size);shaderProgram_s program;
 shaderProgramInit(&program);shaderProgramSetVsh(&program,&d->DVLE[0]);C3D_BindProgram(&program);
 int proj=shaderInstanceGetUniformLocation(program.vertexShader,"projection");
 int ud=shaderInstanceGetUniformLocation(program.vertexShader,"direction"),uc=shaderInstanceGetUniformLocation(program.vertexShader,"lightcolour");
 int um=shaderInstanceGetUniformLocation(program.vertexShader,"material"),ub=shaderInstanceGetUniformLocation(program.vertexShader,"base"),uo=shaderInstanceGetUniformLocation(program.vertexShader,"options");
 C3D_Mtx projection;Mtx_OrthoTilt(&projection,0,400,0,240,0,1,true);
 C3D_AttrInfo *a=C3D_GetAttrInfo();AttrInfo_Init(a);AttrInfo_AddLoader(a,0,GPU_FLOAT,4);AttrInfo_AddLoader(a,1,GPU_FLOAT,4);
 Vertex *v=linearAlloc(6*sizeof(*v));u8 *pixels=linearAlloc(240*400*3);if(!v||!pixels)return 4;
 const float xy[6][2]={{60,40},{340,40},{60,200},{340,40},{340,200},{60,200}};
 int failures=0,maxdelta=0;
 for(int test=0;test<64;++test){
  float n[3],dir[4][3],col[4][3],mat[3],base[3],factors[4],diffuse=.7f;
  for(int c=0;c<3;c++){n[c]=test==0?0:test==1?(c==2?1:0):random_f();mat[c]=(random_f()+2)*.25f;base[c]=(random_f()+2)*.1f;}
  for(int l=0;l<4;l++)for(int c=0;c<3;c++){dir[l][c]=random_f();col[l][c]=l<test%5?(random_f()+2)*.25f:0;}
  float length=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);float inv=length>0?1.f/length:0;
  for(int l=0;l<4;l++){float dot=(n[0]*dir[l][0]+n[1]*dir[l][1]+n[2]*dir[l][2])*inv;factors[l]=dot>0?diffuse*dot:0;}
  int expected[3];for(int c=0;c<3;c++){float value=base[c];for(int l=0;l<4;l++)if(factors[l]>0)value+=mat[c]*factors[l]*col[l][c];int x=(int)(value*255.f+.5f);expected[c]=x<0?0:x>255?255:x;}
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);C3D_RenderTargetClear(target,C3D_CLEAR_ALL,0,0);C3D_FrameDrawOn(target);
  for(int j=0;j<6;j++){v[j]=(Vertex){{xy[j][0],xy[j][1],.5f,1},{n[0],n[1],n[2],0}};}
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,proj,&projection);
  for(int l=0;l<4;l++){C3D_FVUnifSet(GPU_VERTEX_SHADER,ud+l,dir[l][0],dir[l][1],dir[l][2],0);C3D_FVUnifSet(GPU_VERTEX_SHADER,uc+l,col[l][0],col[l][1],col[l][2],0);}
  C3D_FVUnifSet(GPU_VERTEX_SHADER,um,mat[0],mat[1],mat[2],0);C3D_FVUnifSet(GPU_VERTEX_SHADER,ub,base[0],base[1],base[2],0);C3D_FVUnifSet(GPU_VERTEX_SHADER,uo,diffuse,1,0,0);
  C3D_BufInfo *b=C3D_GetBufInfo();BufInfo_Init(b);BufInfo_Add(b,v,sizeof(*v),2,0x10);
  C3D_CullFace(GPU_CULL_NONE);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
  C3D_TexEnv *e=C3D_GetTexEnv(0);C3D_TexEnvInit(e);C3D_TexEnvSrc(e,C3D_Both,GPU_PRIMARY_COLOR,0,0);C3D_TexEnvFunc(e,C3D_Both,GPU_REPLACE);
  C3D_DrawArrays(GPU_TRIANGLES,0,6);C3D_FrameEnd(0);C3D_FrameSync();
  C3D_SyncDisplayTransfer((u32*)target->frameBuf.colorBuf,GX_BUFFER_DIM(240,400),(u32*)pixels,GX_BUFFER_DIM(240,400),FLAGS);GSPGPU_InvalidateDataCache(pixels,240*400*3);
  const u8 *pixel=pixels+3*(200*240+119);int delta=0;for(int c=0;c<3;c++){int x=abs((int)pixel[2-c]-expected[c]);if(x>delta)delta=x;}if(delta>maxdelta)maxdelta=delta;if(delta>1)failures++;
  fprintf(log,"case=%d expected=%d,%d,%d observed=%u,%u,%u max_delta=%d result=%s\n",test,expected[0],expected[1],expected[2],pixel[2],pixel[1],pixel[0],delta,delta<=1?"PASS":"FAIL");fflush(log);
 }
 fprintf(log,"cases=64 failures=%d max_channel_delta=%d\nstatus=complete\n",failures,maxdelta);fclose(log);
 C3D_FrameSync();linearFree(v);linearFree(pixels);C3D_RenderTargetDelete(target);shaderProgramFree(&program);DVLB_Free(d);C3D_Fini();gfxExit();return failures?1:0;
}
