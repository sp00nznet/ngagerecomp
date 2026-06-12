/*
 * HLE for CONE / EIKCORE control-framework bits that must return real data.
 * Most framework calls are harmless named stubs; the ones here feed the game values it
 * actually uses during construction.
 */
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include "ngage_hle.h"

/* CEikAppUi::ApplicationRect() const -> TRect (by value: r0=result, r1=this).
 * The N-Gage screen is 176x208. */
void hle_ApplicationRect(ngage_cpu_t* c) {
    uint32_t r = c->r[0];
    ngage_w32(c, r + 0, 0);     /* iTl.iX */
    ngage_w32(c, r + 4, 0);     /* iTl.iY */
    ngage_w32(c, r + 8, 176);   /* iBr.iX */
    ngage_w32(c, r + 12, 208);  /* iBr.iY */
    c->r[0] = r;
}
