/* SPDX-License-Identifier: MIT */
/* zk_edit core on the host. The first five cases are MarkEdit's document
 * tests (aros-markedit editor/src/lib.rs, `mod tests`), one for one; then
 * the review's text set (2026-11-07), allocation failures at every step,
 * and the layout. zk_edit_core.c is built with failing allocators
 * (ZE_FAIL_* below), zk_text with the system's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zk_edit_core.h"

static int checks;
#define OK(x) do { checks++; if (!(x)) { printf("FAILED line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

/* Allocation failures for zk_edit_core.c only (compiled with -Dmalloc=fm
   -Drealloc=fr -Dcalloc=fc): the n-th call from now fails. */
static long fail_in = -1;
static int failing(void) { if (fail_in < 0) return 0; if (fail_in-- == 0) return 1; return 0; }
void *fm(size_t n) { return failing() ? NULL : malloc(n); }
void *fr(void *p, size_t n) { return failing() ? NULL : realloc(p, n); }
void *fc(size_t a, size_t b) { return failing() ? NULL : calloc(a, b); }

static int is(const ze_doc *d, const char *s)
{
    size_t n;
    const char *t = ze_text(d, &n);
    return n == strlen(s) && !memcmp(t, s, n);
}

static void ins(ze_doc *d, const char *s) { OK(ze_insert(d, s, strlen(s)) == 1); }

/* --- MarkEdit's document tests ------------------------------------------ */

static void typing_merges_into_word_steps(void)
{
    ze_doc *d = ze_new();
    const char *s = "hello world";
    size_t i;
    for (i = 0; s[i]; i++) OK(ze_insert(d, s + i, 1) == 1);
    OK(is(d, "hello world"));
    OK(ze_undo(d) == 1 && is(d, "hello"));
    OK(ze_undo(d) == 1 && is(d, ""));
    OK(ze_redo(d) == 1 && ze_redo(d) == 1 && is(d, "hello world"));
    OK(ze_redo(d) == 0);
    ze_free(d);
}

static void backspace_removes_whole_cluster(void)
{
    ze_doc *d = ze_new();
    OK(ze_set_text(d, "ze\xcc\x81\xc5\xbc", 6) == 1);              /* z, e + U+0301, ż */
    ze_set_caret(d, 6, 0);
    OK(ze_backspace(d) == 1 && is(d, "ze\xcc\x81"));
    OK(ze_backspace(d) == 1 && is(d, "z"));
    OK(ze_undo(d) == 1 && is(d, "ze\xcc\x81\xc5\xbc"));
    ze_free(d);
}

static void caret_never_splits_a_cluster(void)
{
    ze_doc *d = ze_new();
    OK(ze_set_text(d, "ae\xcc\x81" "b", 5) == 1);
    ze_set_caret(d, 2, 0); OK(ze_caret(d) == 1);
    ze_set_caret(d, 3, 0); OK(ze_caret(d) == 1);
    ze_set_caret(d, 1, 0); ze_step(d, 1, 0); OK(ze_caret(d) == 4);
    ze_step(d, 0, 0); OK(ze_caret(d) == 1);
    OK(ze_set_text(d, "a\r\nb", 4) == 1);
    ze_set_caret(d, 1, 0); ze_step(d, 1, 0); OK(ze_caret(d) == 3);
    ze_free(d);
}

static void selection_replace_and_undo_restore_selection(void)
{
    ze_doc *d = ze_new();
    size_t a, b, n;
    const char *sel;
    OK(ze_set_text(d, "Shopping list", 13) == 1);
    ze_select(d, 0, 8);
    sel = ze_selected(d, &n);
    OK(n == 8 && !memcmp(sel, "Shopping", 8));
    ins(d, "Zakupy");
    OK(is(d, "Zakupy list"));
    OK(ze_undo(d) == 1 && is(d, "Shopping list"));
    ze_selection(d, &a, &b);
    OK(a == 0 && b == 8);
    ze_free(d);
}

static void newline_and_delete_forward(void)
{
    ze_doc *d = ze_new();
    OK(ze_set_text(d, "ab", 2) == 1);
    ze_set_caret(d, 1, 0);
    ins(d, "\n");
    OK(is(d, "a\nb"));
    ze_set_caret(d, 0, 0);
    OK(ze_delete(d) == 1 && ze_delete(d) == 1 && is(d, "b"));
    OK(ze_undo(d) == 1 && is(d, "a\nb"));
    ze_free(d);
}

/* MarkEdit text/src/editor.rs: invalid_utf8_is_refused_not_replaced */
static void invalid_utf8_is_refused_not_replaced(void)
{
    ze_doc *d = ze_new();
    OK(ze_set_text(d, "ok", 2) == 1);
    OK(ze_set_text(d, "\xff\xfe", 2) == ZE_INVALID && is(d, "ok"));
    OK(ze_insert(d, "a\xc3", 2) == ZE_INVALID && is(d, "ok"));
    ze_free(d);
}

/* --- the review's text set -------------------------------------------- */

#define POLISH "Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 g\xc4\x99\xc5\x9bl\xc4\x85 ja\xc5\xba\xc5\x84"
#define GREEK  "\xce\x95\xce\xbb\xce\xbb\xce\xb7\xce\xbd\xce\xb9\xce\xba\xce\xac"
#define CYRIL  "\xd0\x96\xd0\xb8\xd0\xb7\xd0\xbd\xd1\x8c"
#define MATHS  "\xe2\x88\x80x \xe2\x88\x88 \xe2\x84\x9d"
#define DECOMP "e\xcc\x81"
#define CLEF   "\xf0\x9d\x84\x9e"
#define FAMILY "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7"
#define FLAGS  "\xf0\x9f\x87\xb5\xf0\x9f\x87\xb1"

static void text_set(void)
{
    ze_doc *d = ze_new();
    const char *all = POLISH " " GREEK " " CYRIL " " MATHS " " DECOMP " " CLEF " " FAMILY " " FLAGS;
    size_t n, i;
    /* typed one code point at a time, as a keyboard would */
    for (i = 0; all[i]; ) {
        size_t len;
        zt_decode(all, strlen(all), i, &len);
        OK(ze_insert(d, all + i, len) == 1);
        i += len;
    }
    OK(is(d, all));                                                  /* bytes kept exactly */
    /* Backspace takes the flag, then the family emoji whole, then the clef */
    OK(ze_backspace(d) == 1 && is(d, POLISH " " GREEK " " CYRIL " " MATHS " " DECOMP " " CLEF " " FAMILY " "));
    OK(ze_backspace(d) == 1 && ze_backspace(d) == 1 && is(d, POLISH " " GREEK " " CYRIL " " MATHS " " DECOMP " " CLEF " "));
    OK(ze_backspace(d) == 1 && ze_backspace(d) == 1 && is(d, POLISH " " GREEK " " CYRIL " " MATHS " " DECOMP " "));
    /* the decomposed é goes whole */
    OK(ze_backspace(d) == 1 && ze_backspace(d) == 1 && is(d, POLISH " " GREEK " " CYRIL " " MATHS " "));
    /* word moves over Greek and Cyrillic */
    ze_set_caret(d, 0, 0);
    ze_word_step(d, 1, 0);
    OK(ze_caret(d) == strlen("Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 "));
    /* a selection over the Greek word replaced, undone, redone */
    {
        size_t g = strlen(POLISH " ");
        ze_select(d, g, g + strlen(GREEK));
        ins(d, "alfa");
        OK(strstr(ze_text(d, &n), " alfa "));
        OK(ze_undo(d) == 1 && strstr(ze_text(d, &n), GREEK));
        OK(ze_redo(d) == 1 && strstr(ze_text(d, &n), " alfa "));
        OK(ze_undo(d) == 1);
    }
    /* undo back to empty, redo to the end: the same bytes, the undone
       replacement redone last (it was still on the redo stack) */
    while (ze_undo(d) == 1) ;
    OK(is(d, ""));
    while (ze_redo(d) == 1) ;
    OK(is(d, POLISH " alfa " CYRIL " " MATHS " "));
    ze_free(d);
}

/* --- allocation failures ---------------------------------------------- */

static void snapshot(const ze_doc *d, char *buf, size_t *n, size_t *caret, size_t *anchor, int *u, int *r)
{
    const char *t = ze_text(d, n);
    memcpy(buf, t, *n);
    *caret = ze_caret(d);
    *anchor = ze_anchor(d);
    *u = ze_can_undo(d);
    *r = ze_can_redo(d);
}

static void nomem(void)
{
    int op;
    for (op = 0; op < 6; op++) {
        long k;
        for (k = 0; k < 12; k++) {
            ze_doc *d = ze_new();
            char before[256];
            size_t n0, c0, a0;
            int u0, r0, r;
            ins(d, POLISH);
            ins(d, " ");
            ins(d, CYRIL);
            if (op == 4) OK(ze_undo(d) == 1);
            if (op == 5) { OK(ze_undo(d) == 1); }
            ze_select(d, 3, 9);
            snapshot(d, before, &n0, &c0, &a0, &u0, &r0);
            fail_in = k;
            switch (op) {
            case 0: r = ze_insert(d, GREEK, strlen(GREEK)); break;
            case 1: ze_set_caret(d, ze_caret(d), 0); fail_in = k; r = ze_backspace(d); break;
            case 2: r = ze_delete(d); break;
            case 3: r = ze_set_text(d, MATHS, strlen(MATHS)); break;
            case 4: r = ze_redo(d); break;
            default: r = ze_undo(d); break;
            }
            fail_in = -1;
            if (r == ZE_NOMEM) {
                /* the draft, the caret, the selection and the history as they were */
                size_t n1, c1, a1;
                char after[256];
                int u1, r1;
                snapshot(d, after, &n1, &c1, &a1, &u1, &r1);
                OK(n1 == n0 && !memcmp(after, before, n0) && u1 == u0 && r1 == r0);
                if (op != 1) OK(c1 == c0 && a1 == a0);
            }
            ze_free(d);
        }
    }
}

/* --- layout ------------------------------------------------------------ */

static void layout(const char *dir)
{
    char vera[512], open[512];
    const char *faces[2];
    zt_font *f;
    ze_doc *d = ze_new();
    ze_view v;
    size_t i, line;
    long goal = -1, x;
    const char *t;
    size_t n;

    snprintf(vera, sizeof vera, "%s/VeraSans.ttf", dir);
    snprintf(open, sizeof open, "%s/OpenSans-Regular.ttf", dir);
    faces[0] = vera; faces[1] = open;
    OK((f = zt_font_open(faces, 2, 16)) != NULL);
    ze_view_init(&v);
    OK(ze_set_text(d, POLISH " " GREEK " " CYRIL "\n\n" DECOMP DECOMP " " CLEF, strlen(POLISH " " GREEK " " CYRIL "\n\n" DECOMP DECOMP " " CLEF)) == 1);
    OK(ze_layout(&v, d, f, 120));
    t = ze_text(d, &n);
    OK(v.nlines >= 4 && v.lineh == zt_font_height(f));
    for (i = 0; i < v.nlines; i++) {
        /* every line starts on a boundary and fits the width */
        OK(zt_cluster_start(t, n, v.line[i].start) == v.line[i].start || v.line[i].start == n);
        OK(zt_measure(f, t + v.line[i].start, v.line[i].len, 0) <= 120 || zt_clusters(t + v.line[i].start, v.line[i].len) == 1);
        OK(v.line[i].y == (long)i * v.lineh);
    }
    /* the empty paragraph is its own line */
    {
        int empty = 0;
        for (i = 0; i < v.nlines; i++) if (!v.line[i].len && t[v.line[i].start] == '\n') empty = 1;
        OK(empty);
    }
    /* caret x and hit agree; Up and Down keep the column */
    for (i = 0; i <= n; i = zt_next_cluster(t, n, i)) {
        size_t back;
        x = ze_caret_x(&v, d, f, i, &line);
        back = ze_hit(&v, d, f, x, v.line[line].y + 1);
        OK(back == i || (back == v.line[line].start + v.line[line].len && i >= back));
        if (i == n) break;
    }
    ze_set_caret(d, 3, 0);
    x = ze_caret_x(&v, d, f, 3, &line);
    goal = -1;
    i = ze_vertical(&v, d, f, 3, 1, &goal);
    OK(goal == x && ze_line_of(&v, i) == line + 1);
    OK(ze_vertical(&v, d, f, i, -1, &goal) == zt_cluster_start(t, n, 3) || ze_line_of(&v, ze_vertical(&v, d, f, i, -1, &goal)) == line);
    OK(ze_line_home(&v, 3) == 0 && ze_line_end(&v, 3) == v.line[0].len);
    /* out of memory in layout: the old lines stay */
    {
        size_t old = v.nlines;
        fail_in = 0;
        OK(!ze_layout(&v, d, f, 60));
        fail_in = -1;
        OK(v.nlines == old);
    }
    ze_view_free(&v);
    ze_free(d);
    zt_font_close(f);
}

int main(int argc, char **argv)
{
    typing_merges_into_word_steps();
    backspace_removes_whole_cluster();
    caret_never_splits_a_cluster();
    selection_replace_and_undo_restore_selection();
    newline_and_delete_forward();
    invalid_utf8_is_refused_not_replaced();
    text_set();
    nomem();
    layout(argc > 1 ? argv[1] : "tests/fixtures/fonts");
    printf("zk_edit: %d checks passed\n", checks);
    return 0;
}
