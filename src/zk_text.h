/* SPDX-License-Identifier: MIT */
/* zk_text: UTF-8 text for Zune classes, without the system's 8-bit set.
 *
 * Two parts, usable apart:
 *  - text: validity, grapheme cluster and word boundaries over UTF-8 bytes
 *    (utf8proc). Offsets are bytes; every boundary the functions return is
 *    a cluster boundary, so an editor that only moves through them never
 *    splits a letter from its combining mark or a sequence of an emoji.
 *  - fonts: a set of TrueType/OpenType faces at one pixel size, the first
 *    the text's own and the rest fallbacks in order, measured and drawn
 *    through FreeType (the system's freetype2.library on AROS). A code point
 *    no face has is drawn as a visible box; the text itself never changes.
 *
 * Not done here: shaping (ligatures, mark positioning, complex scripts),
 * right-to-left text, input methods, colour glyphs. Combining marks are
 * drawn at the advance FreeType gives them (often zero). */
#ifndef ZK_TEXT_H
#define ZK_TEXT_H

#include <stddef.h>

/* --- text --------------------------------------------------------------- */

/* 1 when s[0..n) is valid UTF-8 (no overlongs, surrogates or code points
   past U+10FFFF). */
int zt_valid(const char *s, size_t n);

/* The code point at s[off] and its length in *len (1 for a byte that does
   not start a valid sequence, returned as U+FFFD). */
unsigned long zt_decode(const char *s, size_t n, size_t off, size_t *len);

/* The next cluster boundary after off (n at the end), the previous one
   before off (0 at the start). off itself need not be a boundary. */
size_t zt_next_cluster(const char *s, size_t n, size_t off);
size_t zt_prev_cluster(const char *s, size_t n, size_t off);
/* The boundary at or before off: where a caret at byte off belongs. */
size_t zt_cluster_start(const char *s, size_t n, size_t off);
/* Clusters in s[0..n). */
size_t zt_clusters(const char *s, size_t n);

/* Word moves as editors make them: to the start of the next word, to the
   start of this or the previous word. A word is a run of letters, marks
   and digits (Unicode categories L, M, N) and connector punctuation. */
size_t zt_next_word(const char *s, size_t n, size_t off);
size_t zt_prev_word(const char *s, size_t n, size_t off);

/* --- AROS outline fonts ------------------------------------------------ */

/* The font file an AROS .otag names (OT_Spec1_FontFile, as the system's
   FreeType engine reads it), from the .otag's bytes: 1 and the path in out,
   0 when it names none or out is too small. A Zune font "Vera Sans.font"
   has its tags in "Vera Sans.otag" beside it in FONTS:. */
int zt_otag_file(const unsigned char *otag, size_t n, char *out, size_t outn);

/* --- fonts -------------------------------------------------------------- */

typedef struct zt_font zt_font;

#define ZT_MAX_FACES 8

/* Opens the faces in paths[0..count) at px pixels (the em). Faces that
   cannot be opened are left out; NULL when none opens. On AROS, FreeType
   comes from freetype2.library, which the caller opens (FreeType2Base). */
zt_font *zt_font_open(const char *const *paths, int count, int px);
void zt_font_close(zt_font *f);
/* Faces that opened, in their order. */
int zt_font_faces(const zt_font *f);

/* Line metrics of the first face in pixels: ascent above the baseline,
   descent below it, the distance between baselines. */
int zt_font_ascent(const zt_font *f);
int zt_font_descent(const zt_font *f);
int zt_font_height(const zt_font *f);

/* Which face draws code point cp: 0.. in the order given, -1 none (a box). */
int zt_face_for(zt_font *f, unsigned long cp);

/* Width in pixels of s[0..n), advances rounded per glyph, kerning within a
   face. Invalid bytes measure as U+FFFD. */
long zt_measure(zt_font *f, const char *s, size_t n);

/* Bytes of s[0..n) that fit in width pixels, ending on a cluster boundary;
   0 only when not even the first cluster fits. */
size_t zt_fit(zt_font *f, const char *s, size_t n, long width);

/* The caret boundary nearest to x pixels from the start of s[0..n). */
size_t zt_hit(zt_font *f, const char *s, size_t n, long x);

/* Draws s[0..n) as 8-bit coverage (0 none, 255 full) into cov (w x h,
   stride bytes a row), the pen starting at x with the baseline at y.
   Coverage adds up and saturates, so text can be drawn over earlier text.
   Returns the advance in pixels. */
long zt_draw(zt_font *f, const char *s, size_t n, unsigned char *cov, int w, int h, int stride,
             long x, long y);

/* Counters since the font was opened: glyphs drawn or measured from a
   fallback face, and code points no face has. */
void zt_font_counts(const zt_font *f, unsigned long *fallback, unsigned long *missing);

#endif
