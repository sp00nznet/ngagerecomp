/*
 * NGageRuntime — ARM CPU context
 *
 * The shape the recompiler's emitted C targets. Generated functions take a
 * pointer to this context and operate on guest registers/flags/memory directly.
 *
 * Scaffold: field layout is the agreed ABI; helper prototypes are stubs.
 */
#ifndef NGAGE_CPU_H
#define NGAGE_CPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ARMv4T guest CPU state. */
typedef struct ngage_cpu {
    uint32_t r[16];   /* r0..r15; r13=sp, r14=lr, r15=pc                  */
    uint32_t cpsr;    /* condition flags (N,Z,C,V) + mode bits we model   */
    uint8_t* mem;     /* flat guest address space (single VA model)       */
    /* dispatch table guest_addr -> native fn pointer lives alongside this */
} ngage_cpu_t;

/* CPSR flag bit positions. */
enum { NGAGE_FLAG_N = 31, NGAGE_FLAG_Z = 30, NGAGE_FLAG_C = 29, NGAGE_FLAG_V = 28 };

/* Generated code calls these. Implementations live in runtime/src/cpu.c. */
/* uint32_t ngage_shift(ngage_cpu_t*, uint32_t val, uint32_t type, uint32_t amt, int set_c); */
/* void     ngage_dispatch(ngage_cpu_t*, uint32_t guest_addr); */

#ifdef __cplusplus
}
#endif

#endif /* NGAGE_CPU_H */
