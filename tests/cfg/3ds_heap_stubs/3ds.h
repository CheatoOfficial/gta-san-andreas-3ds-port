#ifndef HEAP_TEST_3DS_H
#define HEAP_TEST_3DS_H
#include <stdint.h>
typedef uint32_t u32,Handle;
typedef int64_t s64;
typedef int32_t Result;
typedef enum {RESLIMIT_COMMIT} ResourceLimitType;
#define R_FAILED(r) ((r)<0)
#define CUR_PROCESS_HANDLE 0xFFFF8001u
#define USERBREAK_PANIC 0
#define OS_HEAP_AREA_BEGIN 0x08000000u
#define OS_MAP_AREA_BEGIN 0x10000000u
#define OS_MAP_AREA_END 0x14000000u
#define MEMOP_ALLOC 1
#define MEMOP_ALLOC_LINEAR 2
#define MEMPERM_READ 1
#define MEMPERM_WRITE 2
Result svcGetResourceLimit(Handle *,Handle);
Result svcGetResourceLimitLimitValues(s64 *,Handle,ResourceLimitType *,int);
Result svcGetResourceLimitCurrentValues(s64 *,Handle,ResourceLimitType *,int);
Result svcCloseHandle(Handle);
Result svcControlMemory(u32 *,u32,u32,u32,int,int);
void svcBreak(int);
#endif
