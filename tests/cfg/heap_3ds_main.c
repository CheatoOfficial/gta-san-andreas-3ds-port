#include <3ds.h>
#include "hle_3ds_profile.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdint.h>
u32 __ctru_heap,__ctru_linear_heap,__ctru_heap_size,__ctru_linear_heap_size;
char *fake_heap_start,*fake_heap_end;
void __system_allocateHeaps(void);
static s64 total,used;
static int fail_query,fail_alloc,allocs,closes,mapped;
static jmp_buf panic;
Result svcGetResourceLimit(Handle *h,Handle process) { assert(process==CUR_PROCESS_HANDLE); *h=7; return 0; }
Result svcGetResourceLimitLimitValues(s64 *n,Handle h,ResourceLimitType *kind,int count)
{ assert(h==7&&*kind==RESLIMIT_COMMIT&&count==1); *n=total; return fail_query?-1:0; }
Result svcGetResourceLimitCurrentValues(s64 *n,Handle h,ResourceLimitType *kind,int count)
{ assert(h==7&&*kind==RESLIMIT_COMMIT&&count==1); *n=used; return 0; }
Result svcCloseHandle(Handle h) { assert(h==7); ++closes; return 0; }
Result svcControlMemory(u32 *out,u32 address,u32 other,u32 bytes,int op,int permissions)
{
    ++allocs; assert(other==0&&permissions==(MEMPERM_READ|MEMPERM_WRITE)&&bytes%4096==0);
    assert((op==MEMOP_ALLOC&&address==OS_HEAP_AREA_BEGIN)||(op==MEMOP_ALLOC_LINEAR&&address==0));
    if(allocs==fail_alloc) return -1;
    *out=op==MEMOP_ALLOC?OS_HEAP_AREA_BEGIN:0x14000000u; return 0;
}
void mappableInit(u32 a,u32 b) { assert(a==OS_MAP_AREA_BEGIN&&b==OS_MAP_AREA_END&&allocs==2); ++mapped; }
void svcBreak(int why) { assert(why==USERBREAK_PANIC); longjmp(panic,1); }
int main(void)
{
    total=64u*HLE_3DS_MIB; used=14u*HLE_3DS_MIB;
    __system_allocateHeaps();
    assert(__ctru_heap_size==42u*HLE_3DS_MIB&&__ctru_linear_heap_size==8u*HLE_3DS_MIB);
    assert((uintptr_t)fake_heap_start==OS_HEAP_AREA_BEGIN);
    assert((uintptr_t)fake_heap_end==OS_HEAP_AREA_BEGIN+__ctru_heap_size&&closes==1&&mapped==1);
    allocs=closes=mapped=0; total=124u*HLE_3DS_MIB;
    __system_allocateHeaps(); assert(__ctru_linear_heap_size==32u*HLE_3DS_MIB&&allocs==2);
    allocs=closes=mapped=0; fail_query=1;
    if(!setjmp(panic)) { __system_allocateHeaps(); assert(0); }
    assert(!allocs&&closes==1&&!mapped);
    fail_query=0; allocs=closes=0; total=used;
    if(!setjmp(panic)) { __system_allocateHeaps(); assert(0); }
    assert(!allocs&&closes==1);
    total=124u*HLE_3DS_MIB; allocs=closes=0; fail_alloc=1;
    if(!setjmp(panic)) { __system_allocateHeaps(); assert(0); }
    assert(allocs==1&&closes==1);
    puts("production pre-main allocator: mappings, newlib heap, resource errors and allocation failure passed");
}
