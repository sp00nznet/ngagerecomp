/*
 * Guest -> native dispatch table.
 *
 * Generated code routes calls / indirect branches / inter-function tail branches
 * through ngage_call(addr). Each lifted function registers its guest entry address
 * at startup via ngage_register(). Linear scan for now — fine for bring-up; swap for
 * a sorted bsearch or hash once the function count grows.
 */
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include <stdlib.h>

typedef struct { uint32_t addr; ngage_fn fn; } ngage_entry;

static ngage_entry* g_tab = 0;
static size_t g_n = 0, g_cap = 0;

void ngage_register(uint32_t addr, ngage_fn fn) {
    if (g_n == g_cap) {
        g_cap = g_cap ? g_cap * 2 : 256;
        g_tab = (ngage_entry*)realloc(g_tab, g_cap * sizeof(*g_tab));
    }
    g_tab[g_n].addr = addr;
    g_tab[g_n].fn = fn;
    g_n++;
}

void ngage_call(ngage_cpu_t* c, uint32_t addr) {
    for (size_t i = 0; i < g_n; i++) {
        if (g_tab[i].addr == addr) { g_tab[i].fn(c); return; }
    }
    /* Not a lifted function: an HLE import or not-yet-lifted code. */
    ngage_unimplemented(c, addr, "call to unregistered address");
}
