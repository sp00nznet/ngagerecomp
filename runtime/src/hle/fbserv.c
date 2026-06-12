/*
 * HLE shims for FBSCLI (font/bitmap server) + BITGDI + NOKIAFC.
 *
 * SonicN renders by hand into a CFbsBitmap's pixel buffer, then flips to screen. So the
 * job here is small but central: give CFbsBitmap a real guest pixel buffer (DataAddress)
 * and present it. BITGDI's graphics context is barely used (no draw calls are imported),
 * so those are thin stubs. NOKIAFC is the N-Gage full-screen flip.
 *
 * Struct-return ABI (APCS): a function returning a >4-byte struct by value takes a hidden
 * result pointer in r0, so `this` shifts to r1. SizeInPixels/Header use that.
 */
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include "ngage_hle.h"

typedef struct { uint32_t key; int w, h, mode; uint32_t buf; } bm_t;
static bm_t g_bm[32];

static bm_t* bm_find(uint32_t key, int alloc) {
    for (int i = 0; i < 32; i++) if (g_bm[i].key == key) return &g_bm[i];
    if (!alloc) return 0;
    for (int i = 0; i < 32; i++) if (!g_bm[i].key) { g_bm[i].key = key; return &g_bm[i]; }
    return 0;
}

static uint32_t obj_vtable(ngage_cpu_t* c);   /* synthetic no-op vtable (defined below) */

/* ---- CFbsBitmap ---- */
void hle_CFbsBitmap_ctor(ngage_cpu_t* c) {
    bm_find(c->r[0], 1);
    ngage_w32(c, c->r[0], obj_vtable(c));      /* so the game can delete it virtually */
}

void hle_CFbsBitmap_Create(ngage_cpu_t* c) {        /* Create(const TSize&, TDisplayMode) */
    uint32_t sz = c->r[1];
    int w = (int)ngage_r32(c, sz), h = (int)ngage_r32(c, sz + 4), mode = (int)c->r[2];
    bm_t* b = bm_find(c->r[0], 1);
    if (!b) { c->r[0] = (uint32_t)-4; return; }
    b->w = w; b->h = h; b->mode = mode;
    b->buf = ngage_alloc_zeroed(c, (uint32_t)ngage_fb_bytewidth(w, mode) * (h > 0 ? h : 0));
    c->r[0] = b->buf ? 0u : (uint32_t)-4;            /* KErrNone / KErrNoMemory */
}

void hle_CFbsBitmap_DataAddress(ngage_cpu_t* c) {   /* TUint32* DataAddress() const */
    bm_t* b = bm_find(c->r[0], 0);
    c->r[0] = b ? b->buf : 0;
}
void hle_CFbsBitmap_DisplayMode(ngage_cpu_t* c) {
    bm_t* b = bm_find(c->r[0], 0);
    c->r[0] = b ? (uint32_t)b->mode : 0;
}
void hle_CFbsBitmap_SizeInPixels(ngage_cpu_t* c) {  /* TSize by value: r0=result, r1=this */
    uint32_t res = c->r[0];
    bm_t* b = bm_find(c->r[1], 0);
    ngage_w32(c, res, b ? (uint32_t)b->w : 0);
    ngage_w32(c, res + 4, b ? (uint32_t)b->h : 0);
    c->r[0] = res;
}
void hle_CFbsBitmap_Header(ngage_cpu_t* c) {        /* SEpocBitmapHeader by value */
    uint32_t res = c->r[0];
    bm_t* b = bm_find(c->r[1], 0);
    int w = b ? b->w : 0, h = b ? b->h : 0, mode = b ? b->mode : NGAGE_DM_COLOR64K;
    int bw = ngage_fb_bytewidth(w, mode);
    ngage_w32(c, res +  0, (uint32_t)(bw * h));      /* iBitmapSize          */
    ngage_w32(c, res +  4, 0x20u);                   /* iStructSize          */
    ngage_w32(c, res +  8, (uint32_t)w);             /* iSizeInPixels.iWidth */
    ngage_w32(c, res + 12, (uint32_t)h);             /* iSizeInPixels.iHeight*/
    ngage_w32(c, res + 16, 0u); ngage_w32(c, res + 20, 0u);  /* twips */
    ngage_w32(c, res + 24, (uint32_t)(mode == NGAGE_DM_GRAY256 ? 8 : 16)); /* iBitsPerPixel */
    ngage_w32(c, res + 28, 0u);                      /* iColor / compression */
    c->r[0] = res;
}

/* CFbsBitmap::Load(...) (FBSCLI ordinal 156): set up `this` as a loaded bitmap so the
 * caller's DisplayMode()/SizeInPixels() return sane values and it skips its colour-convert
 * path. SonicN's bitmaps are EColor4K; size is a default until real MBM loading lands. */
void hle_CFbsBitmap_Load(ngage_cpu_t* c) {
    bm_t* b = bm_find(c->r[0], 1);
    if (!b) { c->r[0] = (uint32_t)-4; return; }
    b->w = 256; b->h = 256; b->mode = NGAGE_DM_COLOR4K;     /* TODO: read from the MBM */
    if (!b->buf) b->buf = ngage_alloc_zeroed(c, (uint32_t)ngage_fb_bytewidth(b->w, b->mode) * (uint32_t)b->h);
    c->r[0] = 0;                                            /* KErrNone */
}

/* ---- HLE C++ object with a synthetic no-op vtable ----
 * The game makes virtual calls on graphics objects (device, gc) and deletes them via the
 * vtable. Give each a vtable of sentinel slots that all dispatch to a no-op-success shim,
 * so `(*(*obj+N))(obj,…)` resolves instead of indexing a null vtable. */
#define HLE_OBJ_VT_BASE 0xF1000000u
static uint32_t g_obj_vtable;
void hle_obj_method(ngage_cpu_t* c) { c->r[0] = 0; }

static uint32_t obj_vtable(ngage_cpu_t* c) {
    if (!g_obj_vtable) {
        g_obj_vtable = ngage_alloc_zeroed(c, 64 * 4);     /* 64 slots (covers +0xc0) */
        for (int i = 0; i < 64; i++) {
            uint32_t m = HLE_OBJ_VT_BASE + (uint32_t)i;
            ngage_w32(c, g_obj_vtable + (uint32_t)i * 4, m);
            ngage_register(m, hle_obj_method);
        }
    }
    return g_obj_vtable;
}
static uint32_t hle_make_object(ngage_cpu_t* c, uint32_t size) {
    uint32_t o = ngage_alloc_zeroed(c, size);
    ngage_w32(c, o, obj_vtable(c));
    return o;
}

/* ---- BITGDI (thin: the game draws its own pixels) ---- */
/* CFbsBitmapDevice::NewL(...) (BITGDI ordinal 170) -> a device object */
void hle_CFbsBitmapDevice_NewL(ngage_cpu_t* c) { c->r[0] = hle_make_object(c, 64); }

void hle_CFbsDevice_CreateContext(ngage_cpu_t* c) { /* (CFbsBitGc*& aGc) -> KErrNone */
    ngage_w32(c, c->r[1], hle_make_object(c, 128));    /* a gc with a usable vtable */
    c->r[0] = 0;
}
void hle_BitGc_noop(ngage_cpu_t* c) { c->r[0] = 0; }  /* Activate/SetDitherOrigin/SetShadowMode/SetUserDisplayMode */

/* ---- NOKIAFC: full-screen flip (ordinal 1) ----
 * Exact signature is undocumented (lib not in the firmware dump). Provisional: present the
 * active render-target bitmap. Verify/refine against EKA2L1 once the game drives it. */
void hle_NOKIAFC_present(ngage_cpu_t* c) {
    /* The game renders into an off-screen CFbsBitmap and flips with NOKIAFC_1(). It doesn't
     * tell us which bitmap is the screen, so present the one with the most rendered content
     * (the active backbuffer). TODO: track the screen bitmap explicitly. */
    bm_t* best = 0; long bestnz = 0;
    for (int i = 0; i < 32; i++) {
        bm_t* b = &g_bm[i];
        if (!b->key || !b->buf || b->w <= 0 || b->h <= 0) continue;
        long nz = 0, n = (long)ngage_fb_bytewidth(b->w, b->mode) * b->h;
        for (long o = 0; o < n; o += 4) if (ngage_r32(c, b->buf + (uint32_t)o)) nz++;
        if (nz > bestnz) { bestnz = nz; best = b; }
    }
    if (best) ngage_present(c, best->buf, best->w, best->h, best->mode);
}

/* Capture the current screen: present the most-rendered bitmap. The game uses a persistent
 * framebuffer (it flips once at init), so call this periodically to snapshot what it's
 * drawing. Returns the count of non-zero pixels in the chosen bitmap. */
long ngage_fb_snapshot(ngage_cpu_t* c) {
    bm_t* best = 0; long bestnz = 0;
    for (int i = 0; i < 32; i++) {
        bm_t* b = &g_bm[i];
        if (!b->key || !b->buf || b->w <= 0 || b->h <= 0) continue;
        long nz = 0, n = (long)ngage_fb_bytewidth(b->w, b->mode) * b->h;
        for (long o = 0; o < n; o += 4) if (ngage_r32(c, b->buf + (uint32_t)o)) nz++;
        if (nz > bestnz) { bestnz = nz; best = b; }
    }
    if (best) ngage_present(c, best->buf, best->w, best->h, best->mode);
    return bestnz;
}
