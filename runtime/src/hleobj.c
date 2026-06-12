/*
 * Shared HLE C++ object factory.
 *
 * Many Symbian objects the game creates through HLE (graphics contexts, window-server
 * handles, screen devices) are then used virtually — `(*(*obj+N))(obj, …)` — and deleted
 * via the vtable. ngage_hle_object() returns a zeroed guest object whose vtable slots all
 * dispatch to a no-op-success shim, so those calls resolve instead of indexing null.
 */
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include "ngage_hle.h"

#define HLE_VT_BASE 0xF2000000u
#define HLE_VT_SLOTS 192            /* window-server vtables are deep */
static uint32_t g_vt;

void hle_obj_vmethod(ngage_cpu_t* c) { c->r[0] = 0; }

static uint32_t hle_vtable(ngage_cpu_t* c) {
    if (!g_vt) {
        g_vt = ngage_alloc_zeroed(c, HLE_VT_SLOTS * 4);
        for (int i = 0; i < HLE_VT_SLOTS; i++) {
            uint32_t m = HLE_VT_BASE + (uint32_t)i;
            ngage_w32(c, g_vt + (uint32_t)i * 4, m);
            ngage_register(m, hle_obj_vmethod);
        }
    }
    return g_vt;
}

/* A single "universal" object: vtable at +0, every member pointing back to itself, so any
 * pointer-chase through it stays valid. HLE objects seed their member slots with it, so the
 * window-server object graph never dereferences null. (Members the game writes get
 * overwritten; members read as integers see a big value — usually harmless.) */
static uint32_t g_universal;
static uint32_t universal(ngage_cpu_t* c) {
    if (!g_universal) {
        uint32_t u = ngage_alloc_zeroed(c, 256 * 4);
        ngage_w32(c, u, hle_vtable(c));
        for (int i = 1; i < 256; i++) ngage_w32(c, u + (uint32_t)i * 4, u);
        g_universal = u;
    }
    return g_universal;
}

uint32_t ngage_hle_object(ngage_cpu_t* c, uint32_t size) {
    if (size < 8) size = 8;
    uint32_t o = ngage_alloc_zeroed(c, size);
    ngage_w32(c, o, hle_vtable(c));
    uint32_t u = universal(c), words = size / 4;
    for (uint32_t i = 1; i < words && i < 64; i++) ngage_w32(c, o + i * 4, u);
    return o;
}

/* The shared universal object (the same persistent instance), for singleton getters. */
uint32_t ngage_hle_universal(ngage_cpu_t* c) { return universal(c); }

/* Generic shim: a getter that must return a valid (singleton) object. */
void hle_return_object(ngage_cpu_t* c) { c->r[0] = universal(c); }
