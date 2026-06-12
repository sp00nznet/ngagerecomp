/*
 * Symbian leave / cleanup-stack machinery.
 *
 * Symbian error handling is non-local: User::Leave() unwinds to the nearest TRAP,
 * running the cleanup stack on the way. We mirror that with setjmp/longjmp.
 *
 * ngage_run(entry) installs a top-level trap and runs the guest entry; a leave anywhere
 * underneath unwinds here. NOTE: a guest's own nested TRAP currently does NOT install
 * its own setjmp (TTrap::Trap just tracks the cleanup level) — that needs the setjmp to
 * live at the guest TRAP call site, i.e. emitted inline by the lifter. Until then leaves
 * propagate to the outermost ngage_run, which is correct for the happy path and aborts
 * cleanly on error rather than crashing. See docs/SYMBIAN-HLE.md.
 */
#include <setjmp.h>
#include "ngage_cpu.h"
#include "ngage_runtime.h"

/* ---- cleanup stack: guest pointers pushed via CleanupStack::PushL ---- */
static uint32_t g_cleanup[1024];
static int g_cleanup_n;

void ngage_cleanup_push(uint32_t guest_ptr) {
    if (g_cleanup_n < (int)(sizeof g_cleanup / sizeof g_cleanup[0]))
        g_cleanup[g_cleanup_n++] = guest_ptr;
}
uint32_t ngage_cleanup_pop(void) {
    return g_cleanup_n ? g_cleanup[--g_cleanup_n] : 0;
}
int ngage_cleanup_level(void) { return g_cleanup_n; }

void ngage_cleanup_unwind_to(ngage_cpu_t* c, int level) {
    while (g_cleanup_n > level) {
        uint32_t p = g_cleanup[--g_cleanup_n];
        if (p) ngage_free(c, p);                 /* TODO: call the C++ dtor before freeing */
    }
}

/* ---- trap frames ---- */
typedef struct { jmp_buf jb; int cleanup_level; int32_t reason; } ngage_traph;
static ngage_traph g_traps[32];
static int g_trap_n;

int ngage_run(ngage_cpu_t* c, ngage_fn entry) {
    if (g_trap_n >= (int)(sizeof g_traps / sizeof g_traps[0])) return -1;
    ngage_traph* t = &g_traps[g_trap_n++];
    t->cleanup_level = ngage_cleanup_level();
    if (setjmp(t->jb) == 0) {
        entry(c);
        g_trap_n--;
        return 0;                                /* completed without leaving */
    }
    /* a leave unwound to here */
    ngage_cleanup_unwind_to(c, t->cleanup_level);
    g_trap_n--;
    return t->reason;
}

void ngage_leave(ngage_cpu_t* c, int32_t reason) {
    if (g_trap_n == 0) { ngage_unimplemented(c, 0, "User::Leave with no active trap"); return; }
    ngage_traph* t = &g_traps[g_trap_n - 1];
    t->reason = reason;
    ngage_cleanup_unwind_to(c, t->cleanup_level);
    longjmp(t->jb, 1);
}
