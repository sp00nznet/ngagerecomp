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

/* ---- system time ----
 * The game times animation/state off User::TickCount() deltas; a constant stub freezes it
 * on the first frame. Advance one tick (~1/64 s on EKA1) per pumped frame so time moves. */
uint32_t ngage_g_ticks = 0;
void hle_User_TickCount(ngage_cpu_t* c) { c->r[0] = ngage_g_ticks; }
void hle_User_After(ngage_cpu_t* c) {     /* User::After(TTimeIntervalMicroSeconds32) us=r0 */
    uint32_t t = c->r[0] / 15625u;        /* us -> 1/64 s ticks */
    ngage_g_ticks += t ? t : 1;
}

/* Drive every registered periodic callback `count` times (one frame each). */
int ngage_pump_periodics(ngage_cpu_t* c, int count) {
    int fired = 0;
    for (int k = 0; k < count; k++) {
        ngage_g_ticks++;                            /* one tick period per frame */
        for (int i = 0; i < g_nper; i++) {
            c->r[0] = g_per[i].ptr;
            c->r[13] = 0x10700000;                  /* fresh stack per tick */
            ngage_call(c, g_per[i].fn);
            fired++;
        }
    }
    return fired;
}
int ngage_periodic_count(void) { return g_nper; }

/* ===========================================================================
 * Active scheduler + window-server event injection (the run-loop primitive).
 *
 * A real Symbian GUI app does nothing after ConstructL until the active
 * scheduler (run by CONE/CEikonEnv) pumps window-server events into the focused
 * control's OfferKeyEventL. Menu navigation -> a command -> the game starts its
 * CPeriodic. We don't have a real OS, so we model the minimum the recompiled
 * game observes: a list of CActive objects, request completion, and a way to
 * inject a key event / invoke a virtual directly.
 *
 * ABI (recovered from the live, relocation-applied image — see snakes vtable):
 *   - CBase-derived vtables put RTTI/offset at [0..1], the destructor at [2],
 *     and the first real virtual at [3].
 *   - CCoeControl::OfferKeyEventL = vtable slot 3, Draw = slot 26.
 *   - CActive layout: +0 vptr, +4 iStatus (TRequestStatus), +8 iActive;
 *     virtuals DoCancel=3, RunL=4, RunError=5.
 *   - TKeyEvent { TUint iCode; TInt iScanCode; TUint iModifiers; TInt iRepeats }.
 *   - TKeyEvent codes (TEventCode): EEventKey=2.
 * ========================================================================= */
#define KRequestPending  ((int32_t)0x80000001)
#define COE_SLOT_OFFERKEY   3
#define COE_SLOT_DRAW       26
#define ACTIVE_SLOT_RUNL    4
#define ACTIVE_ISTATUS_OFF  4

static uint32_t g_active[64];
static int g_nactive;

/* CActiveScheduler::Add(CActive*) — r1 = the active object (r0 = scheduler). */
void hle_CActiveScheduler_Add(ngage_cpu_t* c) {
    uint32_t a = c->r[1];
    if (a && g_nactive < 64) {
        for (int i = 0; i < g_nactive; i++) if (g_active[i] == a) return;
        g_active[g_nactive++] = a;
    }
}

/* User::RequestComplete(TRequestStatus*& aStatus, TInt aReason)
 * r0 = &(pointer-to-status), r1 = reason. Writes reason into *aStatus, nulls the ref. */
void hle_User_RequestComplete(ngage_cpu_t* c) {
    uint32_t pp = c->r[0];
    if (pp) {
        uint32_t status = ngage_r32(c, pp);
        if (status) ngage_w32(c, status, c->r[1]);
        ngage_w32(c, pp, 0);
    }
}

/* User::WaitForRequest(TRequestStatus&) — synchronous model: we complete requests
 * eagerly, so there is nothing to block on. */
void hle_User_WaitForRequest(ngage_cpu_t* c) { (void)c; }

/* CActive::SetActive() — mark the object active (iActive at +8). */
void hle_CActive_SetActive(ngage_cpu_t* c) {
    if (c->r[0]) ngage_w32(c, c->r[0] + 8, 1);
}

/* Invoke a virtual method: obj->vtable[slot](obj, a1, a2). Returns r0. */
uint32_t ngage_call_vmethod(ngage_cpu_t* c, uint32_t obj, int slot,
                            uint32_t a1, uint32_t a2) {
    if (!obj) return 0;
    uint32_t vt = ngage_r32(c, obj);
    uint32_t fn = ngage_r32(c, vt + (uint32_t)slot * 4);
    if (!fn || fn < NGAGE_IMAGE_BASE) return 0;
    c->r[0] = obj; c->r[1] = a1; c->r[2] = a2; c->r[13] = 0x10700000;
    ngage_call(c, fn);
    return c->r[0];
}

/* One scheduler iteration: run RunL for any CActive whose request has completed
 * (iStatus != KRequestPending). Returns how many ran. */
int ngage_as_step(ngage_cpu_t* c) {
    int ran = 0;
    for (int i = 0; i < g_nactive; i++) {
        uint32_t a = g_active[i];
        int32_t st = (int32_t)ngage_r32(c, a + ACTIVE_ISTATUS_OFF);
        if (st != KRequestPending) {
            ngage_w32(c, a + 8, 0);                 /* clear iActive */
            ngage_call_vmethod(c, a, ACTIVE_SLOT_RUNL, 0, 0);
            ran++;
        }
    }
    return ran;
}
int ngage_active_count(void) { return g_nactive; }

/* Inject a key event into a control: build a TKeyEvent on the guest stack and
 * call control->OfferKeyEventL(&event, EEventKey). Returns the consume code. */
uint32_t ngage_inject_key(ngage_cpu_t* c, uint32_t control,
                          uint32_t code, uint32_t scancode) {
    uint32_t ev = ngage_alloc_zeroed(c, 16);
    ngage_w32(c, ev + 0, code);                     /* iCode */
    ngage_w32(c, ev + 4, scancode);                 /* iScanCode */
    ngage_w32(c, ev + 8, 0);                        /* iModifiers */
    ngage_w32(c, ev + 12, 0);                       /* iRepeats */
    return ngage_call_vmethod(c, control, COE_SLOT_OFFERKEY, ev, 2 /*EEventKey*/);
}

/* Ask a control to redraw itself: control->Draw(wholeRect). */
void ngage_invoke_draw(ngage_cpu_t* c, uint32_t control, uint32_t rect) {
    ngage_call_vmethod(c, control, COE_SLOT_DRAW, rect, 0);
}

/* no-ops: nothing yet depends on real async completion */
void hle_sched_noop(ngage_cpu_t* c) { (void)c; }
