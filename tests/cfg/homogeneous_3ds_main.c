#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "hle_3ds_homogeneous.h"
static int closef(float a, float b) { return fabsf(a-b) < 0.0001f; }
int main(void) {
    const float inputs[][4] = {{-0.5f,0.2f,-0.4f,1}, {2,-1,3,4}, {0,0,5,2}, {0,0,-5,2}};
    for (unsigned i=0; i<sizeof(inputs)/sizeof(inputs[0]); ++i) {
        const float *c=inputs[i]; float o[4]; assert(hle_3ds_homogeneous_position(c,o)); assert(o[3] == c[3]);
        assert(closef(o[0]/o[3], (c[0]/c[3]+1)*200));
        assert(closef(o[1]/o[3], (c[1]/c[3]+1)*120));
        /* OrthoTilt produces z' = z - w; DepthMap(-1,0) negates z'/w. */
        const float window_depth = -(o[2] - o[3]) / o[3];
        assert(closef(window_depth, fminf(1,fmaxf(0,(1-c[2]/c[3])*0.5f))));
    }
    float depth[3];
    for (int i=0; i<3; ++i) {
        const float clip[]={0,0,(float)(i-1)*2,2}; float o[4];
        assert(hle_3ds_homogeneous_position(clip,o));
        depth[i] = -(o[2]-o[3])/o[3];
    }
    assert(closef(depth[0],1) && closef(depth[1],0.5f) && closef(depth[2],0));
    assert(depth[0]>depth[1] && depth[1]>depth[2]);
    const float sky_depth = -(1.0f-1.0f)/1.0f;
    assert(sky_depth >= 0 && !(sky_depth >= depth[1])); /* GEQUAL: clear passes, geometry fails. */
    HleDepthState d = hle_3ds_depth_state(0,1);
    assert(d.enabled && d.write && d.func==HLE_DEPTH_ALWAYS);
    d=hle_3ds_depth_state(0,0); assert(!d.enabled && !d.write && d.func==HLE_DEPTH_ALWAYS);
    d=hle_3ds_depth_state(1,0); assert(d.enabled && !d.write && d.func==HLE_DEPTH_GEQUAL);
    d=hle_3ds_depth_state(2,1); assert(d.enabled && d.write && d.func==HLE_DEPTH_GREATER);
    /* Equal screen barycentrics: texture coordinate from W=1,2,4. */
    const float clips[3][4]={{0,0,0,1},{0,0,0,2},{0,0,0,4}}, u[]={0,1,1};
    float sum=0, weight=0;
    for (int i=0;i<3;++i) { float o[4]; assert(hle_3ds_homogeneous_position(clips[i],o));
        assert(o[3] == clips[i][3]); sum += u[i]/o[3]; weight += 1/o[3]; }
    assert(closef(sum/weight,3.0f/7.0f)); assert(!closef(sum/weight,2.0f/3.0f));
    /* Source top-left and bottom-right map to screen top and bottom, W=1.
       The screen-space Y NDC before OrthoTilt's physical rotation is 2*y/240-1. */
    float top[4], bottom[4];
    hle_3ds_screen_position(0,0,640,448,0.5f,top);
    hle_3ds_screen_position(640,448,640,448,0.5f,bottom);
    assert(closef(top[0],0) && closef(top[1],240) && closef(top[3],1));
    assert(closef(bottom[0],400) && closef(bottom[1],0) && closef(bottom[3],1));
    assert(closef(2*top[1]/(240*top[3])-1,1));
    assert(closef(2*bottom[1]/(240*bottom[3])-1,-1));
    float out[4]; const float zero[]={0,0,0,0}, nanv[]={NAN,0,0,1}, huge[]={3.4e38f,0,0,1};
    assert(!hle_3ds_homogeneous_position(zero,out)); assert(!hle_3ds_homogeneous_position(nanv,out)); assert(!hle_3ds_homogeneous_position(huge,out));
    /* GPU-clip rows: same X/Y/W as the CPU path, depth flipped so GE near is OrthoTilt clip z = 0. */
    float rows[4][4]; hle_3ds_gpu_clip_rows(rows);
    for (unsigned i=0; i<sizeof(inputs)/sizeof(inputs[0]); ++i) {
        const float *c=inputs[i]; float o[4], g[4]; assert(hle_3ds_homogeneous_position(c,o));
        for (int r=0;r<4;++r) g[r]=rows[r][0]*c[0]+rows[r][1]*c[1]+rows[r][2]*c[2]+rows[r][3]*c[3];
        assert(closef(g[0],o[0]) && closef(g[1],o[1]) && g[3]==o[3]);
        const float ortho_z = g[2] - g[3];            /* OrthoTilt(0..1, left-handed) row 2 */
        const float gpu_depth = ortho_z / g[3] + 1.0f;  /* DepthMap(1, 1) */
        if (c[2] >= -c[3] && c[2] <= c[3]) {            /* inside GE depth range: same window depth */
            assert(ortho_z <= 0.0001f && ortho_z >= -g[3] - 0.0001f);
            assert(closef(gpu_depth, -(o[2]-o[3])/o[3]));
        }
    }
    for (int i=0; i<3; ++i) { /* GE near, middle, far */
        const float clip[]={0,0,(float)(i-1)*2,2}; float g[4];
        for (int r=0;r<4;++r) g[r]=rows[r][0]*clip[0]+rows[r][1]*clip[1]+rows[r][2]*clip[2]+rows[r][3]*clip[3];
        assert(closef((g[2]-g[3])/g[3]+1.0f, depth[i]));
        if (i == 0) assert(closef(g[2]-g[3], 0)); /* near plane is the PICA z = 0 clip plane */
    }
    const float behind[]={0,0,-3,2}; float gb[4]; /* in front of GE near: rejected by z' <= 0 */
    for (int r=0;r<4;++r) gb[r]=rows[r][0]*behind[0]+rows[r][1]*behind[1]+rows[r][2]*behind[2]+rows[r][3]*behind[3];
    assert(gb[2]-gb[3] > 0);
    puts("homogeneous mapping, full depth chain, depth state, perspective interpolation, 2D and GPU-clip rows passed");
}
