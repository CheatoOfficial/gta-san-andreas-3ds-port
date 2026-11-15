/* ARM11 coroutine support for PS2RC's cooperative guest-thread scheduler. */
#include <stdint.h>
#include <string.h>
#include "hle_plat.h"

#if defined(HLE_3DS)
extern void hle_ctx_swap_asm(uint32_t *save, const uint32_t *load);
extern void hle_ctx_trampoline(void);

#define STACK_PAINT 0x57AC57ACu

size_t hle_plat_stack_used(void *stack, size_t size)
{
    const uint32_t *words = (const uint32_t *)stack;
    size_t untouched = 0;
    while (untouched < size / sizeof(*words) && words[untouched] == STACK_PAINT)
        untouched++;
    return size - untouched * sizeof(*words);
}

void hle_plat_ctx_make(hle_hostctx *ctx, void *stack, size_t size, void (*fn)(void))
{
    memset(ctx, 0, sizeof(*ctx));
    uintptr_t top = (uintptr_t)stack + size;
    top &= ~(uintptr_t)7;
    ctx->w[0] = (uint32_t)(uintptr_t)fn;               /* r4: hle_ctx_trampoline entry */
    ctx->w[8] = (uint32_t)top;                         /* sp */
    ctx->w[9] = (uint32_t)(uintptr_t)hle_ctx_trampoline; /* lr */
}

void hle_plat_ctx_swap(hle_hostctx *save, hle_hostctx *load)
{
    hle_ctx_swap_asm(save->w, load->w);
}
#endif