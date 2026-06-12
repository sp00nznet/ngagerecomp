/*
 * HLE for the GCC compiler-runtime helpers (EUSER re-exports libgcc).
 *
 * The toolchain lowered float/double/64-bit-int math to calls into these. They're pure
 * functions, so we just do the operation natively. ABI (AAPCS soft-float): a double is a
 * register pair (r0:r1 low:high; second operand r2:r3), a float is one register, an int
 * is r0/r1. Comparisons return a value whose sign matches the relation.
 */
#include <string.h>
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include "ngage_hle.h"

static double rdd(ngage_cpu_t* c, int i) {
    uint64_t u = (uint64_t)c->r[i] | ((uint64_t)c->r[i + 1] << 32);
    double d; memcpy(&d, &u, 8); return d;
}
static void wrd(ngage_cpu_t* c, double d) {
    uint64_t u; memcpy(&u, &d, 8);
    c->r[0] = (uint32_t)u; c->r[1] = (uint32_t)(u >> 32);
}
static float rdf(ngage_cpu_t* c, int i) { uint32_t u = c->r[i]; float f; memcpy(&f, &u, 4); return f; }
static void  wrf(ngage_cpu_t* c, float f) { uint32_t u; memcpy(&u, &f, 4); c->r[0] = u; }

/* double arithmetic */
void hle_adddf3(ngage_cpu_t* c) { wrd(c, rdd(c, 0) + rdd(c, 2)); }
void hle_subdf3(ngage_cpu_t* c) { wrd(c, rdd(c, 0) - rdd(c, 2)); }
void hle_muldf3(ngage_cpu_t* c) { wrd(c, rdd(c, 0) * rdd(c, 2)); }
void hle_divdf3(ngage_cpu_t* c) { double b = rdd(c, 2); wrd(c, b != 0.0 ? rdd(c, 0) / b : 0.0); }
void hle_negdf2(ngage_cpu_t* c) { wrd(c, -rdd(c, 0)); }

/* float arithmetic */
void hle_addsf3(ngage_cpu_t* c) { wrf(c, rdf(c, 0) + rdf(c, 1)); }
void hle_subsf3(ngage_cpu_t* c) { wrf(c, rdf(c, 0) - rdf(c, 1)); }
void hle_mulsf3(ngage_cpu_t* c) { wrf(c, rdf(c, 0) * rdf(c, 1)); }

/* conversions */
void hle_floatsidf(ngage_cpu_t* c) { wrd(c, (double)(int32_t)c->r[0]); }
void hle_floatsisf(ngage_cpu_t* c) { wrf(c, (float)(int32_t)c->r[0]); }
void hle_fixdfsi(ngage_cpu_t* c)   { c->r[0] = (uint32_t)(int32_t)rdd(c, 0); }
void hle_fixsfsi(ngage_cpu_t* c)   { c->r[0] = (uint32_t)(int32_t)rdf(c, 0); }

/* integer divide/mod (libgcc) */
void hle_divsi3(ngage_cpu_t* c)  { int32_t b = (int32_t)c->r[1]; c->r[0] = b ? (uint32_t)((int32_t)c->r[0] / b) : 0; }
void hle_udivsi3(ngage_cpu_t* c) { uint32_t b = c->r[1]; c->r[0] = b ? c->r[0] / b : 0; }
void hle_modsi3(ngage_cpu_t* c)  { int32_t b = (int32_t)c->r[1]; c->r[0] = b ? (uint32_t)((int32_t)c->r[0] % b) : 0; }

/* comparisons: return <0 / 0 / >0 for a<b / a==b / a>b (caller compares against 0) */
void hle_cmpdf2(ngage_cpu_t* c) { double a = rdd(c, 0), b = rdd(c, 2); c->r[0] = (uint32_t)(int32_t)(a < b ? -1 : a > b ? 1 : 0); }
void hle_cmpsf2(ngage_cpu_t* c) { float  a = rdf(c, 0), b = rdf(c, 1); c->r[0] = (uint32_t)(int32_t)(a < b ? -1 : a > b ? 1 : 0); }
