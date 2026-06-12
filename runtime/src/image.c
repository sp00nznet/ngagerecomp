/*
 * Image loader — place the game's data segments into guest memory.
 *
 * Lifted code is native, but it reads the image's data (vtables, jump tables, const
 * pools, static objects) from guest addresses. ngage_load_image() copies the relocated
 * segment bytes (segments.bin, from gen_image.py) into the flat window so those reads
 * resolve. Returns the number of segments loaded, or -1 on error.
 */
#include <stdio.h>
#include <stdlib.h>
#include "ngage_cpu.h"
#include "ngage_runtime.h"

int ngage_load_image(ngage_cpu_t* c, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t nseg = 0;
    if (fread(&nseg, 4, 1, f) != 1) { fclose(f); return -1; }
    for (uint32_t i = 0; i < nseg; i++) {
        uint32_t start = 0, len = 0;
        if (fread(&start, 4, 1, f) != 1 || fread(&len, 4, 1, f) != 1) { fclose(f); return -1; }
        /* c->mem is base-biased, so c->mem + start is the host address for guest `start` */
        if (fread(c->mem + start, 1, len, f) != len) { fclose(f); return -1; }
    }
    fclose(f);
    return (int)nseg;
}
