#include "w3d_citro3d_backend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int w3d_opt_cull=1;
void hle_3ds_texture_frame_reset(void) {}
int main(void)
{
    W3dCam camera={0}; W3dState state={0}; state.uscale=1;
    camera.proj[0]=camera.proj[5]=camera.proj[10]=camera.proj[15]=1;
    const float world[12]={1,0,0,0,1,0,0,0,1,0,0,0};
    W3dVF verts[3]; memset(verts,0,sizeof(verts));
    for(int i=0;i<3;i++) verts[i].c=0xFFFFFFFFu;
    const uint32_t prims[2]={0x04030003u,0x0B000000u};
    W3dMeshView mesh={0}; mesh.verts=(const uint8_t *)verts; mesh.nverts=3; mesh.nprims=2;
    mesh.stride=sizeof(verts[0]); mesh.fmt=W3D_FMT_UVF;
    w3d_citro3d_frame_reset(); assert(w3d_be_begin(&camera)); w3d_be_state(&state); w3d_be_world(world);
    w3d_be_range(&mesh,NULL,0,prims,2);
    assert(w3d_citro3d_frame_vertex_count()==3);
    w3d_be_range(&mesh,NULL,0,prims,2); /* compile this test with a static budget of three vertices */
    assert(w3d_citro3d_frame_vertex_count()==3 && w3d_citro3d_frame_batch_count()==1);
    assert(w3d_citro3d_budget_skipped()==1);
    const W3dVC direct[3]={{0}};
    assert(w3d_be_tris(direct,3)!=NULL);
    assert(w3d_citro3d_frame_vertex_count()==6 && w3d_citro3d_frame_batch_count()==2);
    puts("3DS static-budget rejection preserves room for later immediate/lit draws");
    return 0;
}
