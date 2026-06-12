/*
 * NGageRuntime — helpers the recompiler's generated C calls.
 *
 * Flat little-endian guest memory + NZCV flag math. Kept as static-inline so the
 * generated translation units stay dependency-light. Scaffold: single-VA flat
 * model; a guard/MMU layer can replace these without changing generated code.
 */
#ifndef NGAGE_RUNTIME_H
#define NGAGE_RUNTIME_H

#include <stdint.h>
#include "ngage_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* N-Gage image base. The flat model biases the backing buffer by this, so a guest
 * address indexes directly: ngage_*(c, addr) touches (c->mem + addr). Set
 * c->mem = backing_buffer - NGAGE_IMAGE_BASE for a window starting at the image.
 * (A paged model can replace this later without touching generated code.) */
#define NGAGE_IMAGE_BASE 0x10000000u

/* ---- optional guest-memory bounds guard (debug bring-up: -DNGAGE_MEM_GUARD) ---- */
#ifdef NGAGE_MEM_GUARD
extern uint32_t ngage_mem_lo, ngage_mem_hi;
void ngage_mem_fault(uint32_t addr, int size, int write);   /* prints + aborts */
static inline void ngage_chk(uint32_t a, int sz, int w) {
    if (a < ngage_mem_lo || a + (uint32_t)sz > ngage_mem_hi) ngage_mem_fault(a, sz, w);
}
extern uint32_t ngage_watch_addr, ngage_watch_val; extern int ngage_watch_on;
void ngage_watch_set(uint32_t a);
void ngage_watch_setval(uint32_t v);
void ngage_watch_hit(uint32_t a, uint32_t v);
#define ngage_watch(a, v) do { \
    if (ngage_watch_on && ((ngage_watch_addr && (a) == ngage_watch_addr) || \
                           (ngage_watch_val && (v) == ngage_watch_val))) ngage_watch_hit((a), (v)); } while (0)
#else
#define ngage_chk(a, sz, w) ((void)0)
#define ngage_watch(a, v)   ((void)0)
#endif

/* ---- guest memory (little-endian) ---- */
static inline uint32_t ngage_r32(ngage_cpu_t* c, uint32_t a) {
    ngage_chk(a, 4, 0);
    const uint8_t* p = c->mem + a;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint16_t ngage_r16(ngage_cpu_t* c, uint32_t a) {
    ngage_chk(a, 2, 0);
    const uint8_t* p = c->mem + a;
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static inline uint8_t  ngage_r8 (ngage_cpu_t* c, uint32_t a) { ngage_chk(a, 1, 0); return c->mem[a]; }

static inline void ngage_w32(ngage_cpu_t* c, uint32_t a, uint32_t v) {
    ngage_chk(a, 4, 1); ngage_watch(a, v);
    uint8_t* p = c->mem + a;
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static inline void ngage_w16(ngage_cpu_t* c, uint32_t a, uint16_t v) {
    ngage_chk(a, 2, 1);
    uint8_t* p = c->mem + a; p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
}
static inline void ngage_w8 (ngage_cpu_t* c, uint32_t a, uint8_t v) { ngage_chk(a, 1, 1); c->mem[a]=v; }

/* ---- NZCV flags (live in cpsr) ---- */
#define NGAGE_BIT(n) (1u << (n))
static inline uint32_t ngage_nf(ngage_cpu_t* c){ return (c->cpsr >> NGAGE_FLAG_N) & 1u; }
static inline uint32_t ngage_zf(ngage_cpu_t* c){ return (c->cpsr >> NGAGE_FLAG_Z) & 1u; }
static inline uint32_t ngage_cf(ngage_cpu_t* c){ return (c->cpsr >> NGAGE_FLAG_C) & 1u; }
static inline uint32_t ngage_vf(ngage_cpu_t* c){ return (c->cpsr >> NGAGE_FLAG_V) & 1u; }

static inline void ngage_setf(ngage_cpu_t* c, int bit, uint32_t v) {
    if (v) c->cpsr |= NGAGE_BIT(bit); else c->cpsr &= ~NGAGE_BIT(bit);
}
/* N,Z from a 32-bit result. */
static inline void ngage_set_nz(ngage_cpu_t* c, uint32_t r) {
    ngage_setf(c, NGAGE_FLAG_N, r >> 31);
    ngage_setf(c, NGAGE_FLAG_Z, r == 0);
}
/* Full NZCV for an addition a+b(+cin). */
static inline uint32_t ngage_add_flags(ngage_cpu_t* c, uint32_t a, uint32_t b, uint32_t cin) {
    uint64_t u = (uint64_t)a + (uint64_t)b + (uint64_t)cin;
    uint32_t r = (uint32_t)u;
    ngage_set_nz(c, r);
    ngage_setf(c, NGAGE_FLAG_C, (uint32_t)(u >> 32));
    ngage_setf(c, NGAGE_FLAG_V, (~(a ^ b) & (a ^ r)) >> 31);
    return r;
}
/* Full NZCV for a subtraction a-b (ARM: borrow => C set when no borrow). */
static inline uint32_t ngage_sub_flags(ngage_cpu_t* c, uint32_t a, uint32_t b) {
    uint32_t r = a - b;
    ngage_set_nz(c, r);
    ngage_setf(c, NGAGE_FLAG_C, a >= b);
    ngage_setf(c, NGAGE_FLAG_V, ((a ^ b) & (a ^ r)) >> 31);
    return r;
}

/* ---- register-amount shifts (ARM uses Rs[7:0]; C shift by >= width is UB) ---- */
static inline uint32_t ngage_lsl(uint32_t v, uint32_t amt){ amt&=0xff; return amt>=32?0u:(v<<amt); }
static inline uint32_t ngage_lsr(uint32_t v, uint32_t amt){ amt&=0xff; return amt>=32?0u:(v>>amt); }
static inline uint32_t ngage_asr(uint32_t v, uint32_t amt){ amt&=0xff; if(amt>=32)amt=31; return (uint32_t)((int32_t)v>>amt); }
static inline uint32_t ngage_ror(uint32_t v, uint32_t amt){ amt&=0xff; if(!amt)return v; amt&=31; return amt?((v>>amt)|(v<<(32-amt))):v; }

/* ---- guest -> native dispatch ---- */
typedef void (*ngage_fn)(ngage_cpu_t*);

/* ---- guest heap (returns guest addresses; 0 = OOM) ---- */
void     ngage_heap_init(uint32_t base, uint32_t size);
uint32_t ngage_alloc(ngage_cpu_t* c, uint32_t size);
uint32_t ngage_alloc_zeroed(ngage_cpu_t* c, uint32_t size);
void     ngage_free(ngage_cpu_t* c, uint32_t guest_ptr);

/* ---- Symbian descriptors (TDesC/TDes) ----
 * First word packs type in the top 4 bits, length in the low 28. Layout by type:
 *   EBufC(0): [len] data@+4      EPtrC(1): [len][ptr]
 *   EPtr(2):  [len][max][ptr]    EBuf(3):  [len][max] data@+8     EBufCPtr(4): [len][max][ptr]
 * ngage_desc() resolves any of these to a flat (ptr, len, maxlen). Lengths are in
 * elements: bytes for 8-bit descriptors, 16-bit units for 16-bit ones. */
typedef struct { uint32_t ptr; uint32_t len; uint32_t maxlen; } ngage_desc_t;
ngage_desc_t ngage_desc(ngage_cpu_t* c, uint32_t addr);
void         ngage_desc_setlen(ngage_cpu_t* c, uint32_t addr, uint32_t len);

/* ---- image data ---- */
int  ngage_load_image(ngage_cpu_t* c, const char* segments_bin);  /* segs -> guest mem; count or -1 */
void ngage_mem_guard_init(uint32_t lo, uint32_t hi);              /* debug: valid guest addr range */

/* ---- host file backing for EFSRV ---- */
void ngage_fs_mount(const char* host_root);   /* directory the guest filesystem maps to */

/* ---- framebuffer presentation ----
 * TDisplayMode values we convert. N-Gage screen is 176x208; games typically render in
 * EColor4K (12-bit) or EColor64K (16-bit RGB565). present() reads the guest pixel buffer,
 * converts to RGB, and pushes a frame to the host (PPM dump now; SDL backend can slot in). */
enum { NGAGE_DM_GRAY256 = 4, NGAGE_DM_COLOR256 = 6, NGAGE_DM_COLOR64K = 7,
       NGAGE_DM_COLOR16M = 8, NGAGE_DM_COLOR4K = 10, NGAGE_DM_COLOR16MU = 11 };
void ngage_fb_init(const char* out_dir);                       /* where frames are written */
int  ngage_fb_bytewidth(int width, int mode);                  /* DWORD-aligned scanline bytes */
void ngage_present(ngage_cpu_t* c, uint32_t pixels, int w, int h, int mode);
const uint8_t* ngage_fb_rgb(int* w, int* h);                   /* latest frame as RGB888 (host) */

/* ---- Symbian leave / cleanup-stack ---- */
int      ngage_run(ngage_cpu_t* c, ngage_fn entry);   /* top-level trap; returns leave code or 0 */
void     ngage_leave(ngage_cpu_t* c, int32_t reason); /* non-local unwind to nearest trap (no return) */
void     ngage_cleanup_push(uint32_t guest_ptr);
uint32_t ngage_cleanup_pop(void);
int      ngage_cleanup_level(void);
void     ngage_cleanup_unwind_to(ngage_cpu_t* c, int level);

void ngage_register(uint32_t guest_addr, ngage_fn fn);   /* populate the table at startup */
void ngage_call(ngage_cpu_t* c, uint32_t guest_addr);    /* generated code calls this for bl / indirect / tail */
void ngage_game_register(void);                          /* register all lifted funcs (generated) */
uint32_t ngage_vcall(ngage_cpu_t* c, uint32_t object, uint32_t voffset); /* C++ virtual dispatch */

/* Called by generated code for anything the lifter could not translate. */
void ngage_unimplemented(ngage_cpu_t* c, uint32_t guest_addr, const char* what);

#ifdef __cplusplus
}
#endif

#endif /* NGAGE_RUNTIME_H */
