// fpctl -- the floating-point control register, on either ARM.
//
// Not a port of any object. The 3DS's ARM11 keeps its rounding mode and
// its flush-to-zero and default-NaN bits in the VFP's FPSCR; the R36S's
// aarch64 build keeps the same bits at the same positions in FPCR: DN 25,
// FZ 24, RMode 23:22 (0b11 toward zero). main.c's world step uses them to
// round as the EE does.

#ifndef FPCTL_H
#define FPCTL_H

#include <stdint.h>

#define FPCTL_DN       0x02000000u
#define FPCTL_FZ       0x01000000u
#define FPCTL_RZ       0x00c00000u   // RMode 0b11, toward zero
#define FPCTL_RUNFAST  (FPCTL_DN | FPCTL_FZ)

static inline uint32_t fpctl_get(void)
{
#if defined(__aarch64__)
    uint64_t v;
    __asm__ volatile("mrs %0, fpcr" : "=r"(v));
    return (uint32_t)v;
#else
    uint32_t v;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(v));
    return v;
#endif
}

static inline void fpctl_set(uint32_t v)
{
#if defined(__aarch64__)
    __asm__ volatile("msr fpcr, %0" : : "r"((uint64_t)v) : "memory");
#else
    __asm__ volatile("vmsr fpscr, %0" : : "r"(v) : "memory");
#endif
}

#endif
