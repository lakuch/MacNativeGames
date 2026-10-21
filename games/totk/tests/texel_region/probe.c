/* CPU only: ReadTexels / WriteTexels by region (nvn_bl_region) against the whole-level path they replace (decode the
   level, copy the region, encode the level again). For every case both are run on the same random storage and must
   give the same bytes: the region read equals the region cut from the decoded level, and the storage after a region
   write equals the storage after decode + patch + encode. Formats with 1-16 bytes per block (incl. BC), 2D with mip
   levels, 2D arrays, cube maps, 3D with depth tiling, regions at edges, odd sizes and single blocks. Negative control
   (TOTK_PROBE_NEGATIVE=1): the region read is offset by one 16-byte segment and must then differ. */
#include "nvn_tex.h"
#include "nvn_deswizzle.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach_time.h>

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 16); }
static void fill(uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (uint8_t)rnd(); }
static uint32_t mip(uint32_t s, unsigned l) { s >>= l; return s ? s : 1; }
static int g_checks, g_fail, g_negative;

/* One region of one level / layer (3D: z0 .. z0+nz of the level's slices): returns 0 when both paths agree. */
static int one(const nvn_texdesc *d, unsigned level, unsigned layer, uint32_t bx, uint32_t by, uint32_t bz, uint32_t bwn, uint32_t bhn, uint32_t nz) {
    int bw, bh, bpb = nvn_fmt_bpb(d->format, &bw, &bh);
    nvn_texlayout L; nvn_tex_layout(d, &L);
    int is3d = d->target == 2;
    uint32_t lw = mip(d->width, level), lh = mip(d->height, level), lz = is3d ? mip(d->depth, level) : 1;
    size_t lp = (size_t)((lw + bw - 1) / bw) * bpb, lr = (lh + bh - 1) / bh;
    size_t wb = (size_t)bwn * bpb, row = wb + (rnd() & 1) * 16, img = row * bhn + (rnd() & 1) * 64;   // padded strides too
    size_t x0 = (size_t)bx * bpb;
    uint8_t *stor = malloc(L.size), *ref = malloc(L.size), *lin = malloc(lp * lr * lz), *a = calloc(1, img * nz), *b = calloc(1, img * nz), *src = malloc(img * nz);
    if (!stor || !ref || !lin || !a || !b || !src) { printf("out of memory\n"); exit(2); }
    fill(stor, L.size); fill(src, img * nz);
    int bad = 0;
    /* read: whole level, then cut */
    if (nvn_deswizzle(d, level, layer, stor, lin, lp)) { printf("  deswizzle refused the case\n"); bad = 1; goto out; }
    for (uint32_t z = 0; z < nz; z++) for (uint32_t y = 0; y < bhn; y++)
        memcpy(a + z * img + y * row, lin + (size_t)(bz + z) * lp * lr + (size_t)(by + y) * lp + x0, wb);
    uint32_t rx = g_negative ? (uint32_t)x0 + 16 : (uint32_t)x0;
    if (g_negative && rx + wb > lp) rx = (uint32_t)x0 >= 16 ? (uint32_t)x0 - 16 : (uint32_t)x0;
    if (nvn_bl_region(d, level, layer, stor, rx, by, bz, (uint32_t)wb, bhn, nz, b, row, img, 1, NULL, NULL)) { printf("  region read refused the case\n"); bad = 1; goto out; }
    for (uint32_t z = 0; z < nz; z++) for (uint32_t y = 0; y < bhn; y++) if (memcmp(a + z * img + y * row, b + z * img + y * row, wb)) { bad = 1; break; }
    if (bad) goto out;
    /* write: whole level decode + patch + encode, against the region write on a copy of the same storage */
    memcpy(ref, stor, L.size);
    for (uint32_t z = 0; z < nz; z++) for (uint32_t y = 0; y < bhn; y++)
        memcpy(lin + (size_t)(bz + z) * lp * lr + (size_t)(by + y) * lp + x0, src + z * img + y * row, wb);
    if (nvn_swizzle(d, level, layer, lin, lp, ref)) { printf("  swizzle refused the case\n"); bad = 1; goto out; }
    if (nvn_bl_region(d, level, layer, stor, (uint32_t)x0, by, bz, (uint32_t)wb, bhn, nz, src, row, img, 0, NULL, NULL)) { printf("  region write refused the case\n"); bad = 1; goto out; }
    if (memcmp(stor, ref, L.size)) bad = 2;
out:
    free(stor); free(ref); free(lin); free(a); free(b); free(src);
    return bad;
}

typedef struct { const char *what; int target, format; uint32_t w, h, depth, levels; } Tex;
static const Tex texs[] = {
    {"RGBA8 2D 256x256, 9 levels",            1, 0x25, 256, 256, 1, 9},
    {"RGBA8 2D 2048x2048 (the 16 MB kind)",   1, 0x25, 2048, 2048, 1, 1},
    {"R8 2D 333x77, 4 levels (odd size)",     1, 0x01, 333, 77, 1, 4},
    {"RG8 2D 260x260, 9 levels",              1, 0x0d, 260, 260, 1, 9},
    {"RGBA16F 2D 640x360, 3 levels",          1, 0x2c, 640, 360, 1, 3},
    {"RGBA32F 2D 96x80, 2 levels",            1, 0x2e, 96, 80, 1, 2},
    {"BC1 2D 512x512, 10 levels",             1, 0x42, 512, 512, 1, 10},
    {"BC3 sRGB 2D 160x160, 8 levels",         1, 0x48, 160, 160, 1, 8},
    {"BC4 2D 1024x1024, 3 levels",            1, 0x49, 1024, 1024, 1, 3},
    {"RGBA8 2D array 128x128x6, 4 levels",    4, 0x25, 128, 128, 6, 4},
    {"BC1 2D array 256x256x3, 2 levels",      4, 0x42, 256, 256, 3, 2},
    {"RGBA8 cube 64x64, 3 levels",            8, 0x25, 64, 64, 1, 3},
    {"RGBA8 3D 64x64x32, 3 levels",           2, 0x25, 64, 64, 32, 3},
    {"R8 3D 40x24x17, 2 levels",              2, 0x01, 40, 24, 17, 2},
};

int main(void) {
    g_negative = getenv("TOTK_PROBE_NEGATIVE") != NULL;
    for (size_t i = 0; i < sizeof texs / sizeof texs[0]; i++) {
        const Tex *T = &texs[i];
        nvn_texdesc d; memset(&d, 0, sizeof d);
        d.target = T->target; d.format = T->format; d.width = T->w; d.height = T->h; d.depth = T->depth; d.levels = T->levels;
        int bw, bh, bpb = nvn_fmt_bpb(d.format, &bw, &bh);
        if (!bpb) { printf("FAIL %s: format %#x unknown to nvn_fmt_bpb\n", T->what, d.format); g_fail++; continue; }
        nvn_texlayout L; nvn_tex_layout(&d, &L);
        int fails = 0, cases = 0;
        for (unsigned level = 0; level < T->levels; level++) {
            uint32_t cw = (mip(T->w, level) + bw - 1) / bw, ch = (mip(T->h, level) + bh - 1) / bh, cz = T->target == 2 ? mip(T->depth, level) : 1;
            unsigned layers = T->target == 2 ? 1 : L.layers;
            for (int k = 0; k < 24; k++) {
                uint32_t bwn, bhn, bx, by, nz = 1, bz = 0;
                switch (k) {
                case 0: bx = 0; by = 0; bwn = cw; bhn = ch; break;                          // the whole level
                case 1: bx = 0; by = 0; bwn = 1; bhn = 1; break;                            // one block
                case 2: bx = cw - 1; by = ch - 1; bwn = 1; bhn = 1; break;                  // last block
                case 3: bx = 0; by = ch - 1; bwn = cw; bhn = 1; break;                      // last block row
                default: bwn = 1 + rnd() % cw; bhn = 1 + rnd() % ch; bx = rnd() % (cw - bwn + 1); by = rnd() % (ch - bhn + 1); break;
                }
                if (T->target == 2) { nz = 1 + rnd() % cz; bz = rnd() % (cz - nz + 1); if (k == 0) { nz = cz; bz = 0; } }
                unsigned layer = layers > 1 ? rnd() % layers : 0;
                int r = one(&d, level, layer, bx, by, bz, bwn, bhn, nz);
                cases++; g_checks++;
                if (r) { if (fails++ < 3) printf("  %s lv%u layer %u region (%u,%u,%u) %ux%ux%u blocks: %s differs\n", T->what, level, layer, bx, by, bz, bwn, bhn, nz, r == 2 ? "write" : "read"); }
            }
        }
        if (fails) g_fail++;
        printf("%-42s %4d cases  %s\n", T->what, cases, fails ? "FAIL" : "ok");
    }
    /* timing: a small region of the 2048x2048 texture, the old way (decode the level) and by region */
    {
        nvn_texdesc d; memset(&d, 0, sizeof d); d.target = 1; d.format = 0x25; d.width = d.height = 2048; d.depth = 1; d.levels = 1;
        nvn_texlayout L; nvn_tex_layout(&d, &L);
        uint8_t *stor = malloc(L.size), *lin = malloc((size_t)2048 * 4 * 2048), out[64 * 64 * 4]; fill(stor, L.size);
        mach_timebase_info_data_t tb; mach_timebase_info(&tb);
        uint64_t t = mach_absolute_time();
        for (int i = 0; i < 20; i++) { nvn_deswizzle(&d, 0, 0, stor, lin, 2048 * 4); for (int y = 0; y < 64; y++) memcpy(out + y * 256, lin + (size_t)(512 + y) * 8192 + 1024, 256); }
        double whole = (mach_absolute_time() - t) * tb.numer / tb.denom / 1e6 / 20;
        t = mach_absolute_time();
        for (int i = 0; i < 20; i++) nvn_bl_region(&d, 0, 0, stor, 1024, 512, 0, 256, 64, 1, out, 256, 256 * 64, 1, NULL, NULL);
        double region = (mach_absolute_time() - t) * tb.numer / tb.denom / 1e6 / 20;
        printf("timing: 64x64 RGBA8 region of a 2048x2048 texture: whole level %.3f ms, by region %.4f ms\n", whole, region);
        free(stor); free(lin);
    }
    if (g_negative) {
        printf("texel region probe (negative control): %s\n", g_fail ? "differences detected as expected" : "FAILED: the shifted read was not detected");
        return g_fail ? 3 : 1;
    }
    printf("texel region probe: %d cases, %s\n", g_checks, g_fail ? "FAILED" : "all match");
    return g_fail ? 1 : 0;
}
