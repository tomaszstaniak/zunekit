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

    /* measure: the text's own widths; a decomposed é is as wide as the
       composed one (the shaper puts the mark over its letter); missing
       glyphs take a box; a joiner draws nothing */
    OK(zt_measure(f, "", 0, 0) == 0);
    w = zt_measure(f, E_COMPOSED, 2, 0);
    wd = zt_measure(f, E_DECOMPOSED, 3, 0);
    OK(w > 0 && wd >= w - 1 && wd <= w + 1);
    OK(zt_measure(f, POLISH, strlen(POLISH), 0) > 0);
    OK(zt_measure(f, NON_BMP, 4, 0) == (16 + 1) / 2);
    OK(zt_measure(f, "\xe2\x80\x8d", 3, 0) == 0);

    /* fit, hit and x come from the same shaping as measure and draw */
    {
        const char *s = "a" E_DECOMPOSED "b";
        long wa = zt_x(f, s, 4 + 1, 1, 0), wae = zt_x(f, s, 5, 4, 0);
        n = strlen(s);
        OK(zt_x(f, s, n, 0, 0) == 0 && zt_x(f, s, n, n, 0) == zt_measure(f, s, n, 0));
        OK(zt_x(f, s, n, 2, 0) == zt_x(f, s, n, 1, 0));              /* inside a cluster: its start */
        OK(zt_fit(f, s, n, 0, 0) == 0 && zt_fit(f, s, n, 10000, 0) == n);
        OK(zt_fit(f, s, n, zt_measure(f, s, 1, 0), 0) == 1 && zt_fit(f, s, n, zt_measure(f, s, 4, 0), 0) == 4);
        OK(zt_hit(f, s, n, 0, 0) == 0 && zt_hit(f, s, n, wa + 1, 0) == 1 && zt_hit(f, s, n, wae - 1, 0) == 4);
        OK(zt_hit(f, s, n, 10000, 0) == n);
    }

    /* the review's reproducer (2026-11-07): kerning inside "AV" counts for
       measure, fit and hit alike */
    {
        const char *one[1];
        zt_font *k;
        long av, a, vv;
        one[0] = vera;
        OK((k = zt_font_open(one, 1, 40)) != NULL);
        av = zt_measure(k, "AV", 2, 0);
        a = zt_measure(k, "A", 1, 0);
        vv = zt_measure(k, "V", 1, 0);
        OK(av < a + vv);                                              /* Vera kerns AV */
        OK(zt_fit(k, "AV", 2, av, 0) == 2 && zt_fit(k, "AV", 2, av - 1, 0) == 1);
        OK(zt_x(k, "AV", 2, 1, 0) < a && zt_hit(k, "AV", 2, av - 1, 0) == 2);
        /* bold: wider by the same amount measured and drawn */
        {
            unsigned char *c1 = (unsigned char *)calloc(400 * 60, 1), *c2 = (unsigned char *)calloc(400 * 60, 1);
            long wb = zt_measure(k, "AVA", 3, ZT_BOLD), wn = zt_measure(k, "AVA", 3, 0), x, right1 = 0, right2 = 0;
            int y;
            OK(c1 && c2 && wb > wn);
            OK(zt_draw(k, "AVA", 3, ZT_BOLD, c1, 400, 60, 400, 10, 45) == wb);
            OK(zt_draw(k, "AVA", 3, 0, c2, 400, 60, 400, 10, 45) == wn);
            for (y = 0; y < 60; y++)
                for (x = 0; x < 400; x++) {
                    if (c1[y * 400 + x] > 64 && x > right1) right1 = x;
                    if (c2[y * 400 + x] > 64 && x > right2) right2 = x;
                }
            /* the ink ends near its advance in both: bold ink is wider by
               about what bold adds to the width */
            OK(right1 - 10 <= wb + 2 && right2 - 10 <= wn + 2 && right1 - right2 >= (wb - wn) / 2);
            free(c1); free(c2);
        }
        zt_font_close(k);
    }

    /* draw: coverage where the text is, nothing past its advance; the box
       for a missing glyph is drawn; counters follow */
    cov = (unsigned char *)calloc(200 * 40, 1);
    OK(cov != NULL);
    w = zt_draw(f, "Hi" CYRIL NON_BMP, strlen("Hi" CYRIL NON_BMP), 0, cov, 200, 40, 200, 2, 30);
    OK(w == zt_measure(f, "Hi" CYRIL NON_BMP, strlen("Hi" CYRIL NON_BMP), 0));
    any = 0;
    for (i = 0; i < 200 * 40; i++) if (cov[i]) { any = 1; break; }
    OK(any);
    for (i = 0; i < 40; i++) OK(!cov[i * 200 + 2 + w + 2]);
    {
        unsigned long sh;
        zt_font_counts(f, &fb, &miss, &sh);
        OK(fb >= 1 && miss >= 1 && sh >= 1);
    }
    /* the decomposed é's accent is drawn over its letter, inside its width */
    memset(cov, 0, 200 * 40);
    w = zt_draw(f, E_DECOMPOSED, 3, 0, cov, 200, 40, 200, 2, 30);
    {
        int top = 40, x, y;
        for (y = 0; y < 40; y++) for (x = 0; x < 200; x++) if (cov[y * 200 + x] > 64 && y < top) top = y;
        for (y = 0; y < 40; y++) for (x = 2 + (int)w + 2; x < 200; x++) OK(!cov[y * 200 + x]);
        OK(top < 30 - zt_font_ascent(f) / 2);                        /* something above x-height: the accent */
    }
    /* drawing outside the buffer is clipped, not a crash */
    zt_draw(f, "W", 1, 0, cov, 200, 40, 200, 195, 50);
    zt_draw(f, "W", 1, 0, cov, 200, 40, 200, -30, -5);
    free(cov);

    /* a family emoji is one cluster even with no face for it: one box */
    OK(zt_measure(f, FAMILY, strlen(FAMILY), 0) == (16 + 1) / 2);

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

/* One long paragraph: walking its clusters, finding cluster starts from
   a known boundary and word moves stay linear (no rescans from the
   paragraph's start for each step). Timed loosely: 1 MB must take well
   under a second on the host. */
#include <time.h>
static void scaling(void)
{
    size_t n = 1u << 20, i, b, count = 0;
    char *s = (char *)malloc(n);
    zt_iter it;
    clock_t t0;
    double sec;
    OK(s != NULL);
    for (i = 0; i < n; i++) s[i] = "ab cd\xc5"[i % 6] == '\xc5' ? 'e' : "ab cd\xc5"[i % 6];
    t0 = clock();
    zt_iter_init(&it, s, n, 0);
    while ((b = zt_iter_next(&it)) < n) count++;
    OK(count + 1 == n);
    for (i = 0, b = 0; i < 2000; i++) b = zt_cluster_start_from(s, n, n - 4096, n - 4096 + (i % 4096));
    for (i = 0, b = 0; b < 200000; i++) b = zt_next_word(s, n, b);
    sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("scaling: 1 MB paragraph walked, 2000 starts, %lu word moves in %.3f s\n", (unsigned long)i, sec);
    OK(sec < 2.0);
    free(s);
}


/* Windows-1250 for the bytes the tests use; ISO-8859-1 elsewhere. In 1250,
   0x8c is Ś and 0x9c ś: printable letters on bytes ISO-8859 keeps for C1
   controls, so a filter on bytes would lose them. 0x81 is empty there. */
static size_t cp1250(void *u, unsigned char c, char *out)
{
    unsigned long cp;
    (void)u;
    switch (c) {
    case 0x8c: cp = 0x15a; break;
    case 0x9c: cp = 0x15b; break;
    case 0xb9: cp = 0x105; break;
    case 0x81: return 0;
    default: cp = c; break;
    }
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    out[0] = (char)(0xc0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3f));
    return 2;
}
static size_t latin1(void *u, unsigned char c, char *out)
{
    (void)u;
    if (c < 0x80) { out[0] = (char)c; return 1; }
    out[0] = (char)(0xc0 | (c >> 6)); out[1] = (char)(0x80 | (c & 0x3f));
    return 2;
}

static void keys(void)
{
    char out[32];
    const unsigned char sw[] = { 0x8c, 0x9c, 0xb9 }, ctl[] = { 0x1b, 'a', 0x7f, 0x85, 0x9b, 0x0d, 'b' };
    size_t k = zt_keys_decode(sw, sizeof sw, cp1250, NULL, out, sizeof out);
    /* Ś ś ą: kept, decoded from 1250 */
    OK(k == 6 && !memcmp(out, "\xc5\x9a\xc5\x9b\xc4\x85", 6));
    /* the same bytes in Latin-1 are C1 controls: left out, after decoding */
    k = zt_keys_decode(sw, 2, latin1, NULL, out, sizeof out);
    OK(k == 0);
    /* ESC, DEL, U+0085, CSI (0x9b), CR: controls; a and b stay */
    k = zt_keys_decode(ctl, sizeof ctl, latin1, NULL, out, sizeof out);
    OK(k == 2 && !memcmp(out, "ab", 2));
    /* a byte the set leaves empty gives nothing, not a guess */
    { const unsigned char e[] = { 0x81, 'x' }; k = zt_keys_decode(e, 2, cp1250, NULL, out, sizeof out); OK(k == 1 && out[0] == 'x'); }
    /* never past cap, never half a character */
    k = zt_keys_decode(sw, sizeof sw, cp1250, NULL, out, 3);
    OK(k == 2);
}

static void folding(void)
{
    size_t n;
    char *f;
    /* Polish: case only */
    OK(zt_contains_folded(POLISH, strlen(POLISH), "ZA\xc5\xbb\xc3\x93\xc5\x81\xc4\x86", 10) == 1);  /* ZAŻÓŁĆ */
    /* Greek final sigma, capital and small */
    OK(zt_contains_folded("\xce\xbb\xcf\x8c\xce\xb3\xce\xbf\xcf\x82", 10, "\xce\x9b\xce\x8c\xce\x93\xce\x9f\xce\xa3", 10) == 1);
    /* Cyrillic */
    OK(zt_contains_folded(CYRIL, strlen(CYRIL), "\xd0\xb6\xd0\x98\xd0\x97", 6) == 1);              /* жИЗ */
    /* ß and SS; the Kelvin sign and k */
    OK(zt_contains_folded("Stra\xc3\x9f" "e", 7, "STRASSE", 7) == 1);
    OK(zt_contains_folded("5 \xe2\x84\xaa", 5, "5 k", 3) == 1);
    /* composed and decomposed é are one letter */
    OK(zt_contains_folded("caf" E_COMPOSED, 5, "CAF" E_DECOMPOSED, 6) == 1);
    /* not a match: different letters; and no match inside a character */
    OK(zt_contains_folded(POLISH, strlen(POLISH), "zazolc", 6) == 0);
    OK(zt_contains_folded("\xc4\x85", 2, "\x85", 1) <= 0);
    /* empty needle matches; the hay is not changed */
    OK(zt_contains_folded(POLISH, strlen(POLISH), "", 0) == 1);
    f = zt_fold("\xc3\x89T\xc3\x89", 5, &n);
    OK(f && n == 5 && !memcmp(f, "\xc3\xa9t\xc3\xa9", 5));
    free(f);
    OK(!zt_fold("\xff", 1, &n));
}

int main(int argc, char **argv)
{
    otag();
    scaling();
    text();
    keys();
    folding();
    fonts(argc > 1 ? argv[1] : "tests/fixtures/fonts");
    printf("zk_text: %d checks passed\n", checks);
    return 0;
}
