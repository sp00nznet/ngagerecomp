/*
 * Debug aids for bring-up. Compiled into guard builds (-DNGAGE_MEM_GUARD).
 * ngage_mem_fault() turns an out-of-bounds guest access into a precise report
 * (faulting address + the recent call trace) instead of a blind host segfault.
 */
#include <stdio.h>
#include <stdlib.h>
#include "ngage_cpu.h"
#include "ngage_runtime.h"

uint32_t ngage_mem_lo = 0, ngage_mem_hi = 0xffffffffu;
extern uint32_t ngage_stack[512];
extern int ngage_calldepth;

void ngage_mem_guard_init(uint32_t lo, uint32_t hi) { ngage_mem_lo = lo; ngage_mem_hi = hi; }

/* soft mode: OOB accesses return 0 / are dropped instead of aborting (to run past nulls) */
int ngage_soft_guard = 0;
static long g_soft_reads = 0, g_soft_writes = 0;
void ngage_soft_set(int on) { ngage_soft_guard = on; }
long ngage_soft_reads(void)  { return g_soft_reads; }
long ngage_soft_writes(void) { return g_soft_writes; }
void ngage_soft_hit(uint32_t addr, int size, int write) {
    (void)addr; (void)size;
    if (write) g_soft_writes++; else g_soft_reads++;
}

/* write watchpoint: report the call stack when a chosen guest address or value is written */
uint32_t ngage_watch_addr = 0, ngage_watch_val = 0;
int ngage_watch_on = 0;
static int g_watch_count = 0;
void ngage_watch_set(uint32_t a) { ngage_watch_addr = a; ngage_watch_on = 1; }
void ngage_watch_setval(uint32_t v) { ngage_watch_val = v; ngage_watch_on = 1; }
void ngage_watch_hit(uint32_t a, uint32_t v) {
    if (g_watch_count++ > 8) return;
    fprintf(stderr, "*** WATCH#%d: write %#x to %#x ***  call stack:\n", g_watch_count, v, a);
    int lo = ngage_calldepth > 12 ? ngage_calldepth - 12 : 0;
    for (int i = lo; i < ngage_calldepth; i++) fprintf(stderr, "  [%d] %#x\n", i, ngage_stack[i]);
}

void ngage_mem_fault(uint32_t addr, int size, int write) {
    fprintf(stderr, "\n*** GUEST MEM FAULT: %s %d-byte @ %#x  (valid %#x..%#x) ***\n",
            write ? "write" : "read", size, addr, ngage_mem_lo, ngage_mem_hi);
    int lo = ngage_calldepth > 24 ? ngage_calldepth - 24 : 0;
    fprintf(stderr, "call stack (caller -> callee):\n");
    for (int i = lo; i < ngage_calldepth; i++)
        fprintf(stderr, "  [%d] %#x\n", i, ngage_stack[i]);
    abort();
}
