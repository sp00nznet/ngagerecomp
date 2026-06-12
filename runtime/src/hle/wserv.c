/*
 * HLE for WS32 (the window server) — minimal, object-returning stubs.
 * Snakes uses only a handful of raw WS32 ordinals; the object factories return a usable
 * self-referential HLE object so the game's virtual calls / member chases resolve.
 */
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include "ngage_hle.h"
void hle_ws_object(ngage_cpu_t* c) { c->r[0] = ngage_hle_object(c, 256); }
void hle_ws_noop(ngage_cpu_t* c)   { c->r[0] = 0; }
