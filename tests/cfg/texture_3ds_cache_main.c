/* The production cache/decoder, with a fake GPU allocator and entirely synthetic raster packets. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hle_3ds_profile.h"
#include "hle_3ds_texture.h"
#include "hle_3ds_texture_layout.h"
#include "hle_3ds_texture_pack.h"

static uint8_t ram[1024*1024];
uint8_t *hle_ram=ram;
uint32_t hle_ram_end=sizeof(ram);
static size_t live_bytes;
static unsigned frees;
static size_t tex_bytes(const C3D_Tex *t) { return (size_t)t->width*t->height*(t->fmt==GPU_RGBA8?4u:2u); }
bool C3D_TexInit(C3D_Tex *t,uint16_t w,uint16_t h,GPU_TEXCOLOR f)
{
    t->width=w; t->height=h; t->fmt=f; t->data=malloc(tex_bytes(t));
    if(t->data) live_bytes+=tex_bytes(t);
    return t->data!=NULL;
}
void C3D_TexDelete(C3D_Tex *t) { assert(t->data); live_bytes-=tex_bytes(t); free(t->data); t->data=NULL; ++frees; }
void C3D_TexFlush(C3D_Tex *t) { assert(t->data); }
void *C3D_Tex2DGetImagePtr(C3D_Tex *t,int level,void *size) { (void)size; assert(level==0); return t->data; }
uint32_t linearSpaceFree(void) { return (uint32_t)(64u*1024u*1024u-live_bytes); }
void hle_plat_log_flush(void) {}
static void put32(uint32_t p,uint32_t v) { memcpy(ram+p,&v,4); }
static void put64(uint32_t p,uint64_t v) { memcpy(ram+p,&v,8); }

static void packet_at(uint32_t p,uint32_t w,uint8_t alpha)
{
    const uint32_t h=w;
    memset(ram+p,0,0x50u+w*h*4u);
    put64(p,3ull|(1ull<<60)); put64(p+8,0xEu);
    ram[p+0x18]=0x51; ram[p+0x28]=0x52; ram[p+0x38]=0x53;
    put64(p+0x20,w|((uint64_t)h<<32)); put64(p+0x40,(2ull<<58)|(w*h*4u/16u));
    for(uint32_t y=0;y<h;y++) for(uint32_t x=0;x<w;x++) {
        uint8_t *c=ram+p+0x50u+(y*w+x)*4u;
        c[0]=(uint8_t)(x*2); c[1]=(uint8_t)(y*2); c[2]=255; c[3]=alpha;
    }
}
static void packet(uint8_t alpha) { packet_at(0x20000u,128,alpha); }
static uint32_t raster(unsigned n)
{
    uint32_t r=0x1000u+n*0x40u;
    memset(ram+r,0,0x28u); put32(r+4,0x20050u);
    put32(r+0x0C,128); put32(r+0x10,128); put32(r+0x14,32); ram[r+0x23]=5; /* direct RGBA32 */
    return r;
}
static uint16_t first16(const Hle3dsTexture *t)
{
    uint16_t value;
    uint32_t at=hle_3ds_texture_texel_index(0,0,t->tex.width,t->tex.height);
    memcpy(&value,(uint8_t *)t->tex.data+at*2u,2); return value;
}
static void exercise(int hardware)
{
    hle_3ds_profile_select(hardware,80u*HLE_3DS_MIB,32u*HLE_3DS_MIB);
    const uint8_t red[4]={255,0,0,255}, green[4]={0,255,0,255}, blue[4]={0,0,255,0}, blend[4]={255,128,0,128};
    assert(hle_3ds_pack_rgb565(red)==0xF800 && hle_3ds_pack_rgb565(green)==0x07E0);
    assert(hle_3ds_pack_rgba5551(red)==0xF801 && hle_3ds_pack_rgba5551(blue)==0x003E);
    assert(hle_3ds_pack_rgba4(blend)==0xF808);

    packet(0x80); uint32_t r=raster(0); int alpha=-1;
    assert(hle_3ds_texture_prepare(r,&alpha) && alpha==0);
    const Hle3dsTexture *world=hle_3ds_texture_get(r); assert(world);
    assert(hle_3ds_texture_prepare_2d(r));
    const Hle3dsTexture *hud=hle_3ds_texture_get_2d(r); assert(hud);
    if(HLE_3DS_COMPACT) {
        assert(world!=hud && world->tex.fmt==GPU_RGB565);
        assert(tex_bytes(&world->tex)==8192u && world->tex.width==64 && hud->tex.width==128 && tex_bytes(&hud->tex)==32768u);
        assert(first16(world)==0x001F && first16(hud)==0x001F);
    } else {
        assert(world==hud && world->tex.fmt==GPU_RGBA8 && world->tex.width==128);
        uint32_t at=hle_3ds_texture_texel_index(0,0,128,128)*4u;
        const uint8_t *c=(const uint8_t *)world->tex.data+at;
        assert(c[0]==255 && c[1]==255 && c[2]==0 && c[3]==0); /* ABGR */
    }
    unsigned before=frees;
    hle_3ds_texture_forget(r);
    assert(frees==before && !hle_3ds_texture_prepare(r,NULL)); /* this frame still references the snapshot */
    hle_3ds_texture_frame_reset();
    assert(!hle_3ds_texture_prepare(r,NULL)); /* submitted frame is not yet complete */
    hle_3ds_texture_gpu_synced();
    assert(frees>before && !hle_3ds_texture_get(r) && !hle_3ds_texture_get_2d(r));
    packet(0x40);
    assert(hle_3ds_texture_prepare(r,&alpha) && alpha==2);
    if(HLE_3DS_COMPACT) { world=hle_3ds_texture_get(r); assert(world->tex.fmt==GPU_RGBA4 && first16(world)==0x00F8); }
    hle_3ds_texture_forget(r); hle_3ds_texture_frame_reset(); hle_3ds_texture_gpu_synced();
    packet(0); ram[0x20050+3]=0x80; /* cutout contains opaque and transparent texels */
    assert(hle_3ds_texture_prepare(r,&alpha) && alpha==1);
    if(HLE_3DS_COMPACT) assert(hle_3ds_texture_get(r)->tex.fmt==GPU_RGBA5551);
    hle_3ds_texture_shutdown(); assert(live_bytes==0);

    if(HLE_3DS_COMPACT) {
        packet(0x80);
        packet_at(0x50000u,32,0x80);
        for(unsigned i=0;i<300;i++) {
            uint32_t r=raster(i);
            if(i>=200) { put32(r+4,0x50050u); put32(r+0x0C,32); put32(r+0x10,32); }
            assert(hle_3ds_texture_prepare(r,NULL));
        }
        assert(live_bytes<=2u*1024u*1024u);
        for(unsigned i=0;i<300;i++) assert(hle_3ds_texture_get(0x1000u+i*0x40u));
        unsigned refused=0;
        for(unsigned i=300;i<400;i++) if(!hle_3ds_texture_prepare(raster(i),NULL)) ++refused;
        assert(refused>0 && live_bytes<=2u*1024u*1024u); /* full pinned cache fails without freeing live GPU textures */
        hle_3ds_texture_shutdown(); assert(live_bytes==0);
    }
    puts("production 3DS texture cache: packing, alpha, detail tiers, bounded residency and deferred invalidation passed");
    assert(live_bytes==0);
}

int main(void)
{
#if HLE_3DS_AUTO_MODEL
    exercise(0); exercise(1); /* the same production cache executable, both startup selections */
#else
    exercise(!HLE_3DS_OLD_MODEL);
#endif
    return 0;
}
