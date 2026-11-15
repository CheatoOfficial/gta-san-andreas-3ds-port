#ifndef TEST_3DS_STUB_H
#define TEST_3DS_STUB_H
#include <stdint.h>
void GSPGPU_FlushDataCache(void *, uint32_t);
static inline uint64_t svcGetSystemTick(void) { return 0; } /* host stub: conversion timing reads zero */
#endif
