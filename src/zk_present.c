/* SPDX-License-Identifier: MIT */
#include "zk_present.h"

#include <stdlib.h>
#include <string.h>

#include <cybergraphx/cybergraphics.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/cybergraphics.h>
#include <proto/graphics.h>

struct Region *zk_present_region(const unsigned char *argb, int w, int h)
{
    struct Region *r = NewRegion();
    int x, y;
    if (!r) return NULL;
    for (y = 0; y < h; y++) {
        const unsigned char *row = argb + (size_t)y * (size_t)w * 4;
        for (x = 0; x < w;) {
            struct Rectangle rect;
            int e;
            if (row[x * 4] < 128) { x++; continue; }
            e = x;
            while (e < w && row[e * 4] >= 128) e++;
            rect.MinX = (WORD)x; rect.MinY = (WORD)y; rect.MaxX = (WORD)(e - 1); rect.MaxY = (WORD)y;
            if (!OrRectRegion(r, &rect)) { DisposeRegion(r); return NULL; }
            x = e;
        }
    }
    return r;
}

int zk_present_put(struct Window *window, const unsigned char *argb, int w, int h,
                   const unsigned char *under)
{
    unsigned char *flat;
    size_t n = (size_t)w * (size_t)h, i;
    ULONG got;
    if (!window || w <= 0 || h <= 0) return 0;
    if (zk_present_composed(window))
        return WritePixelArray((APTR)argb, 0, 0, (UWORD)(w * 4), window->RPort, 0, 0,
                               (UWORD)w, (UWORD)h, RECTFMT_ARGB) == (ULONG)n;
    if (!(flat = malloc(n * 4))) return 0;
    for (i = 0; i < n; i++) {
        const unsigned char *s = argb + i * 4;
        unsigned char *d = flat + i * 4;
        int a = s[0], c;
        d[0] = 255;
        for (c = 1; c < 4; c++) {
            int below = under ? under[i * 4 + (size_t)c] : 0;
            d[c] = (unsigned char)((s[c] * a + below * (255 - a) + 127) / 255);
        }
    }
    got = WritePixelArray(flat, 0, 0, (UWORD)(w * 4), window->RPort, 0, 0, (UWORD)w, (UWORD)h, RECTFMT_ARGB);
    free(flat);
    return got == (ULONG)n;
}

int zk_glass_capture(struct Screen *screen, int x, int y, int w, int h, unsigned char *argb)
{
    size_t n = (size_t)w * (size_t)h, i;
    if (!screen || w <= 0 || h <= 0) return 0;
    if (x < 0 || y < 0 || x + w > screen->Width || y + h > screen->Height) return 0;
    if (ReadPixelArray(argb, 0, 0, (UWORD)(w * 4), &screen->RastPort, (UWORD)x, (UWORD)y,
                       (UWORD)w, (UWORD)h, RECTFMT_ARGB) != (ULONG)n) return 0;
    for (i = 0; i < n; i++) argb[i * 4] = 255;
    return 1;
}
