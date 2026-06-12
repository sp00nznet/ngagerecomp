/*
 * NGageRuntime — Symbian High-Level Emulation (HLE).
 *
 * Each Symbian import the game uses is a slot in the image's import address table.
 * ngage_hle_init() makes every slot point to itself and registers that address with
 * the dispatch table, so the game's `ldr r12,[slot]; blx r12` lands in a native shim.
 *
 * Shim ABI (ARM EABI / Symbian APCS): integer args in r0..r3, `this` in r0 for C++
 * methods, return value in r0 (r0:r1 for 64-bit). A shim reads/writes the cpu context.
 */
#ifndef NGAGE_HLE_H
#define NGAGE_HLE_H

#include "ngage_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wire the import address table -> shims. Generated (gen_hle.py) from imports.json. */
void ngage_hle_init(ngage_cpu_t* c);

/* ---- implemented shims (hand-written, runtime/src/hle/) ---- */
void hle_memcpy(ngage_cpu_t*);      /* EUSER: memcpy(dst, src, n) -> dst                */
void hle_memset(ngage_cpu_t*);      /* EUSER: memset(dst, c, n)   -> dst                */
void hle_Mem_FillZ(ngage_cpu_t*);   /* EUSER: Mem::FillZ(void* p, TInt len)             */

#ifdef __cplusplus
}
#endif

#endif /* NGAGE_HLE_H */
