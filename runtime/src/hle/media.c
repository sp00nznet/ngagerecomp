/*
 * HLE for MEDIACLIENTAUDIOSTREAM (CMdaAudioOutputStream).
 *
 * SonicN sets up audio output during init: it creates a CMdaAudioOutputStream and calls
 * virtual methods on it. We don't render audio yet, but the object must be REAL enough
 * that those virtual calls resolve to something benign — otherwise they index a garbage
 * vtable and the game's error path retries forever (and panics).
 *
 * Pattern (reusable for any HLE-created C++ object the game calls virtually): allocate a
 * guest object whose vtable slots hold sentinel addresses registered to a no-op-success
 * shim. `(*(*obj+N))(obj, …)` then dispatches to hle_media_method -> KErrNone.
 */
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include "ngage_hle.h"

#define HLE_METHOD_BASE 0xF0000000u     /* sentinel call targets (never real guest code) */
static uint32_t g_audio_vtable;

/* every stream virtual method: succeed and do nothing */
void hle_media_method(ngage_cpu_t* c) { c->r[0] = 0; }

static uint32_t audio_vtable(ngage_cpu_t* c) {
    if (!g_audio_vtable) {
        g_audio_vtable = ngage_alloc_zeroed(c, 16 * 4);
        for (int i = 0; i < 16; i++) {
            uint32_t m = HLE_METHOD_BASE + (uint32_t)i;
            ngage_w32(c, g_audio_vtable + (uint32_t)i * 4, m);
            ngage_register(m, hle_media_method);
        }
    }
    return g_audio_vtable;
}

/* CMdaAudioOutputStream factory (MEDIACLIENTAUDIOSTREAM ordinal 2). Returns a stream
 * object with a controlled vtable so the game's virtual calls on it succeed. */
void hle_CMdaAudioOutputStream_NewL(ngage_cpu_t* c) {
    uint32_t obj = ngage_alloc_zeroed(c, 128);
    ngage_w32(c, obj, audio_vtable(c));
    c->r[0] = obj;
}
