#ifndef TEXTURE_TEST_3DS_H
#define TEXTURE_TEST_3DS_H
#include <stdint.h>
typedef uint16_t u16;
#define SYSCLOCK_ARM11 268111856u
static inline uint64_t svcGetSystemTick(void) { return 0; }   /* the decode timer: no time on the host */
#endif
