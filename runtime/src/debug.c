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
extern uint32_t ngage_trace[32];
extern unsigned ngage_trace_pos;

void ngage_mem_guard_init(uint32_t lo, uint32_t hi) { ngage_mem_lo = lo; ngage_mem_hi = hi; }

void ngage_mem_fault(uint32_t addr, int size, int write) {
    fprintf(stderr, "\n*** GUEST MEM FAULT: %s %d-byte @ %#x  (valid %#x..%#x) ***\n",
            write ? "write" : "read", size, addr, ngage_mem_lo, ngage_mem_hi);
    int n = ngage_trace_pos > 20 ? 20 : (int)ngage_trace_pos;
    fprintf(stderr, "last %d dispatched guest addrs (newest last):\n", n);
    for (int i = n; i > 0; i--)
        fprintf(stderr, "  %#x\n", ngage_trace[(ngage_trace_pos - (unsigned)i) & 31]);
    abort();
}
