/*
 * HLE shims for EFSRV.DLL — the file server (read path).
 *
 * Maps Symbian file ops to host files under a mounted root. Enough to load a game's
 * assets: RFs::Connect, RFile::Open/Read/Size/Seek/Close. Writes are supported too so
 * the surface is complete, but asset bring-up only needs reads.
 *
 * Handles: an RFile is identified by its guest `this` pointer (r0); we map that to a
 * host FILE*. Symbian errors: KErrNone=0, KErrNotFound=-1.
 */
#include <stdio.h>
#include <string.h>
#include "ngage_cpu.h"
#include "ngage_runtime.h"
#include "ngage_hle.h"

static char g_root[512] = ".";

void ngage_fs_mount(const char* host_root) {
    snprintf(g_root, sizeof g_root, "%s", host_root ? host_root : ".");
}

/* ---- open RFile handle table (keyed by guest `this`) ---- */
typedef struct { uint32_t key; FILE* fp; } fh_t;
static fh_t g_fh[64];

static FILE** fh_slot(uint32_t key, int alloc) {
    for (int i = 0; i < 64; i++) if (g_fh[i].key == key && g_fh[i].fp) return &g_fh[i].fp;
    if (!alloc) return 0;
    for (int i = 0; i < 64; i++) if (!g_fh[i].fp) { g_fh[i].key = key; return &g_fh[i].fp; }
    return 0;
}

/* Decode a guest TDesC16 filename to a host ASCII path under the mount root. */
static int read_fname(ngage_cpu_t* c, uint32_t desc, char* out, size_t outsz) {
    ngage_desc_t d = ngage_desc(c, desc);
    char name[260]; size_t n = d.len; if (n > sizeof name - 1) n = sizeof name - 1;
    for (size_t i = 0; i < n; i++) {
        uint16_t ch = ngage_r16(c, d.ptr + i * 2);
        name[i] = (ch && ch < 128) ? (char)ch : '_';
    }
    name[n] = 0;
    /* normalise: drop a leading drive "X:", turn '\' into '/' */
    char* p = name;
    if (p[0] && p[1] == ':') p += 2;
    for (char* q = p; *q; q++) if (*q == '\\') *q = '/';
    while (*p == '/') p++;
    snprintf(out, outsz, "%s/%s", g_root, p);
    return 1;
}

static FILE* open_path(ngage_cpu_t* c, uint32_t desc, const char* mode) {
    char path[600];
    read_fname(c, desc, path, sizeof path);
    FILE* fp = fopen(path, mode);
    if (!fp) {
        /* fallback: same basename under the app's private dir */
        const char* base = strrchr(path, '/'); base = base ? base + 1 : path;
        char alt[700];
        snprintf(alt, sizeof alt, "%s/system/apps/sonicn/%s", g_root, base);
        fp = fopen(alt, mode);
    }
    return fp;
}

/* ---- RFs ---- */
void hle_RFs_Connect(ngage_cpu_t* c)  { c->r[0] = 0; }              /* KErrNone */
void hle_RFsBase_Close(ngage_cpu_t* c) { (void)c; }
void hle_RFs_Delete(ngage_cpu_t* c)   { c->r[0] = 0; }
void hle_RFs_MkDir(ngage_cpu_t* c)    { c->r[0] = 0; }

/* ---- RFile ---- */
void hle_RFile_Open(ngage_cpu_t* c) {
    FILE* fp = open_path(c, c->r[2], "rb");
    if (!fp) { c->r[0] = (uint32_t)-1; return; }            /* KErrNotFound */
    FILE** s = fh_slot(c->r[0], 1); if (s) *s = fp; else { fclose(fp); c->r[0] = (uint32_t)-4; return; }
    c->r[0] = 0;
}
void hle_RFile_Create(ngage_cpu_t* c) {
    FILE* fp = open_path(c, c->r[2], "wb");
    if (!fp) { c->r[0] = (uint32_t)-1; return; }
    FILE** s = fh_slot(c->r[0], 1); if (s) *s = fp; else { fclose(fp); c->r[0] = (uint32_t)-4; return; }
    c->r[0] = 0;
}

static void read_into(ngage_cpu_t* c, uint32_t desc, uint32_t want) {
    FILE** s = fh_slot(c->r[0], 0);
    if (!s) { c->r[0] = (uint32_t)-1; return; }
    ngage_desc_t d = ngage_desc(c, desc);
    if (want > d.maxlen) want = d.maxlen;
    uint32_t got = 0;
    for (; got < want; got++) {
        int ch = fgetc(*s);
        if (ch == EOF) break;
        ngage_w8(c, d.ptr + got, (uint8_t)ch);
    }
    ngage_desc_setlen(c, desc, got);
    c->r[0] = 0;                                            /* KErrNone (EOF is not an error) */
}
void hle_RFile_Read(ngage_cpu_t* c) {                       /* Read(TDes8&) — up to MaxLength */
    ngage_desc_t d = ngage_desc(c, c->r[1]);
    read_into(c, c->r[1], d.maxlen);
}
void hle_RFile_ReadLen(ngage_cpu_t* c) {                    /* Read(TDes8&, TInt aLength) */
    read_into(c, c->r[1], c->r[2]);
}

void hle_RFile_Write(ngage_cpu_t* c) {                      /* Write(const TDesC8&, TInt) */
    FILE** s = fh_slot(c->r[0], 0);
    if (!s) { c->r[0] = (uint32_t)-1; return; }
    ngage_desc_t d = ngage_desc(c, c->r[1]);
    uint32_t n = c->r[2] ? c->r[2] : d.len;
    for (uint32_t i = 0; i < n; i++) fputc(ngage_r8(c, d.ptr + i), *s);
    c->r[0] = 0;
}

void hle_RFile_Size(ngage_cpu_t* c) {                       /* Size(TInt& aSize) */
    FILE** s = fh_slot(c->r[0], 0);
    if (!s) { c->r[0] = (uint32_t)-1; return; }
    long cur = ftell(*s); fseek(*s, 0, SEEK_END);
    long sz = ftell(*s); fseek(*s, cur, SEEK_SET);
    ngage_w32(c, c->r[1], (uint32_t)sz);
    c->r[0] = 0;
}
void hle_RFile_Seek(ngage_cpu_t* c) {                       /* Seek(TSeek aMode, TInt& aPos) */
    FILE** s = fh_slot(c->r[0], 0);
    if (!s) { c->r[0] = (uint32_t)-1; return; }
    int32_t pos = (int32_t)ngage_r32(c, c->r[2]);
    int whence = (c->r[1] == 0) ? SEEK_SET : (c->r[1] == 1) ? SEEK_CUR : SEEK_END;
    fseek(*s, pos, whence);
    ngage_w32(c, c->r[2], (uint32_t)ftell(*s));
    c->r[0] = 0;
}
void hle_RFile_SetSize(ngage_cpu_t* c) { c->r[0] = 0; }
void hle_RFile_Flush(ngage_cpu_t* c)   { FILE** s = fh_slot(c->r[0], 0); if (s) fflush(*s); c->r[0] = 0; }

/* RHandleBase::Close() — base of RFile/RFs handles; close a matching open file if any. */
void hle_RHandleBase_Close(ngage_cpu_t* c) {
    FILE** s = fh_slot(c->r[0], 0);
    if (s && *s) { fclose(*s); *s = 0; }
}
