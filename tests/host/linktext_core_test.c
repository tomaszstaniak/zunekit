/* SPDX-License-Identifier: MIT */
/* Linktext's logic on the host, with a font of 8 pixels a character (9 bold).
 *   cc -Wall -Wextra -fsanitize=address,undefined -Isrc \
 *      tests/host/linktext_core_test.c src/linktext_core.c -o build/lt-test */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linktext_core.h"

static long fail_after = -1, live = 0;
void *lt_alloc(size_t n)
{
    if (fail_after == 0) return NULL;
    if (fail_after > 0) fail_after--;
    live++;
    return malloc(n);
}
void lt_free(void *p) { if (p) { live--; free(p); } }

static long font(void *ctx, const char *s, lt_ulong n, lt_ulong fl)
{
    (void)ctx; (void)s;
    return (long)n * (fl & LT_BOLD ? 9 : 8);
}

static int checks;
#define OK(x) do { checks++; if (!(x)) { printf("FAILED line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static lt_content *mk(const char *t, const lt_span_in *s, lt_ulong n)
{
    int st = -1;
    lt_content *c = lt_content_new(t, (lt_ulong)-1, s, n, &st);
    OK(c && st == LT_OK);
    return c;
}

static void validation(void)
{
    int st;
    lt_span_in s[2];
    memset(s, 0, sizeof s);
    /* Past the end, empty, wrap-around. */
    s[0].start = 3; s[0].length = 3; s[0].target = "x";
    OK(!lt_content_new("abcd", 4, s, 1, &st) && st == LT_RANGE);
    s[0].length = 0;
    OK(!lt_content_new("abcd", 4, s, 1, &st) && st == LT_RANGE);
    s[0].start = 2; s[0].length = (lt_ulong)-1;
    OK(!lt_content_new("abcd", 4, s, 1, &st) && st == LT_RANGE);
    s[0].start = (lt_ulong)-1; s[0].length = 2;
    OK(!lt_content_new("abcd", 4, s, 1, &st) && st == LT_RANGE);
    /* Overlap and order. */
    s[0].start = 0; s[0].length = 2; s[1].start = 1; s[1].length = 2;
    OK(!lt_content_new("abcd", 4, s, 2, &st) && st == LT_ORDER);
    s[0].start = 2; s[1].start = 0;
    OK(!lt_content_new("abcd", 4, s, 2, &st) && st == LT_ORDER);
    /* Empty target; NULL spans; size limits. */
    s[0].start = 0; s[0].length = 1; s[0].target = "";
    OK(!lt_content_new("abcd", 4, s, 1, &st) && st == LT_TARGET);
    OK(!lt_content_new("abcd", 4, NULL, 1, &st) && st == LT_RANGE);
    OK(!lt_content_new("abcd", 64UL * 1024 * 1024 + 1, NULL, 0, &st) && st == LT_TOOLARGE);
    OK(!lt_content_new("abcd", 4, s, 1000001, &st) && st == LT_TOOLARGE);
    OK(live == 0);
}

static void exact_targets(void)
{
    static const char *ends[] = {
        "https://e.org/?a=1&b=%2F!", "https://e.org/x;", "https://e.org/x?", "https://e.org/x'", "https://e.org/x.", NULL };
    lt_span_in s[5];
    char big[5001];
    lt_content *c;
    int i;
    memset(s, 0, sizeof s);
    for (i = 0; ends[i]; i++) { s[i].start = (lt_ulong)i * 2; s[i].length = 1; s[i].target = ends[i]; s[i].id = (lt_ulong)i + 7; }
    c = mk("a b c d e", s, 5);
    OK(c->nlinks == 5);
    for (i = 0; ends[i]; i++) {
        OK(!strcmp(lt_link(c, (lt_ulong)i)->target, ends[i]));
        OK(lt_link(c, (lt_ulong)i)->id == (lt_ulong)i + 7);
    }
    lt_content_unref(c);
    /* Longer than the old 1024-byte limit, copied, not referenced. */
    memset(big, 'x', 5000); big[5000] = 0;
    memcpy(big, "https://", 8);
    s[0].start = 0; s[0].length = 1; s[0].target = big;
    c = mk("a", s, 1);
    big[20] = 'y';
    OK(strlen(lt_link(c, 0)->target) == 5000 && lt_link(c, 0)->target[20] == 'x');
    lt_content_unref(c);
    OK(live == 0);
}

static void control_chars(void)
{
    lt_content *c = mk("a\tb\rc\nd", NULL, 0);
    OK(!strcmp(c->text, "a b c\nd"));
    lt_content_unref(c);
}

static void wrapping(void)
{
    lt_line *l = NULL; lt_ulong n = 0;
    lt_content *c = mk("aaa bbb ccc\n\nlast", NULL, 0);
    /* 10 characters wide. */
    OK(lt_layout(c, 80, font, NULL, &l, &n));
    OK(n == 4);
    OK(l[0].start == 0 && l[0].shown == 7 && l[0].len == 8);
    OK(l[1].start == 8 && l[1].shown == 3);
    OK(l[2].len == 0 && l[3].start == 13);
    lt_free(l);
    lt_content_unref(c);

    /* Word ending exactly at the edge. */
    c = mk("aaaa bbbb cccc", NULL, 0);
    OK(lt_layout(c, 32, font, NULL, &l, &n));
    OK(n == 3 && l[0].shown == 4 && l[1].start == 5 && l[1].shown == 4);
    lt_free(l);
    lt_content_unref(c);

    /* A word over several lines keeps every byte exactly once. */
    c = mk("see https://example.org/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/bbbbbbbbbbbbbbbbbbbbbbbbbbb?x=1 end", NULL, 0);
    OK(lt_layout(c, 160, font, NULL, &l, &n));
    {
        lt_ulong i, at = 0;
        OK(n >= 4);
        for (i = 0; i < n; i++) {
            OK(l[i].start == at);
            OK(l[i].shown * 8 <= 160);
            OK(l[i].len >= 1);
            at += l[i].len;
        }
        OK(at == c->len);
        /* It starts on the first line rather than leaving a gap. */
        OK(l[0].shown > 4);
    }
    lt_free(l);
    lt_content_unref(c);

    /* Width below a character: one per line, no endless loop. */
    c = mk("abc", NULL, 0);
    OK(lt_layout(c, 3, font, NULL, &l, &n) && n == 3);
    lt_free(l);
    OK(lt_layout(c, 0, font, NULL, &l, &n) && n == 3);
    lt_free(l);
    lt_content_unref(c);

    c = mk("", NULL, 0);
    OK(lt_layout(c, 80, font, NULL, &l, &n) && n == 1 && l[0].len == 0);
    lt_free(l);
    lt_content_unref(c);
    OK(live == 0);
}

static void bold_widths(void)
{
    lt_span_in s[1];
    lt_line *l = NULL; lt_ulong n = 0;
    lt_content *c;
    memset(s, 0, sizeof s);
    s[0].start = 0; s[0].length = 6; s[0].flags = LT_BOLD;
    c = mk("Notes: rest", s, 1);
    OK(lt_width(c, 0, 11, font, NULL) == 6 * 9 + 5 * 8);
    OK(lt_width(c, 4, 4, font, NULL) == 2 * 9 + 2 * 8);
    /* Bold 54 + " " 8 = 62 fits 64, next char does not fit 70? 70 >= 62+8 */
    OK(lt_layout(c, 64, font, NULL, &l, &n));
    OK(l[0].start == 0);
    lt_free(l);
    lt_content_unref(c);
}

static void hits_on_every_fragment(void)
{
    /* One link over several lines, one repeated target after it. */
    static const char t[] = "words https://example.org/aaaaaaaaaaaaaaaaaaaaaaaaaaaaa/bbbbbbbbbbbbbbbbbbbbbb then https://example.org/z end";
    const char *long_u = strstr(t, "https");
    const char *second = strstr(long_u + 1, "https");
    lt_span_in s[2];
    lt_content *c;
    lt_line *l = NULL; lt_ulong n = 0, line, f, caret;
    long x0, x1;
    memset(s, 0, sizeof s);
    s[0].start = (lt_ulong)(long_u - t); s[0].length = (lt_ulong)(strchr(long_u, ' ') - long_u);
    s[0].target = "https://target/full!"; s[0].id = 1;
    s[1].start = (lt_ulong)(second - t); s[1].length = 21; s[1].target = "https://target/full!"; s[1].id = 2;
    c = mk(t, s, 2);
    OK(lt_layout(c, 160, font, NULL, &l, &n));
    for (f = 0; lt_link_fragment(c, l, n, 0, f, font, NULL, &line, &x0, &x1); f++) {
        long off = lt_hit(c, &l[line], (x0 + x1) / 2, font, NULL, &caret);
        OK(off >= 0 && lt_link_at(c, (lt_ulong)off) == 0);
        off = lt_hit(c, &l[line], x0, font, NULL, &caret);
        OK(lt_link_at(c, (lt_ulong)off) == 0);
        off = lt_hit(c, &l[line], x1 - 1, font, NULL, &caret);
        OK(lt_link_at(c, (lt_ulong)off) == 0);
    }
    OK(f >= 3);
    /* Just left of the link: the space. */
    OK(lt_hit(c, &l[0], 47, font, NULL, &caret) == 5 && lt_link_at(c, 5) == -1);
    /* Past the end of a line: nothing, although the caret goes there. */
    OK(lt_hit(c, &l[n - 1], 400, font, NULL, &caret) == -1);
    OK(caret == l[n - 1].start + l[n - 1].shown);
    /* The second link. */
    OK(lt_link_fragment(c, l, n, 1, 0, font, NULL, &line, &x0, &x1));
    OK(lt_link_at(c, (lt_ulong)lt_hit(c, &l[line], x0 + 1, font, NULL, &caret)) == 1);
    OK(lt_link_fragment(c, l, n, 2, 0, font, NULL, &line, &x0, &x1) == 0);
    lt_free(l);
    lt_content_unref(c);
    OK(live == 0);
}

static void many_links(void)
{
    enum { N = 40 };
    char text[N * 3 + 1];
    lt_span_in s[N];
    lt_content *c;
    long i, cur;
    memset(text, ' ', sizeof text); text[N * 3] = 0;
    memset(s, 0, sizeof s);
    for (i = 0; i < N; i++) {
        text[i * 3] = 'x';
        s[i].start = (lt_ulong)i * 3; s[i].length = 1; s[i].target = i % 2 ? "https://a" : "https://b"; s[i].id = (lt_ulong)i;
    }
    c = mk(text, s, N);
    OK(c->nlinks == N);
    for (i = 0; i < N; i++) OK(lt_link_at(c, (lt_ulong)i * 3) == i);
    OK(lt_link_at(c, 1) == -1);
    for (cur = -1, i = 0; i < N; i++) { cur = lt_link_step(c, cur, 1); OK(cur == i); }
    OK(lt_link_step(c, cur, 1) == -1);
    OK(lt_link_step(c, -1, -1) == N - 1);
    OK(lt_link_step(c, 0, -1) == -1);
    lt_content_unref(c);
}

static void style_and_links_mixed(void)
{
    lt_span_in s[3];
    lt_content *c;
    lt_line l[1];
    lt_ulong fl = 0, r, p;
    memset(s, 0, sizeof s);
    s[0].start = 0; s[0].length = 5; s[0].flags = LT_BOLD;
    s[1].start = 6; s[1].length = 4; s[1].target = "https://t"; s[1].flags = LT_BOLD;
    c = mk("Hello link text", s, 2);
    OK(c->nlinks == 1);
    OK(lt_link_at(c, 0) == -1 && lt_link_at(c, 6) == 0);
    l[0].start = 0; l[0].len = 15; l[0].shown = 15;
    /* Selection 3..8 splits both spans. */
    p = 0;
    r = lt_run(c, l, p, 3, 8, &fl); OK(r == 3 && fl == LT_BOLD); p += r;
    r = lt_run(c, l, p, 3, 8, &fl); OK(r == 2 && fl == (LT_BOLD | LT_RUN_SEL)); p += r;
    r = lt_run(c, l, p, 3, 8, &fl); OK(r == 1 && fl == LT_RUN_SEL); p += r;
    r = lt_run(c, l, p, 3, 8, &fl); OK(r == 2 && fl == (LT_BOLD | LT_RUN_LINK | LT_RUN_SEL)); p += r;
    r = lt_run(c, l, p, 3, 8, &fl); OK(r == 2 && fl == (LT_BOLD | LT_RUN_LINK)); p += r;
    r = lt_run(c, l, p, 3, 8, &fl); OK(r == 5 && fl == 0); p += r;
    OK(lt_run(c, l, p, 3, 8, &fl) == 0);
    lt_content_unref(c);
}

/* Every allocation can fail: nothing leaks, nothing half-made. */
static void allocation_failures(void)
{
    lt_span_in s[1];
    long k;
    memset(s, 0, sizeof s);
    s[0].start = 0; s[0].length = 3; s[0].target = "https://x";
    fail_after = 0;
    {
        int st = -1;
        OK(!lt_content_new("abc", 3, s, 1, &st) && st == LT_NOMEM);
    }
    fail_after = -1;
    for (k = 0; k < 12; k++) {
        char t[4000];
        lt_content *c;
        lt_line *l = (lt_line *)0x1, *keep = l; lt_ulong n = 77;
        int r;
        memset(t, 'a', sizeof t); t[sizeof t - 1] = 0;
        c = mk(t, NULL, 0);
        fail_after = k;
        r = lt_layout(c, 80, font, NULL, &l, &n);
        fail_after = -1;
        if (r) lt_free(l);
        else OK(l == keep && n == 77);
        lt_content_unref(c);
    }
    OK(live == 0);
}

/* A hook's reference keeps the target while the object replaces its content. */
static void reference_outlives_replacement(void)
{
    lt_span_in s[1];
    lt_content *c, *held;
    const char *tgt;
    memset(s, 0, sizeof s);
    s[0].start = 0; s[0].length = 1; s[0].target = "https://kept!";
    c = mk("a", s, 1);
    held = c; lt_content_ref(held);
    tgt = lt_link(held, 0)->target;
    lt_content_unref(c);
    OK(!strcmp(tgt, "https://kept!"));
    lt_content_unref(held);
    OK(live == 0);
}

int main(void)
{
    validation();
    exact_targets();
    control_chars();
    wrapping();
    bold_widths();
    hits_on_every_fragment();
    many_links();
    style_and_links_mixed();
    allocation_failures();
    reference_outlives_replacement();
    printf("linktext core: %d checks passed\n", checks);
    return 0;
}
