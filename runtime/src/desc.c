/*
 * Symbian descriptor decoding.
 *
 * TDesC/TDes are Symbian's length-prefixed string/buffer types. The first word packs
 * the descriptor type (top 4 bits) and length (low 28). Resolving the data pointer and
 * max length depends on the type — see the table in ngage_runtime.h.
 */
#include "ngage_cpu.h"
#include "ngage_runtime.h"

#define DES_TYPE(w)  ((w) >> 28)
#define DES_LEN(w)   ((w) & 0x0fffffffu)

ngage_desc_t ngage_desc(ngage_cpu_t* c, uint32_t addr) {
    uint32_t w = ngage_r32(c, addr);
    ngage_desc_t d;
    d.len = DES_LEN(w);
    switch (DES_TYPE(w)) {
        case 0: /* EBufC  */ d.ptr = addr + 4;              d.maxlen = d.len; break;
        case 1: /* EPtrC  */ d.ptr = ngage_r32(c, addr + 4); d.maxlen = d.len; break;
        case 2: /* EPtr   */ d.maxlen = ngage_r32(c, addr + 4); d.ptr = ngage_r32(c, addr + 8); break;
        case 3: /* EBuf   */ d.maxlen = ngage_r32(c, addr + 4); d.ptr = addr + 8; break;
        case 4: /* EBufCPtr */ d.maxlen = ngage_r32(c, addr + 4); d.ptr = ngage_r32(c, addr + 8); break;
        default:             d.ptr = addr + 4;              d.maxlen = d.len; break;
    }
    return d;
}

void ngage_desc_setlen(ngage_cpu_t* c, uint32_t addr, uint32_t len) {
    uint32_t w = ngage_r32(c, addr);
    ngage_w32(c, addr, (w & 0xf0000000u) | (len & 0x0fffffffu));
}
