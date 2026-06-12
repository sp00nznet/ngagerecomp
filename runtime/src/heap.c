/*
 * Guest heap allocator.
 *
 * Hands out *guest* addresses from a region of the flat memory window. Each block has
 * an 8-byte header in guest memory: [uint32 size][uint32 free], payload follows at +8.
 * First-fit with a bump frontier — simple and correct for bring-up; a real RHeap with
 * coalescing/cell-lists can replace it behind the same ngage_alloc/free interface.
 */
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#ifdef NGAGE_MEM_GUARD
#include <stdio.h>
#endif

static uint32_t g_base, g_end, g_brk;

void ngage_heap_init(uint32_t base, uint32_t size) {
    g_base = (base + 7u) & ~7u;
    g_end = base + size;
    g_brk = g_base;
}

uint32_t ngage_alloc(ngage_cpu_t* c, uint32_t size) {
    size = (size + 7u) & ~7u;
    if (size == 0) size = 8;
    /* first-fit over freed blocks */
    for (uint32_t p = g_base; p < g_brk; ) {
        uint32_t bsize = ngage_r32(c, p);
        uint32_t bfree = ngage_r32(c, p + 4);
#ifdef NGAGE_MEM_GUARD
        if (bsize > (g_end - g_base) || bsize == 0) {
            fprintf(stderr, "*** HEAP CORRUPT: block @%#x has size %#x (free=%#x) ***\n", p, bsize, bfree);
            ngage_mem_fault(p, 0, 0);
        }
#endif
        if (bfree && bsize >= size) { ngage_w32(c, p + 4, 0u); return p + 8; }
        p += 8 + bsize;
    }
    /* bump the frontier */
    if (g_brk + 8 + size > g_end) return 0;          /* OOM */
    uint32_t blk = g_brk;
    ngage_w32(c, blk, size);
    ngage_w32(c, blk + 4, 0u);
    g_brk += 8 + size;
    return blk + 8;
}

uint32_t ngage_alloc_zeroed(ngage_cpu_t* c, uint32_t size) {
    uint32_t p = ngage_alloc(c, size);
    if (p) {
        uint32_t n = (size + 7u) & ~7u;
        for (uint32_t i = 0; i < n; i += 4) ngage_w32(c, p + i, 0u);
    }
    return p;
}

void ngage_free(ngage_cpu_t* c, uint32_t guest_ptr) {
    if (!guest_ptr) return;                          /* free(NULL) is a no-op */
    ngage_w32(c, guest_ptr - 4, 1u);                 /* mark the block free */
}
