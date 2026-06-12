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
#include <stdio.h>
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include "ngage_hle.h"

/* Range guard: under -DNGAGE_MEM_GUARD, validate before a raw host memcpy/memset so a
 * wild pointer reports (with the call trace) instead of a blind host segfault. */
static void rangechk(uint32_t addr, uint32_t n, int write) {
    ngage_chk(addr, 1, write);
    if (n) ngage_chk(addr + n - 1, 1, write);
}

/* memcpy(TAny* dst, const TAny* src, unsigned n) -> dst */
void hle_memcpy(ngage_cpu_t* c) {
    uint32_t dst = c->r[0], src = c->r[1], n = c->r[2];
    rangechk(dst, n, 1); rangechk(src, n, 0);
    memmove(c->mem + dst, c->mem + src, n);   /* memmove: tolerate overlap defensively */
    /* r0 already holds dst, which is the return value */
}

/* memset(TAny* dst, int ch, unsigned n) -> dst */
void hle_memset(ngage_cpu_t* c) {
    uint32_t dst = c->r[0], ch = c->r[1], n = c->r[2];
    rangechk(dst, n, 1);
    memset(c->mem + dst, (int)(ch & 0xff), n);
}

/* Mem::FillZ(TAny* aTrg, TInt aLength) */
void hle_Mem_FillZ(ngage_cpu_t* c) {
    uint32_t p = c->r[0], n = c->r[1];
    rangechk(p, n, 1);
    memset(c->mem + p, 0, n);
}

/* ---- heap / new / delete (EABI: arg0=r0, return r0) ---- */

/* CBase::operator new(TUint aSize) — CBase memory is zero-initialised. */
void hle_CBase_new(ngage_cpu_t* c)  { c->r[0] = ngage_alloc_zeroed(c, c->r[0]); }

/* CBase::operator new(TUint aSize, TLeave) — zeroed, leaves on OOM. */
void hle_CBase_newL(ngage_cpu_t* c) {
    uint32_t p = ngage_alloc_zeroed(c, c->r[0]);
    if (!p) ngage_leave(c, -4);           /* KErrNoMemory */
    c->r[0] = p;
}

/* User::AllocL(TInt aSize) — raw alloc (not zeroed), leaves on OOM. */
void hle_User_AllocL(ngage_cpu_t* c) {
    uint32_t p = ngage_alloc(c, c->r[0]);
    if (!p) ngage_leave(c, -4);
    c->r[0] = p;
}

void hle_vec_new(ngage_cpu_t* c) { c->r[0] = ngage_alloc(c, c->r[0]); }
void hle_delete(ngage_cpu_t* c)  { ngage_free(c, c->r[0]); }

/* ---- leave / cleanup / lifecycle ---- */

/* User::LeaveIfError(TInt aReason): leave iff aReason < 0, else return it unchanged. */
void hle_User_LeaveIfError(ngage_cpu_t* c) {
    int32_t e = (int32_t)c->r[0];
    if (e < 0) ngage_leave(c, e);
    /* else: r0 already holds the (non-negative) value, which is the return */
}

/* TTrap::Trap(TInt& aResult): set *aResult = KErrNone and return 0 (no-leave path). The
 * result MUST be written or TRAP({r}, ...) loops see r != KErrNone and retry forever.
 * Real per-TRAP leave recovery still needs an inline setjmp at the call site (kernel.c). */
void hle_TTrap_Trap(ngage_cpu_t* c) {
    if (c->r[1]) ngage_w32(c, c->r[1], 0);   /* *aResult = KErrNone */
    c->r[0] = 0;
}
void hle_TTrap_UnTrap(ngage_cpu_t* c) { (void)c; }

/* CleanupStack — the pushed item is a CBase* in r0. */
void hle_Cleanup_PushL(ngage_cpu_t* c) { ngage_cleanup_push(c->r[0]); }
void hle_Cleanup_Pop(ngage_cpu_t* c)   { (void)c; ngage_cleanup_pop(); }
void hle_Cleanup_PopAndDestroy(ngage_cpu_t* c) {
    uint32_t p = ngage_cleanup_pop();
    if (p) ngage_free(c, p);              /* TODO: invoke the virtual dtor first */
}
void hle_Cleanup_PopAndDestroyN(ngage_cpu_t* c) {
    uint32_t n = c->r[0];
    while (n--) { uint32_t p = ngage_cleanup_pop(); if (p) ngage_free(c, p); }
}

/* User::Panic(const TDesC16& aCategory, TInt aReason) — FATAL: never returns in Symbian.
 * Unwind out (a returning panic causes runaway retry loops in the active-object code). */
extern uint32_t ngage_stack[512]; extern int ngage_calldepth;
void hle_User_Panic(ngage_cpu_t* c) {
    fprintf(stderr, "[GUEST PANIC] reason %d. call stack (caller -> callee):\n", (int)c->r[1]);
    int lo = ngage_calldepth > 22 ? ngage_calldepth - 22 : 0;
    for (int i = lo; i < ngage_calldepth; i++)
        fprintf(stderr, "  [%d] %#x\n", i, ngage_stack[i]);
    ngage_leave(c, -1000 - (int32_t)c->r[1]);
}
void hle_User_Exit(ngage_cpu_t* c)  { ngage_leave(c, (int32_t)c->r[0]); }   /* unwind out */
/* hle_RHandleBase_Close lives in hle/efsrv.c (it may close an open file handle). */
