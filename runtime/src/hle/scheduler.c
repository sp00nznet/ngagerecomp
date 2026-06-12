/*
 * HLE for the active scheduler + CPeriodic (EUSER).
 *
 * SonicN has no CActiveScheduler::Start import — its game loop is a CPeriodic timer whose
 * callback (sub_100170D8 -> the frame function) the framework would fire each tick. We
 * record the callback and expose ngage_pump_periodics() to drive frames directly, which
 * is how the recompiled game runs without a real OS scheduler.
 *
 * CActive / CActiveScheduler::Add / RTimer are no-ops for now (nothing depends on real
 * async completion yet).
 */
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include "ngage_hle.h"

typedef struct { uint32_t fn, ptr; } periodic_t;
static periodic_t g_per[16];
static int g_nper;

/* CPeriodic::NewL(TInt aPriority) -> new CPeriodic. Offset 8 must read 0 (= inactive),
 * which a zeroed allocation gives; the game checks it before Start. */
void hle_CPeriodic_NewL(ngage_cpu_t* c) {
    c->r[0] = ngage_alloc_zeroed(c, 32);
}

/* CPeriodic::Start(TTimeIntervalMicroSeconds32 aDelay, anInterval, TCallBack aCallBack)
 * APCS: r0=this, r1=delay, r2=interval, r3=callback.iFunction, [sp]=callback.iPtr. */
void hle_CPeriodic_Start(ngage_cpu_t* c) {
    uint32_t self = c->r[0], fn = c->r[3], ptr = ngage_r32(c, c->r[13]);
    if (self) ngage_w32(c, self + 8, 1);            /* mark active */
    if (fn && g_nper < 16) { g_per[g_nper].fn = fn; g_per[g_nper].ptr = ptr; g_nper++; }
}

/* Drive every registered periodic callback `count` times (one frame each). */
int ngage_pump_periodics(ngage_cpu_t* c, int count) {
    int fired = 0;
    for (int k = 0; k < count; k++)
        for (int i = 0; i < g_nper; i++) {
            c->r[0] = g_per[i].ptr;
            c->r[13] = 0x10700000;                  /* fresh stack per tick */
            ngage_call(c, g_per[i].fn);
            fired++;
        }
    return fired;
}
int ngage_periodic_count(void) { return g_nper; }

/* no-ops: nothing yet depends on real async completion */
void hle_sched_noop(ngage_cpu_t* c) { (void)c; }
