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

void ngage_mem_fault(uint32_t addr, int size, int write) {
    fprintf(stderr, "\n*** GUEST MEM FAULT: %s %d-byte @ %#x  (valid %#x..%#x) ***\n",
            write ? "write" : "read", size, addr, ngage_mem_lo, ngage_mem_hi);
    int lo = ngage_calldepth > 24 ? ngage_calldepth - 24 : 0;
    fprintf(stderr, "call stack (caller -> callee):\n");
    for (int i = lo; i < ngage_calldepth; i++)
        fprintf(stderr, "  [%d] %#x\n", i, ngage_stack[i]);
    abort();
}
