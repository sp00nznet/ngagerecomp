/*
 * HLE for EUSER descriptor constructors + basic ops.
 *
 * The game builds TPtr/TPtrC/TBuf descriptors constantly (over file buffers, strings,
 * bitmaps). They must be initialised to the right type/length/pointer layout or later
 * use reads a garbage pointer. Layout matches desc.c:
 *   word0 = (type<<28) | length ;  EPtr(2)/EBuf(3): word1 = maxLength ;  EPtr: word2 = ptr.
 */
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include "ngage_hle.h"

#define DLEN(n) ((n) & 0x0fffffffu)

/* TPtr8(TUint8* aPtr, TInt aMaxLength) — EPtr, length 0 */
void hle_TPtr8_pm(ngage_cpu_t* c) {
    uint32_t o = c->r[0];
    ngage_w32(c, o, 2u << 28); ngage_w32(c, o + 4, c->r[2]); ngage_w32(c, o + 8, c->r[1]);
}
/* TPtr8(TUint8* aPtr, TInt aLength, TInt aMaxLength) */
void hle_TPtr8_plm(ngage_cpu_t* c) {
    uint32_t o = c->r[0];
    ngage_w32(c, o, (2u << 28) | DLEN(c->r[2])); ngage_w32(c, o + 4, c->r[3]); ngage_w32(c, o + 8, c->r[1]);
}
/* TPtr16(TUint16* aPtr, TInt aMaxLength) / (aPtr, aLength, aMaxLength) — same layout */
void hle_TPtr16_pm(ngage_cpu_t* c)  { hle_TPtr8_pm(c); }
void hle_TPtr16_plm(ngage_cpu_t* c) { hle_TPtr8_plm(c); }

/* TPtrC16(const TUint16* aString) — EPtrC over a NUL-terminated 16-bit string */
void hle_TPtrC16_z(ngage_cpu_t* c) {
    uint32_t o = c->r[0], s = c->r[1], n = 0;
    while (ngage_r16(c, s + n * 2)) n++;
    ngage_w32(c, o, (1u << 28) | DLEN(n)); ngage_w32(c, o + 4, s);
}

/* TBufBase8/16(TInt aMaxLength) — EBuf, length 0, inline data at +8 */
void hle_TBufBase(ngage_cpu_t* c) {
    uint32_t o = c->r[0];
    ngage_w32(c, o, 3u << 28); ngage_w32(c, o + 4, c->r[1]);
}

/* TDes8::SetLength(TInt aLength) */
void hle_TDes_SetLength(ngage_cpu_t* c) { ngage_desc_setlen(c, c->r[0], c->r[1]); }

/* TDes8::PtrZ() — NUL-terminate and return the data pointer */
void hle_TDes8_PtrZ(ngage_cpu_t* c) {
    ngage_desc_t d = ngage_desc(c, c->r[0]);
    ngage_w8(c, d.ptr + d.len, 0);
    c->r[0] = d.ptr;
}
