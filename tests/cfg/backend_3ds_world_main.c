#include "w3d_citro3d_backend.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int w3d_opt_cull=1;
void hle_3ds_texture_frame_reset(void) {}
static int nearf(float a,float b) { return fabsf(a-b)<0.0001f; }
int main(void) {
    W3dCam c={0}; W3dState s={0}; s.uscale=1;
    c.proj[0]=2; c.proj[5]=3; c.proj[10]=4; c.proj[11]=1; c.proj[12]=5; c.proj[13]=6; c.proj[14]=7;
    const float w[12]={2,0,0,0,3,0,0,0,4,10,20,30};
    const W3dVC tris[3]={{0,0,0xffffffff,1,2,3},{1,0,0xffffffff,-1,0,1},{0,1,0xffffffff,0,1,-1}};
    const float expected[3][4]={{29,84,175,42},{21,66,143,34},{25,75,111,26}};
    w3d_citro3d_frame_reset(); assert(w3d_be_begin(&c)); w3d_be_state(&s);
    w3d_be_world(w); const W3dC3dVertex *copy=w3d_be_tris(tris,3); assert(copy);
    assert(w3d_citro3d_frame_vertex_count()==3 && w3d_citro3d_frame_batch_count()==1);
    for(int i=0;i<3;i++) for(int j=0;j<4;j++) assert(nearf(copy[i].clip[j],expected[i][j]));
    w3d_be_tris_again(copy,3);
    assert(w3d_citro3d_frame_vertex_count()==6 && w3d_citro3d_frame_batch_count()==2);
    const W3dC3dVertex *all=w3d_citro3d_frame_vertices();
    for(int i=0;i<3;i++) for(int j=0;j<4;j++) assert(nearf(all[i+3].clip[j],expected[i][j]));
    w3d_citro3d_frame_reset(); assert(w3d_be_begin(&c)); w3d_be_state(&s);
    assert(w3d_be_tris(tris,3)==NULL); assert(w3d_citro3d_frame_vertex_count()==0);
    /* Fill the selected model's bounded buffer, then ensure exhaustion never advances past its end. */
    w3d_citro3d_frame_reset(); assert(w3d_be_begin(&c)); w3d_be_state(&s); w3d_be_world(w);
    for (unsigned i=0;i<W3D_C3D_FRAME_VERTEX_CAPACITY/3u+2u;i++) w3d_be_tris(tris,3);
    assert((unsigned)w3d_citro3d_frame_vertex_count()<=W3D_C3D_FRAME_VERTEX_CAPACITY);
    assert((unsigned)w3d_citro3d_frame_batch_count()<=W3D_C3D_BATCH_CAPACITY);
    puts("actual 3DS backend model-space transform, world guard and replay passed");
}
