/*
 * NGageRuntime — helpers the recompiler's generated C calls.
 *
 * Flat little-endian guest memory + NZCV flag math. Kept as static-inline so the
 * generated translation units stay dependency-light. Scaffold: single-VA flat
 * model; a guard/MMU layer can replace these without changing generated code.
 */
#ifndef NGAGE_RUNTIME_H
#define NGAGE_RUNTIME_H

#include <stdint.h>
#include "ngage_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- guest memory (little-endian) ---- */
static inline uint32_t ngage_r32(ngage_cpu_t* c, uint32_t a) {
    const uint8_t* p = c->mem + a;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint16_t ngage_r16(ngage_cpu_t* c, uint32_t a) {
    const uint8_t* p = c->mem + a;
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline uint8_t  ngage_r8 (ngage_cpu_t* c, uint32_t a) { return c->mem[a]; }

static inline void ngage_w32(ngage_cpu_t* c, uint32_t a, uint32_t v) {
    uint8_t* p = c->mem + a;
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static inline void ngage_w16(ngage_cpu_t* c, uint32_t a, uint16_t v) {
    uint8_t* p = c->mem + a; p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
}
static inline void ngage_w8 (ngage_cpu_t* c, uint32_t a, uint8_t v) { c->mem[a]=v; }

/* ---- NZCV flags (live in cpsr) ---- */
#define NGAGE_BIT(n) (1u << (n))
static inline uint32_t ngage_nf(ngage_cpu_t* c){ return (c->cpsr >> NGAGE_FLAG_N) & 1u; }
static inline uint32_t ngage_zf(ngage_cpu_t* c){ return (c->cpsr >> NGAGE_FLAG_Z) & 1u; }
static inline uint32_t ngage_cf(ngage_cpu_t* c){ return (c->cpsr >> NGAGE_FLAG_C) & 1u; }
static inline uint32_t ngage_vf(ngage_cpu_t* c){ return (c->cpsr >> NGAGE_FLAG_V) & 1u; }

static inline void ngage_setf(ngage_cpu_t* c, int bit, uint32_t v) {
    if (v) c->cpsr |= NGAGE_BIT(bit); else c->cpsr &= ~NGAGE_BIT(bit);
}
/* N,Z from a 32-bit result. */
static inline void ngage_set_nz(ngage_cpu_t* c, uint32_t r) {
    ngage_setf(c, NGAGE_FLAG_N, r >> 31);
    ngage_setf(c, NGAGE_FLAG_Z, r == 0);
}
/* Full NZCV for an addition a+b(+cin). */
static inline uint32_t ngage_add_flags(ngage_cpu_t* c, uint32_t a, uint32_t b, uint32_t cin) {
    uint64_t u = (uint64_t)a + (uint64_t)b + (uint64_t)cin;
    uint32_t r = (uint32_t)u;
    ngage_set_nz(c, r);
    ngage_setf(c, NGAGE_FLAG_C, (uint32_t)(u >> 32));
    ngage_setf(c, NGAGE_FLAG_V, (~(a ^ b) & (a ^ r)) >> 31);
    return r;
}
/* Full NZCV for a subtraction a-b (ARM: borrow => C set when no borrow). */
static inline uint32_t ngage_sub_flags(ngage_cpu_t* c, uint32_t a, uint32_t b) {
    uint32_t r = a - b;
    ngage_set_nz(c, r);
    ngage_setf(c, NGAGE_FLAG_C, a >= b);
    ngage_setf(c, NGAGE_FLAG_V, ((a ^ b) & (a ^ r)) >> 31);
    return r;
}

/* ---- guest -> native dispatch ---- */
typedef void (*ngage_fn)(ngage_cpu_t*);
void ngage_register(uint32_t guest_addr, ngage_fn fn);   /* populate the table at startup */
void ngage_call(ngage_cpu_t* c, uint32_t guest_addr);    /* generated code calls this for bl / indirect / tail */

/* Called by generated code for anything the lifter could not translate. */
void ngage_unimplemented(ngage_cpu_t* c, uint32_t guest_addr, const char* what);

#ifdef __cplusplus
}
#endif

#endif /* NGAGE_RUNTIME_H */
