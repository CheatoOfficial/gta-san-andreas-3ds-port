/* libctru's weak startup hook: allocate without libc, services, or a hardware-model assumption.
 * SDK reference: devkitPro/libctru source/system/allocateHeaps.c. Uses the public kernel and newlib interfaces. */
#include "hle_3ds_profile.h"
#if HLE_3DS_AUTO_MODEL
#include <3ds.h>
#include <3ds/allocator/mappable.h>
extern u32 __ctru_heap, __ctru_linear_heap, __ctru_heap_size, __ctru_linear_heap_size;
extern char *fake_heap_start, *fake_heap_end;
static void heap_panic(void) { svcBreak(USERBREAK_PANIC); for (;;) {} }
void __system_allocateHeaps(void)
{
    Handle limit;
    if (R_FAILED(svcGetResourceLimit(&limit,CUR_PROCESS_HANDLE))) heap_panic();
    s64 total=0,used=0;
    ResourceLimitType kind=RESLIMIT_COMMIT;
    Result a=svcGetResourceLimitLimitValues(&total,limit,&kind,1);
    Result b=svcGetResourceLimitCurrentValues(&used,limit,&kind,1);
    svcCloseHandle(limit);
    if (R_FAILED(a)||R_FAILED(b)||total<=used||used<0||total-used>UINT32_MAX) heap_panic();
    hle_3ds_heap_plan((uint32_t)(total-used),&__ctru_heap_size,&__ctru_linear_heap_size);
    if (!__ctru_heap_size||!__ctru_linear_heap_size) heap_panic();
    if (R_FAILED(svcControlMemory(&__ctru_heap,OS_HEAP_AREA_BEGIN,0,__ctru_heap_size,
                                  MEMOP_ALLOC,MEMPERM_READ|MEMPERM_WRITE))) heap_panic();
    if (R_FAILED(svcControlMemory(&__ctru_linear_heap,0,0,__ctru_linear_heap_size,
                                  MEMOP_ALLOC_LINEAR,MEMPERM_READ|MEMPERM_WRITE))) heap_panic();
    mappableInit(OS_MAP_AREA_BEGIN,OS_MAP_AREA_END);
    fake_heap_start=(char *)__ctru_heap; fake_heap_end=fake_heap_start+__ctru_heap_size;
}
#endif
