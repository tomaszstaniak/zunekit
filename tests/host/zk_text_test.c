/* SPDX-License-Identifier: MIT */
/* zk_text on the host: UTF-8, clusters and words; fonts through FreeType
 * with the fixture faces (Bitstream Vera Sans, Open Sans as its fallback).
 *   scripts/test-host.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zk_text.h"

static int checks;
#define OK(x) do { checks++; if (!(x)) { printf("FAILED line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

/* The review's text set (2026-11-07). */
#define POLISH   "Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 g\xc4\x99\xc5\x9bl\xc4\x85 ja\xc5\xba\xc5\x84"
#define GREEK    "\xce\xb1\xce\xb2\xce\xb3"                         /* αβγ */
#define CYRIL    "\xd0\x96\xd0\xb8\xd0\xb7\xd0\xbd\xd1\x8c"         /* Жизнь */
#define MATHS    "\xe2\x88\x80x \xe2\x88\x88 \xe2\x84\x9d"          /* ∀x ∈ ℝ */
#define E_COMPOSED   "\xc3\xa9"                                    /* é, U+00E9 */
#define E_DECOMPOSED "e\xcc\x81"                                   /* e + U+0301 */
#define NON_BMP  "\xf0\x9d\x84\x9e"                                 /* U+1D11E 𝄞 */
#define FAMILY   "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7"     /* 👩‍👧: one cluster */
#define FLAGS    "\xf0\x9f\x87\xb5\xf0\x9f\x87\xb1\xf0\x9f\x87\xa9\xf0\x9f\x87\xaa" /* 🇵🇱🇩🇪 */

static void text(void)
{
    const char *s;
    size_t n, len;

    OK(zt_valid(POLISH, strlen(POLISH)) && zt_valid(NON_BMP, 4) && zt_valid("", 0));
    OK(!zt_valid("\xc3", 1) && !zt_valid("\xc0\xaf", 2) && !zt_valid("\xed\xa0\x80", 3)
       && !zt_valid("\xf4\x90\x80\x80", 4) && !zt_valid("a\xff", 2));
    OK(zt_decode(NON_BMP, 4, 0, &len) == 0x1D11EUL && len == 4);
    OK(zt_decode("\xff" "a", 2, 0, &len) == 0xFFFDUL && len == 1);

    /* a letter and its combining mark are one cluster, composed or not */
    s = "a" E_DECOMPOSED "b";
    n = strlen(s);
    OK(zt_next_cluster(s, n, 0) == 1 && zt_next_cluster(s, n, 1) == 4 && zt_next_cluster(s, n, 4) == 5);
    OK(zt_prev_cluster(s, n, 4) == 1 && zt_prev_cluster(s, n, 3) == 1 && zt_cluster_start(s, n, 2) == 1);
    OK(zt_clusters(s, n) == 3 && zt_clusters(E_COMPOSED, 2) == 1 && zt_clusters(E_DECOMPOSED, 3) == 1);
    /* outside the BMP; a joined emoji; two flags */
    OK(zt_clusters(NON_BMP, 4) == 1 && zt_next_cluster(NON_BMP, 4, 1) == 4);
    OK(zt_clusters(FAMILY, strlen(FAMILY)) == 1);
    OK(zt_clusters(FLAGS, strlen(FLAGS)) == 2 && zt_next_cluster(FLAGS, 16, 0) == 8 && zt_prev_cluster(FLAGS, 16, 16) == 8);
    OK(zt_clusters(POLISH, strlen(POLISH)) == 17);
    /* a line feed is its own boundary, across paragraphs */
    s = "ab\ncd";
    OK(zt_next_cluster(s, 5, 1) == 2 && zt_next_cluster(s, 5, 2) == 3 && zt_prev_cluster(s, 5, 3) == 2);
    OK(zt_prev_cluster(s, 5, 0) == 0 && zt_next_cluster(s, 5, 5) == 5);

    /* words over letters of any script, marks and digits */
    s = POLISH ", " GREEK " 42.";
    n = strlen(s);
    OK(zt_next_word(s, n, 0) == strlen("Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 "));
    OK(zt_next_word(s, n, strlen(POLISH)) == strlen(POLISH ", "));
    OK(zt_prev_word(s, n, n) == strlen(POLISH ", " GREEK " "));
    OK(zt_prev_word(s, n, strlen(POLISH ", ")) == strlen("Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 g\xc4\x99\xc5\x9bl\xc4\x85 "));
    s = "x" E_DECOMPOSED "y z";
    OK(zt_next_word(s, strlen(s), 0) == strlen("x" E_DECOMPOSED "y "));
}

static void fonts(const char *dir)
{
    char vera[512], open[512];
    const char *both[2], *missing_first[2];
    zt_font *f, *v;
    unsigned long fb, miss;
    unsigned char *cov;
    long w, wd;
    size_t n;
    int i, any;

    snprintf(vera, sizeof vera, "%s/VeraSans.ttf", dir);
    snprintf(open, sizeof open, "%s/OpenSans-Regular.ttf", dir);
    both[0] = vera; both[1] = open;
    missing_first[0] = "/nonexistent.ttf"; missing_first[1] = vera;

    OK(!zt_font_open(both, 0, 16) && !zt_font_open(both, 2, 0));
    OK((v = zt_font_open(missing_first, 2, 16)) && zt_font_faces(v) == 1);   /* a face that fails is left out */
    OK((f = zt_font_open(both, 2, 16)) && zt_font_faces(f) == 2);
    OK(zt_font_ascent(f) > 0 && zt_font_descent(f) > 0 && zt_font_height(f) >= zt_font_ascent(f) + zt_font_descent(f));

    /* Vera has Latin-1 only: Polish letters and Cyrillic come from Open
       Sans; a musical symbol from neither */
    OK(zt_face_for(f, 'A') == 0 && zt_face_for(f, 0x00F3) == 0);
    OK(zt_face_for(f, 0x017C) == 1 && zt_face_for(v, 0x017C) == -1);
    OK(zt_face_for(f, 0x0416) == 1 && zt_face_for(v, 0x0416) == -1);
    OK(zt_face_for(f, 0x1D11E) == -1);

    /* measure: the text's own widths; a decomposed é is as wide as its
       base letter (the mark advances 0); missing glyphs take a box */
    OK(zt_measure(f, "", 0) == 0);
    w = zt_measure(f, "e", 1);
    wd = zt_measure(f, E_DECOMPOSED, 3);
    OK(w > 0 && wd >= w && wd <= w + 1);
    OK(zt_measure(f, POLISH, strlen(POLISH)) > 0);
    OK(zt_measure(f, NON_BMP, 4) == (16 + 1) / 2);
    OK(zt_measure(f, "\xe2\x80\x8d", 3) == 0);                           /* a joiner draws nothing */

    /* fit ends on a cluster boundary; hit finds the nearer boundary */
    {
        const char *s = "a" E_DECOMPOSED "b";
        long wa = zt_measure(f, "a", 1), we = zt_measure(f, E_DECOMPOSED, 3);
        n = strlen(s);
        OK(zt_fit(f, s, n, wa) == 1 && zt_fit(f, s, n, wa + we - 1) == 1 && zt_fit(f, s, n, wa + we) == 4);
        OK(zt_fit(f, s, n, 0) == 0 && zt_fit(f, s, n, 10000) == n);
        OK(zt_hit(f, s, n, 0) == 0 && zt_hit(f, s, n, wa + 1) == 1 && zt_hit(f, s, n, wa + we - 1) == 4);
        OK(zt_hit(f, s, n, 10000) == n);
    }

    /* draw: coverage where the text is, nothing past its advance; the box
       for a missing glyph is drawn; counters follow */
    cov = (unsigned char *)calloc(200 * 40, 1);
    OK(cov != NULL);
    w = zt_draw(f, "Hi" CYRIL NON_BMP, strlen("Hi" CYRIL NON_BMP), cov, 200, 40, 200, 2, 30);
    OK(w == zt_measure(f, "Hi" CYRIL NON_BMP, strlen("Hi" CYRIL NON_BMP)));
    any = 0;
    for (i = 0; i < 200 * 40; i++) if (cov[i]) { any = 1; break; }
    OK(any);
    for (i = 0; i < 40; i++) OK(!cov[i * 200 + 2 + w + 2]);
    zt_font_counts(f, &fb, &miss);
    OK(fb >= 5 && miss >= 1);
    /* drawing outside the buffer is clipped, not a crash */
    zt_draw(f, "W", 1, cov, 200, 40, 200, 195, 50);
    zt_draw(f, "W", 1, cov, 200, 40, 200, -30, -5);
    free(cov);

    zt_font_close(f);
    zt_font_close(v);
}

/* An .otag as the system's font manager writes it: big-endian tag pairs,
   indirect strings at the offsets they give. */
static void otag(void)
{
    unsigned char b[96];
    char out[64];
    const char *file = "Fonts:TrueType/VeraSans.ttf", *engine = "freetype2";
    memset(b, 0, sizeof b);
    /* OT_FileIdent, then OT_Engine (indirect), then OT_Spec1_FontFile */
    b[0] = 0x80; b[2] = 0x10; b[3] = 0x01; b[7] = 96;
    b[8] = 0x80; b[10] = 0x90; b[11] = 0x02; b[15] = 32;
    b[16] = 0x80; b[18] = 0x91; b[19] = 0x01; b[23] = 48;
    memcpy(b + 32, engine, strlen(engine) + 1);
    memcpy(b + 48, file, strlen(file) + 1);
    OK(zt_otag_file(b, sizeof b, out, sizeof out) && !strcmp(out, file));
    OK(!zt_otag_file(b, sizeof b, out, 10));              /* too small for it */
    OK(!zt_otag_file(b, 40, out, sizeof out));            /* its string cut off */
    b[16] = 0;                                            /* TAG_DONE before it */
    OK(!zt_otag_file(b, sizeof b, out, sizeof out));
    OK(!zt_otag_file(b, 4, out, sizeof out));
}

int main(int argc, char **argv)
{
    otag();
    text();
    fonts(argc > 1 ? argv[1] : "tests/fixtures/fonts");
    printf("zk_text: %d checks passed\n", checks);
    return 0;
}
