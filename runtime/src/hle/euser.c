/*
 * HLE shims for EUSER.DLL — the bring-up batch.
 *
 * Starting with the pure-computation exports (mem / runtime helpers): they have
 * unambiguous semantics, are called constantly (asset loading, init), and need no
 * Symbian server/IPC state. Args follow the ARM EABI (r0..r3, return in r0).
 *
 * The wider EUSER surface (heap, descriptors, cleanup stack, active scheduler) builds
 * on this file as it comes online — see docs/SYMBIAN-HLE.md and HLE-IMPORTS.md.
 */
#include <string.h>
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include "ngage_hle.h"

/* memcpy(TAny* dst, const TAny* src, unsigned n) -> dst */
void hle_memcpy(ngage_cpu_t* c) {
    uint32_t dst = c->r[0], src = c->r[1], n = c->r[2];
    memmove(c->mem + dst, c->mem + src, n);   /* memmove: tolerate overlap defensively */
    /* r0 already holds dst, which is the return value */
}

/* memset(TAny* dst, int ch, unsigned n) -> dst */
void hle_memset(ngage_cpu_t* c) {
    uint32_t dst = c->r[0], ch = c->r[1], n = c->r[2];
    memset(c->mem + dst, (int)(ch & 0xff), n);
}

/* Mem::FillZ(TAny* aTrg, TInt aLength) */
void hle_Mem_FillZ(ngage_cpu_t* c) {
    uint32_t p = c->r[0], n = c->r[1];
    memset(c->mem + p, 0, n);
}
