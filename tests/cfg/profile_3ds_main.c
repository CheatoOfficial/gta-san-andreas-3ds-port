#include "hle_3ds_profile.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    uint32_t app,gpu;
    const uint32_t budgets[]={0,4095,4096,7u*HLE_3DS_MIB,50u*HLE_3DS_MIB,88u*HLE_3DS_MIB-4096,
                              88u*HLE_3DS_MIB,112u*HLE_3DS_MIB,UINT32_MAX};
    for(unsigned i=0;i<sizeof(budgets)/sizeof(*budgets);i++) {
        hle_3ds_heap_plan(budgets[i],&app,&gpu);
        assert(!(app&4095u)&&!(gpu&4095u));
        assert((uint64_t)app+gpu<=budgets[i] && app<=96u*HLE_3DS_MIB);
    }
    hle_3ds_heap_plan(50u*HLE_3DS_MIB,&app,&gpu);
    assert(app==42u*HLE_3DS_MIB&&gpu==8u*HLE_3DS_MIB);
    hle_3ds_profile_select(0,app,gpu);
    assert(HLE_3DS_COMPACT && !hle_3ds_extra_controls);
    hle_3ds_heap_plan(88u*HLE_3DS_MIB,&app,&gpu);
    assert(app==56u*HLE_3DS_MIB&&gpu==32u*HLE_3DS_MIB);
    hle_3ds_profile_select(1,app,gpu);
    assert(!HLE_3DS_COMPACT&&hle_3ds_extra_controls&&W3D_C3D_FRAME_VERTEX_CAPACITY==196608);
    hle_3ds_profile_select(0,app,gpu); /* generous emulator/dev allowance must not imply New hardware */
    assert(HLE_3DS_COMPACT&&!hle_3ds_extra_controls);
    hle_3ds_profile_select(1,40u*HLE_3DS_MIB,8u*HLE_3DS_MIB);
    assert(HLE_3DS_COMPACT&&hle_3ds_extra_controls); /* low-memory New launch keeps its physical controls */
    hle_3ds_profile_select(1,56u*HLE_3DS_MIB-4096,32u*HLE_3DS_MIB);
    assert(HLE_3DS_COMPACT);
    hle_3ds_profile_select(1,80u*HLE_3DS_MIB,32u*HLE_3DS_MIB-4096);
    assert(HLE_3DS_COMPACT);
    puts("startup heap bounds, hardware identification and conservative New-model fallback passed");
}
