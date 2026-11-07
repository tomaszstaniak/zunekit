/* SPDX-License-Identifier: MIT */
/* zk_text: see zk_text.h. */
#include "zk_text.h"
#include "utf8proc.h"

#ifdef __AROS__
#include <proto/freetype2.h>
#else
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ text */

int zt_valid(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        utf8proc_int32_t cp;
        utf8proc_ssize_t k = utf8proc_iterate((const utf8proc_uint8_t *)s + i, (utf8proc_ssize_t)(n - i), &cp);
        if (k <= 0) return 0;
        i += (size_t)k;
    }
    return 1;
}

unsigned long zt_decode(const char *s, size_t n, size_t off, size_t *len)
{
    utf8proc_int32_t cp;
    utf8proc_ssize_t k;
    if (off >= n) { *len = 0; return 0; }
    k = utf8proc_iterate((const utf8proc_uint8_t *)s + off, (utf8proc_ssize_t)(n - off), &cp);
    if (k <= 0) { *len = 1; return 0xFFFDUL; }
    *len = (size_t)k;
    return (unsigned long)cp;
}

/* Where a run of clusters can start from scratch: after a line feed (a
   boundary in every case, GB4/GB5), or the start. */
static size_t para_start(const char *s, size_t off)
{
    while (off > 0 && s[off - 1] != '\n') off--;
    return off;
}

/* Walks the clusters of s from `from` (a boundary) and calls back with each
   boundary until it returns nonzero; returns that boundary, or n. */
static size_t walk(const char *s, size_t n, size_t from, int (*stop)(size_t at, void *u), void *u)
{
    utf8proc_int32_t prev = -1, state = 0;
    size_t i = from;
    if (stop(i, u)) return i;
    while (i < n) {
        size_t len;
        utf8proc_int32_t cp = (utf8proc_int32_t)zt_decode(s, n, i, &len);
        if (prev >= 0 && utf8proc_grapheme_break_stateful(prev, cp, &state)) {
            if (stop(i, u)) return i;
        }
        prev = cp;
        i += len;
    }
    return n;
}

struct after { size_t off; };
static int stop_after(size_t at, void *u) { return at > ((struct after *)u)->off; }

size_t zt_next_cluster(const char *s, size_t n, size_t off)
{
    struct after a;
    if (off >= n) return n;
    a.off = off;
    return walk(s, n, para_start(s, off), stop_after, &a);
}

struct before { size_t off, last; };
static int stop_before(size_t at, void *u)
{
    struct before *b = (struct before *)u;
    if (at >= b->off) return 1;
    b->last = at;
    return 0;
}

size_t zt_prev_cluster(const char *s, size_t n, size_t off)
{
    struct before b;
    if (off == 0) return 0;
    if (off > n) off = n;
    /* the boundary before a line feed's own start is the previous paragraph's */
    b.off = off;
    b.last = 0;
    walk(s, n, para_start(s, off - 1), stop_before, &b);
    return b.last;
}

struct atb { size_t off, last; };
static int stop_at(size_t at, void *u)
{
    struct atb *b = (struct atb *)u;
    if (at > b->off) return 1;
    b->last = at;
    return 0;
}

size_t zt_cluster_start(const char *s, size_t n, size_t off)
{
    struct atb b;
    if (off >= n) return n;
    b.off = off;
    b.last = 0;
    walk(s, n, para_start(s, off), stop_at, &b);
    return b.last;
}

static int count_cb(size_t at, void *u) { (void)at; (*(size_t *)u)++; return 0; }

size_t zt_clusters(const char *s, size_t n)
{
    size_t c = 0, i = 0;
    /* walk() counts the start too; paragraphs restart at line feeds, which
       are boundaries anyway, so one walk over the whole text is right */
    if (!n) return 0;
    walk(s, n, i, count_cb, &c);
    return c;              /* boundaries before each cluster */
}

static int word_cp(unsigned long cp)
{
    utf8proc_category_t c = utf8proc_category((utf8proc_int32_t)cp);
    switch (c) {
    case UTF8PROC_CATEGORY_LU: case UTF8PROC_CATEGORY_LL: case UTF8PROC_CATEGORY_LT:
    case UTF8PROC_CATEGORY_LM: case UTF8PROC_CATEGORY_LO:
    case UTF8PROC_CATEGORY_MN: case UTF8PROC_CATEGORY_MC: case UTF8PROC_CATEGORY_ME:
    case UTF8PROC_CATEGORY_ND: case UTF8PROC_CATEGORY_NL: case UTF8PROC_CATEGORY_NO:
    case UTF8PROC_CATEGORY_PC:
        return 1;
    default:
        return 0;
    }
}

/* The cluster at off is part of a word: its first code point is. */
static int word_at(const char *s, size_t n, size_t off)
{
    size_t len;
    return off < n && word_cp(zt_decode(s, n, off, &len));
}

size_t zt_next_word(const char *s, size_t n, size_t off)
{
    off = zt_cluster_start(s, n, off);
    while (off < n && word_at(s, n, off)) off = zt_next_cluster(s, n, off);
    while (off < n && !word_at(s, n, off)) off = zt_next_cluster(s, n, off);
    return off;
}

size_t zt_prev_word(const char *s, size_t n, size_t off)
{
    if (off > n) off = n;
    off = zt_cluster_start(s, n, off);
    while (off > 0) {
        size_t p = zt_prev_cluster(s, n, off);
        if (word_at(s, n, p)) break;
        off = p;
    }
    while (off > 0) {
        size_t p = zt_prev_cluster(s, n, off);
        if (!word_at(s, n, p)) break;
        off = p;
    }
    return off;
}

/* --------------------------------------------------------- outline fonts */

#define OT_SPEC1_FONTFILE 0x80009101UL   /* OT_Level1 | OT_Indirect | 0x101 */

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

int zt_otag_file(const unsigned char *otag, size_t n, char *out, size_t outn)
{
    size_t i;
    for (i = 0; i + 8 <= n; i += 8) {
        unsigned long tag = be32(otag + i), data = be32(otag + i + 4);
        size_t k;
        if (!tag) break;                          /* TAG_DONE */
        if (tag != OT_SPEC1_FONTFILE) continue;
        if (data >= n) return 0;
        for (k = 0; data + k < n && otag[data + k]; k++) ;
        if (data + k >= n || !k || k + 1 > outn) return 0;
        memcpy(out, otag + data, k);
        out[k] = 0;
        return 1;
    }
    return 0;
}

/* ----------------------------------------------------------------- fonts */

#define ADV_CACHE 1024

struct adv { unsigned long cp; int face; unsigned glyph; long adv; int used; };

struct zt_font {
    FT_Library lib;
    FT_Face face[ZT_MAX_FACES];
    int nfaces, px;
    struct adv cache[ADV_CACHE];
    unsigned long fallback, missing;
};

zt_font *zt_font_open(const char *const *paths, int count, int px)
{
    zt_font *f;
    int i;
    if (px < 1 || count < 1) return NULL;
    if (!(f = (zt_font *)calloc(1, sizeof *f))) return NULL;
    if (FT_Init_FreeType(&f->lib)) { free(f); return NULL; }
    f->px = px;
    for (i = 0; i < count && f->nfaces < ZT_MAX_FACES; i++) {
        FT_Face face;
        if (!paths[i] || FT_New_Face(f->lib, paths[i], 0, &face)) continue;
        if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px)) { FT_Done_Face(face); continue; }
        f->face[f->nfaces++] = face;
    }
    if (!f->nfaces) { FT_Done_FreeType(f->lib); free(f); return NULL; }
    return f;
}

void zt_font_close(zt_font *f)
{
    int i;
    if (!f) return;
    for (i = 0; i < f->nfaces; i++) FT_Done_Face(f->face[i]);
    FT_Done_FreeType(f->lib);
    free(f);
}

int zt_font_faces(const zt_font *f) { return f ? f->nfaces : 0; }

static int round26(long v) { return (int)((v + 32) >> 6); }

int zt_font_ascent(const zt_font *f) { return round26(f->face[0]->size->metrics.ascender); }
int zt_font_descent(const zt_font *f) { return -round26(f->face[0]->size->metrics.descender); }
int zt_font_height(const zt_font *f)
{
    int h = round26(f->face[0]->size->metrics.height);
    int a = zt_font_ascent(f) + zt_font_descent(f);
    return h > a ? h : a;
}

/* Code points drawn as nothing: controls and the zero-width format
   characters joiners and variation selectors are made of. */
static int invisible(unsigned long cp)
{
    return cp < 0x20 || cp == 0x7f || cp == 0x200b || cp == 0x200c || cp == 0x200d || cp == 0x2060
        || cp == 0xfeff || (cp >= 0xfe00 && cp <= 0xfe0f) || (cp >= 0xe0100 && cp <= 0xe01ef);
}

/* The face, glyph and advance for cp; face -1 is the missing-glyph box. */
static const struct adv *lookup(zt_font *f, unsigned long cp)
{
    struct adv *a = &f->cache[(cp * 2654435761UL >> 7) % ADV_CACHE];
    int i;
    if (a->used && a->cp == cp) return a;
    a->used = 1;
    a->cp = cp;
    a->face = -1;
    a->glyph = 0;
    a->adv = invisible(cp) ? 0 : (f->px + 1) / 2;
    if (invisible(cp)) { a->face = -2; return a; }
    for (i = 0; i < f->nfaces; i++) {
        FT_UInt g = FT_Get_Char_Index(f->face[i], (FT_ULong)cp);
        if (!g) continue;
        if (FT_Load_Glyph(f->face[i], g, FT_LOAD_DEFAULT)) continue;
        a->face = i;
        a->glyph = g;
        a->adv = round26(f->face[i]->glyph->advance.x);
        break;
    }
    return a;
}

int zt_face_for(zt_font *f, unsigned long cp)
{
    const struct adv *a = lookup(f, cp);
    return a->face >= 0 ? a->face : -1;
}

static void count(zt_font *f, const struct adv *a)
{
    if (a->face > 0) f->fallback++;
    else if (a->face == -1) f->missing++;
}

/* Kerning between two glyphs of one face, in pixels. */
static long kern(zt_font *f, const struct adv *a, const struct adv *b)
{
    FT_Vector v;
    if (!a || a->face < 0 || a->face != b->face || !FT_HAS_KERNING(f->face[a->face])) return 0;
    if (FT_Get_Kerning(f->face[a->face], a->glyph, b->glyph, FT_KERNING_DEFAULT, &v)) return 0;
    return round26(v.x);
}

long zt_measure(zt_font *f, const char *s, size_t n)
{
    size_t i = 0;
    long w = 0;
    struct adv prev;
    int have = 0;
    while (i < n) {
        size_t len;
        unsigned long cp = zt_decode(s, n, i, &len);
        const struct adv *a = lookup(f, cp);
        if (have) w += kern(f, &prev, a);
        w += a->adv;
        count(f, a);
        prev = *a;
        have = 1;
        i += len;
    }
    return w;
}

size_t zt_fit(zt_font *f, const char *s, size_t n, long width)
{
    size_t at = 0;
    long x = 0;
    while (at < n) {
        size_t e = zt_next_cluster(s, n, at);
        long cw = zt_measure(f, s + at, e - at);
        if (x + cw > width) break;
        x += cw;
        at = e;
    }
    return at;
}

size_t zt_hit(zt_font *f, const char *s, size_t n, long x)
{
    size_t at = 0;
    long pos = 0;
    if (x <= 0) return 0;
    while (at < n) {
        size_t e = zt_next_cluster(s, n, at);
        long cw = zt_measure(f, s + at, e - at);
        if (x < pos + cw) return x - pos >= pos + cw - x ? e : at;
        pos += cw;
        at = e;
    }
    return n;
}

static void put(unsigned char *cov, int w, int h, int stride, long x, long y, unsigned v)
{
    unsigned char *p;
    if (x < 0 || y < 0 || x >= w || y >= h || !v) return;
    p = cov + (size_t)y * (size_t)stride + (size_t)x;
    *p = (unsigned char)(*p + v > 255 ? 255 : *p + v);
}

/* The missing-glyph box: an outline inside the advance, cap high. */
static void box(zt_font *f, unsigned char *cov, int w, int h, int stride, long x, long y, long adv)
{
    long top = y - (zt_font_ascent(f) * 3) / 4, x0 = x + 1, x1 = x + adv - 2, yy, xx;
    if (x1 <= x0) x1 = x0 + 1;
    for (xx = x0; xx <= x1; xx++) { put(cov, w, h, stride, xx, top, 255); put(cov, w, h, stride, xx, y - 1, 255); }
    for (yy = top; yy < y; yy++) { put(cov, w, h, stride, x0, yy, 255); put(cov, w, h, stride, x1, yy, 255); }
}

long zt_draw(zt_font *f, const char *s, size_t n, unsigned char *cov, int w, int h, int stride,
             long x, long y)
{
    size_t i = 0;
    long pen = x;
    struct adv prev;
    int have = 0;
    while (i < n) {
        size_t len;
        unsigned long cp = zt_decode(s, n, i, &len);
        const struct adv *a = lookup(f, cp);
        struct adv cur = *a;
        if (have) pen += kern(f, &prev, &cur);
        count(f, &cur);
        if (cur.face >= 0) {
            FT_Face face = f->face[cur.face];
            if (!FT_Load_Glyph(face, cur.glyph, FT_LOAD_RENDER)) {
                FT_Bitmap *b = &face->glyph->bitmap;
                long gx = pen + face->glyph->bitmap_left, gy = y - face->glyph->bitmap_top;
                unsigned r, c;
                for (r = 0; r < b->rows; r++) {
                    const unsigned char *row = b->pitch >= 0 ? b->buffer + (size_t)r * (size_t)b->pitch
                                                             : b->buffer + (size_t)(b->rows - 1 - r) * (size_t)(-b->pitch);
                    for (c = 0; c < b->width; c++) {
                        unsigned v = b->pixel_mode == FT_PIXEL_MODE_MONO
                                   ? ((row[c >> 3] >> (7 - (c & 7))) & 1) * 255 : row[c];
                        put(cov, w, h, stride, gx + (long)c, gy + (long)r, v);
                    }
                }
            }
        } else if (cur.face == -1) {
            box(f, cov, w, h, stride, pen, y, cur.adv);
        }
        pen += cur.adv;
        prev = cur;
        have = 1;
        i += len;
    }
    return pen - x;
}

void zt_font_counts(const zt_font *f, unsigned long *fallback, unsigned long *missing)
{
    *fallback = f->fallback;
    *missing = f->missing;
}
