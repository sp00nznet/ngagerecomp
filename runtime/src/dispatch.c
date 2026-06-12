/*
 * Guest -> native dispatch table.
 *
 * Generated code routes calls / indirect branches / inter-function tail branches
 * through ngage_call(addr). Lifted functions register their guest entry address
 * (ngage_game_register), and HLE import slots register theirs (ngage_hle_init).
 *
 * Entries arrive incrementally, so the table is lazily sorted by address on the first
 * lookup after a registration and then binary-searched — O(log n) over the ~2,800
 * SonicN entries.
 */
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include <stdlib.h>

typedef struct { uint32_t addr; ngage_fn fn; } ngage_entry;

static ngage_entry* g_tab = 0;
static size_t g_n = 0, g_cap = 0;
static int g_dirty = 0;

/* lightweight call trace (debug aid) — last addresses dispatched through ngage_call */
uint32_t ngage_trace[32];
unsigned ngage_trace_pos = 0;

void ngage_register(uint32_t addr, ngage_fn fn) {
    if (g_n == g_cap) {
        g_cap = g_cap ? g_cap * 2 : 1024;
        g_tab = (ngage_entry*)realloc(g_tab, g_cap * sizeof(*g_tab));
    }
    g_tab[g_n].addr = addr;
    g_tab[g_n].fn = fn;
    g_n++;
    g_dirty = 1;
}

static int cmp_entry(const void* a, const void* b) {
    uint32_t x = ((const ngage_entry*)a)->addr, y = ((const ngage_entry*)b)->addr;
    return (x > y) - (x < y);
}

void ngage_call(ngage_cpu_t* c, uint32_t addr) {
    ngage_trace[ngage_trace_pos++ & 31] = addr;
    if (g_dirty) { qsort(g_tab, g_n, sizeof(*g_tab), cmp_entry); g_dirty = 0; }
    size_t lo = 0, hi = g_n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_tab[mid].addr < addr) lo = mid + 1;
        else hi = mid;
    }
    if (lo < g_n && g_tab[lo].addr == addr) { g_tab[lo].fn(c); return; }
    /* Not a lifted function or HLE import: not-yet-lifted code or a bad pointer. */
    ngage_unimplemented(c, addr, "call to unregistered address");
}

/* C++ virtual call: read the object's vtable, dispatch the slot at byte offset `voffset`.
 * Set the argument registers (r1..) before calling; `this` goes in r0. Returns r0. */
uint32_t ngage_vcall(ngage_cpu_t* c, uint32_t object, uint32_t voffset) {
    uint32_t vtable = ngage_r32(c, object);
    uint32_t fn = ngage_r32(c, vtable + voffset);
    if (!fn) { ngage_unimplemented(c, object, "vcall: empty vtable slot"); return 0; }
    c->r[0] = object;
    ngage_call(c, fn);
    return c->r[0];
}
