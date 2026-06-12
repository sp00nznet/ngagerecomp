/*
 * Framebuffer presentation.
 *
 * N-Gage games software-render into a bitmap; we convert that bitmap to RGB888 and push
 * it to the host. Output here is a PPM per frame (zero-dependency, lets you *see* pixels);
 * a live SDL2 window can replace ngage_present_host() without touching the conversion.
 *
 * Scanlines in a CFbsBitmap are DWORD-aligned. Display modes handled: EColor64K (RGB565),
 * EColor4K (0x0RGB 4-4-4), EGray256, EColor16MU (xRGB8888).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ngage_cpu.h"
#include "ngage_runtime.h"

static char g_dir[256] = ".";
static int  g_frame = 0;
static uint8_t* g_rgb = 0;          /* latest frame, RGB888, host memory */
static int g_w = 0, g_h = 0;

void ngage_fb_init(const char* out_dir) {
    snprintf(g_dir, sizeof g_dir, "%s", out_dir ? out_dir : ".");
}

static int storage_bpp(int mode) {
    switch (mode) {
        case NGAGE_DM_GRAY256:
        case NGAGE_DM_COLOR256:  return 8;
        case NGAGE_DM_COLOR4K:
        case NGAGE_DM_COLOR64K:  return 16;
        case NGAGE_DM_COLOR16M:  return 24;
        case NGAGE_DM_COLOR16MU: return 32;
        default:                 return 16;
    }
}

int ngage_fb_bytewidth(int width, int mode) {
    int bits = width * storage_bpp(mode);
    return ((bits + 31) / 32) * 4;     /* DWORD-aligned scanline */
}

static void px_to_rgb(uint32_t v, int mode, uint8_t* rgb) {
    switch (mode) {
        case NGAGE_DM_COLOR64K:        /* RGB565 */
            rgb[0] = (uint8_t)(((v >> 11) & 0x1f) * 255 / 31);
            rgb[1] = (uint8_t)(((v >> 5)  & 0x3f) * 255 / 63);
            rgb[2] = (uint8_t)((v & 0x1f) * 255 / 31);
            break;
        case NGAGE_DM_COLOR4K:         /* 0x0RGB, 4 bits each */
            rgb[0] = (uint8_t)(((v >> 8) & 0xf) * 17);
            rgb[1] = (uint8_t)(((v >> 4) & 0xf) * 17);
            rgb[2] = (uint8_t)((v & 0xf) * 17);
            break;
        case NGAGE_DM_GRAY256:
            rgb[0] = rgb[1] = rgb[2] = (uint8_t)(v & 0xff);
            break;
        case NGAGE_DM_COLOR16M:
        case NGAGE_DM_COLOR16MU:       /* xRGB / RGB */
            rgb[0] = (uint8_t)((v >> 16) & 0xff);
            rgb[1] = (uint8_t)((v >> 8) & 0xff);
            rgb[2] = (uint8_t)(v & 0xff);
            break;
        default:
            rgb[0] = rgb[1] = rgb[2] = 0;
    }
}

const uint8_t* ngage_fb_rgb(int* w, int* h) { if (w) *w = g_w; if (h) *h = g_h; return g_rgb; }

void ngage_present(ngage_cpu_t* c, uint32_t pixels, int w, int h, int mode) {
    if (w <= 0 || h <= 0) return;
    int bw = ngage_fb_bytewidth(w, mode);
    int bpp = storage_bpp(mode);
    if (g_w != w || g_h != h) { free(g_rgb); g_rgb = (uint8_t*)malloc((size_t)w * h * 3); g_w = w; g_h = h; }

    for (int y = 0; y < h; y++) {
        uint32_t row = pixels + (uint32_t)y * bw;
        for (int x = 0; x < w; x++) {
            uint32_t v;
            if (bpp == 8)       v = ngage_r8(c, row + x);
            else if (bpp == 16) v = ngage_r16(c, row + x * 2);
            else                v = ngage_r32(c, row + x * 4);
            px_to_rgb(v, mode, g_rgb + ((size_t)y * w + x) * 3);
        }
    }

    /* write a PPM so the frame is viewable with anything */
    char path[320];
    snprintf(path, sizeof path, "%s/frame_%04d.ppm", g_dir, g_frame++);
    FILE* f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        fwrite(g_rgb, 1, (size_t)w * h * 3, f);
        fclose(f);
    }
}
