#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "hle_3ds_texture_layout.h"
static void check(unsigned w,unsigned h,unsigned image_w,unsigned image_h)
{
    unsigned *seen=calloc((size_t)w*h,sizeof(*seen));
    unsigned *pixels=calloc((size_t)w*h,sizeof(*pixels));
    assert(seen && pixels);
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
        unsigned i=hle_3ds_texture_texel_index(x,y,w,h);
        assert(i<w*h && !seen[i]); seen[i]=1;
        unsigned sx=x<image_w?x:image_w-1, sy=y<image_h?y:image_h-1;
        pixels[i]=1+sy*image_w+sx;
    }
    /* Decode every Morton index independently from the packed tile and bit pairs. */
    for(unsigned i=0;i<w*h;i++) {
        assert(seen[i]); unsigned tile=i/64, m=i%64, x=(tile%(w/8))*8, dy=(tile/(w/8))*8;
        for(unsigned bit=0;bit<3;bit++) { x+=((m>>(2*bit))&1u)<<bit; dy+=((m>>(2*bit+1))&1u)<<bit; }
        unsigned y=h-1-dy, sx=x<image_w?x:image_w-1, sy=y<image_h?y:image_h-1;
        assert(pixels[i]==1+sy*image_w+sx);
    }
    assert(pixels[hle_3ds_texture_texel_index(0,0,w,h)]==1);
    assert(pixels[hle_3ds_texture_texel_index(w-1,0,w,h)]==image_w);
    assert(pixels[hle_3ds_texture_texel_index(0,h-1,w,h)]==1+(image_h-1)*image_w);
    assert(pixels[hle_3ds_texture_texel_index(w-1,h-1,w,h)]==image_h*image_w);
    free(pixels); free(seen);
}
int main(void) { check(8,8,8,8); check(16,8,16,8); check(8,16,8,16); check(32,16,19,11); check(16,32,9,19); puts("3DS texture tiled layout, inverse addressing and padded edges passed"); }
