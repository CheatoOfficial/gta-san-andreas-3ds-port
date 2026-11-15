.syntax unified
.cpu mpcore
.fpu vfp
.text
.align 2

.global hle_ctx_swap_asm
.type hle_ctx_swap_asm, %function
hle_ctx_swap_asm:
    stmia   r0!, {r4-r11}
    str     sp, [r0], #4
    str     lr, [r0], #4
    vstmia  r0!, {d8-d15}
    vmrs    r2, fpscr
    str     r2, [r0]

    ldmia   r1!, {r4-r11}
    ldr     sp, [r1], #4
    ldr     lr, [r1], #4
    vldmia  r1!, {d8-d15}
    ldr     r2, [r1]
    vmsr    fpscr, r2
    bx      lr
.size hle_ctx_swap_asm, .-hle_ctx_swap_asm

.global hle_ctx_trampoline
.type hle_ctx_trampoline, %function
hle_ctx_trampoline:
    mov     r0, r4
    blx     r0
.Lctx_returned:
    b       .Lctx_returned
.size hle_ctx_trampoline, .-hle_ctx_trampoline