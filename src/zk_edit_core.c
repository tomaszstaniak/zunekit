/* SPDX-License-Identifier: MIT */
/* zk_edit core: see zk_edit_core.h. */
#include "zk_edit_core.h"
#include <stdlib.h>
#include <string.h>

enum { K_TYPING, K_BACKSPACE, K_DELETE, K_OTHER };

typedef struct {
    size_t at;
    char *removed, *inserted;
    size_t nrem, nins;
    size_t b_anchor, b_caret, a_anchor, a_caret;
    int kind;
} step;

typedef struct { step *s; size_t n, cap; } stack;

struct ze_doc {
    char *t;
    size_t n, cap;
    size_t caret, anchor;
    stack undo, redo;
    int open;                 /* the last step may still grow */
    unsigned long rev;
};

static void step_free(step *s) { free(s->removed); free(s->inserted); }

static void stack_clear(stack *k)
{
    size_t i;
    for (i = 0; i < k->n; i++) step_free(&k->s[i]);
    k->n = 0;
}

static int stack_room(stack *k)
{
    if (k->n < k->cap) return 1;
    {
        size_t nc = k->cap ? k->cap * 2 : 16;
        step *s = (step *)realloc(k->s, sizeof *s * nc);
        if (!s) return 0;
        k->s = s;
        k->cap = nc;
    }
    return 1;
}

static char *dup(const char *s, size_t n)
{
    char *p = (char *)malloc(n ? n : 1);
    if (p && n) memcpy(p, s, n);
    return p;
}

ze_doc *ze_new(void)
{
    ze_doc *d = (ze_doc *)calloc(1, sizeof *d);
    if (!d) return NULL;
    if (!(d->t = (char *)malloc(16))) { free(d); return NULL; }
    d->t[0] = 0;
    d->cap = 16;
    return d;
}

void ze_free(ze_doc *d)
{
    if (!d) return;
    stack_clear(&d->undo); free(d->undo.s);
    stack_clear(&d->redo); free(d->redo.s);
    free(d->t);
    free(d);
}

/* Room for need bytes and the NUL; the text stays as it is on failure. */
static int room(ze_doc *d, size_t need)
{
    char *t;
    size_t nc;
    if (need + 1 <= d->cap) return 1;
    nc = d->cap;
    while (nc < need + 1) nc = nc * 2;
    if (!(t = (char *)realloc(d->t, nc))) return 0;
    d->t = t;
    d->cap = nc;
    return 1;
}

int ze_set_text(ze_doc *d, const char *s, size_t n)
{
    if (!zt_valid(s, n)) return ZE_INVALID;
    if (!room(d, n)) return ZE_NOMEM;
    memcpy(d->t, s, n);
    d->t[n] = 0;
    d->n = n;
    d->caret = d->anchor = n;
    stack_clear(&d->undo);
    stack_clear(&d->redo);
    d->open = 0;
    d->rev++;
    return 1;
}

const char *ze_text(const ze_doc *d, size_t *n) { if (n) *n = d->n; return d->t; }
unsigned long ze_revision(const ze_doc *d) { return d->rev; }
size_t ze_caret(const ze_doc *d) { return d->caret; }
size_t ze_anchor(const ze_doc *d) { return d->anchor; }

void ze_selection(const ze_doc *d, size_t *a, size_t *b)
{
    *a = d->anchor < d->caret ? d->anchor : d->caret;
    *b = d->anchor < d->caret ? d->caret : d->anchor;
}

const char *ze_selected(const ze_doc *d, size_t *n)
{
    size_t a, b;
    ze_selection(d, &a, &b);
    *n = b - a;
    return d->t + a;
}

static size_t snap(const ze_doc *d, size_t pos)
{
    if (pos >= d->n) return d->n;
    return zt_cluster_start(d->t, d->n, pos);
}

void ze_set_caret(ze_doc *d, size_t pos, int extend)
{
    d->caret = snap(d, pos);
    if (!extend) d->anchor = d->caret;
    d->open = 0;
}

void ze_select(ze_doc *d, size_t anchor, size_t caret)
{
    d->anchor = snap(d, anchor);
    d->caret = snap(d, caret);
    d->open = 0;
}

void ze_select_all(ze_doc *d) { ze_select(d, 0, d->n); }

void ze_step(ze_doc *d, int forward, int extend)
{
    size_t a, b, pos;
    ze_selection(d, &a, &b);
    if (!extend && a != b) pos = forward ? b : a;
    else if (forward) pos = zt_next_cluster(d->t, d->n, d->caret);
    else pos = zt_prev_cluster(d->t, d->n, d->caret);
    ze_set_caret(d, pos, extend);
}

void ze_word_step(ze_doc *d, int forward, int extend)
{
    size_t pos = forward ? zt_next_word(d->t, d->n, d->caret) : zt_prev_word(d->t, d->n, d->caret);
    ze_set_caret(d, pos, extend);
}

/* Replaces [at, end) with ins as one edit of `kind`, merging into the last
   step as MarkEdit does. All memory first, then the change. */
static int apply(ze_doc *d, size_t at, size_t end, const char *ins, size_t nins, int kind)
{
    size_t nrem = end - at, newn = d->n - nrem + nins;
    step *last = d->undo.n ? &d->undo.s[d->undo.n - 1] : NULL;
    char *grow = NULL, *rem = NULL, *insc = NULL;
    int merge = 0;

    if (d->open && kind != K_OTHER && last && last->kind == kind) {
        if (kind == K_TYPING)
            merge = last->at + last->nins == at && nrem == 0 && !(nins == 1 && ins[0] == '\n')
                    && !(ins[0] == ' ' && !(last->nins && last->inserted[last->nins - 1] == ' '));
        else if (kind == K_BACKSPACE) merge = end == last->at && !nins;
        else if (kind == K_DELETE) merge = at == last->at && !nins;
    }
    if (!room(d, newn)) return ZE_NOMEM;
    if (merge) {
        if (kind == K_TYPING) {
            if (!(grow = (char *)malloc(last->nins + nins))) return ZE_NOMEM;
            memcpy(grow, last->inserted, last->nins);
            memcpy(grow + last->nins, ins, nins);
        } else {
            if (!(grow = (char *)malloc(last->nrem + nrem ? last->nrem + nrem : 1))) return ZE_NOMEM;
            if (kind == K_BACKSPACE) {       /* removed before the step's own */
                memcpy(grow, d->t + at, nrem);
                memcpy(grow + nrem, last->removed, last->nrem);
            } else {
                memcpy(grow, last->removed, last->nrem);
                memcpy(grow + last->nrem, d->t + at, nrem);
            }
        }
    } else {
        if (!stack_room(&d->undo)) return ZE_NOMEM;
        if (!(rem = dup(d->t + at, nrem))) return ZE_NOMEM;
        if (!(insc = dup(ins, nins))) { free(rem); return ZE_NOMEM; }
    }

    /* nothing can fail from here on */
    if (merge) {
        if (kind == K_TYPING) { free(last->inserted); last->inserted = grow; last->nins += nins; }
        else { free(last->removed); last->removed = grow; last->nrem += nrem; if (kind == K_BACKSPACE) last->at = at; }
        last->a_anchor = last->a_caret = at + nins;
    } else {
        step *s;
        if (d->undo.n == ZE_UNDO_LIMIT) {
            step_free(&d->undo.s[0]);
            memmove(d->undo.s, d->undo.s + 1, sizeof *d->undo.s * (d->undo.n - 1));
            d->undo.n--;
        }
        s = &d->undo.s[d->undo.n++];
        s->at = at;
        s->removed = rem; s->nrem = nrem;
        s->inserted = insc; s->nins = nins;
        s->b_anchor = d->anchor; s->b_caret = d->caret;
        s->a_anchor = s->a_caret = at + nins;
        s->kind = kind;
    }
    memmove(d->t + at + nins, d->t + end, d->n - end);
    memcpy(d->t + at, ins, nins);
    d->n = newn;
    d->t[newn] = 0;
    d->caret = d->anchor = at + nins;
    stack_clear(&d->redo);
    d->open = kind != K_OTHER;
    d->rev++;
    return 1;
}

int ze_insert(ze_doc *d, const char *s, size_t n)
{
    size_t a, b, len;
    int kind;
    if (!zt_valid(s, n)) return ZE_INVALID;
    ze_selection(d, &a, &b);
    if (a == b && !n) return 0;
    kind = a == b && n && zt_decode(s, n, 0, &len) && len == n ? K_TYPING : K_OTHER;
    return apply(d, a, b, s, n, kind);
}

int ze_backspace(ze_doc *d)
{
    size_t a, b;
    ze_selection(d, &a, &b);
    if (a != b) return apply(d, a, b, "", 0, K_OTHER);
    if (!a) return 0;
    return apply(d, zt_prev_cluster(d->t, d->n, a), a, "", 0, K_BACKSPACE);
}

int ze_delete(ze_doc *d)
{
    size_t a, b;
    ze_selection(d, &a, &b);
    if (a != b) return apply(d, a, b, "", 0, K_OTHER);
    if (a >= d->n) return 0;
    return apply(d, a, zt_next_cluster(d->t, d->n, a), "", 0, K_DELETE);
}

/* Puts `put` (n bytes) over [at, at + nold). */
static int swap_text(ze_doc *d, size_t at, size_t nold, const char *put, size_t n)
{
    size_t newn = d->n - nold + n;
    if (!room(d, newn)) return 0;
    memmove(d->t + at + n, d->t + at + nold, d->n - at - nold);
    memcpy(d->t + at, put, n);
    d->n = newn;
    d->t[newn] = 0;
    return 1;
}

int ze_undo(ze_doc *d)
{
    step s;
    if (!d->undo.n) return 0;
    if (!stack_room(&d->redo)) return ZE_NOMEM;
    s = d->undo.s[d->undo.n - 1];
    if (!swap_text(d, s.at, s.nins, s.removed, s.nrem)) return ZE_NOMEM;
    d->undo.n--;
    d->redo.s[d->redo.n++] = s;
    d->anchor = s.b_anchor;
    d->caret = s.b_caret;
    d->open = 0;
    d->rev++;
    return 1;
}

int ze_redo(ze_doc *d)
{
    step s;
    if (!d->redo.n) return 0;
    if (!stack_room(&d->undo)) return ZE_NOMEM;
    s = d->redo.s[d->redo.n - 1];
    if (!swap_text(d, s.at, s.nrem, s.inserted, s.nins)) return ZE_NOMEM;
    d->redo.n--;
    d->undo.s[d->undo.n++] = s;
    d->anchor = s.a_anchor;
    d->caret = s.a_caret;
    d->open = 0;
    d->rev++;
    return 1;
}

int ze_can_undo(const ze_doc *d) { return d->undo.n > 0; }
int ze_can_redo(const ze_doc *d) { return d->redo.n > 0; }

/* ----------------------------------------------------------------- layout */

void ze_view_init(ze_view *v) { memset(v, 0, sizeof *v); }
void ze_view_free(ze_view *v) { free(v->line); memset(v, 0, sizeof *v); }

static int add_line(ze_line **a, size_t *n, size_t *cap, size_t start, size_t len)
{
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 32;
        ze_line *l = (ze_line *)realloc(*a, sizeof *l * nc);
        if (!l) return 0;
        *a = l;
        *cap = nc;
    }
    (*a)[*n].start = start;
    (*a)[*n].len = len;
    (*a)[*n].y = 0;
    (*n)++;
    return 1;
}

int ze_layout(ze_view *v, const ze_doc *d, zt_font *f, long width)
{
    ze_line *a = NULL;
    size_t n = 0, cap = 0, p = 0, i;
    const char *t = d->t;
    long lineh = zt_font_height(f);
    if (width < 1) width = 1;
    for (;;) {
        const char *nl = (const char *)memchr(t + p, '\n', d->n - p);
        size_t e = nl ? (size_t)(nl - t) : d->n, pos = p;
        if (pos == e && !add_line(&a, &n, &cap, pos, 0)) goto fail;
        while (pos < e) {
            size_t k = zt_fit(f, t + pos, e - pos, width, v->flags), brk, next, j;
            if (pos + k >= e) { if (!add_line(&a, &n, &cap, pos, e - pos)) goto fail; break; }
            if (!k) {
                /* not even one cluster fits: it goes on the line alone */
                zt_iter it;
                zt_iter_init(&it, t, d->n, pos);
                k = zt_iter_next(&it) - pos;
            }
            /* a word that fits stays whole: back to the last space */
            brk = pos + k;
            next = brk;
            if (t[brk] != ' ') {
                for (j = brk; j > pos && t[j - 1] != ' '; j--) ;
                if (j > pos) { brk = j; next = j; }
            }
            while (brk > pos && t[brk - 1] == ' ') brk--;
            while (next < e && t[next] == ' ') next++;
            if (brk == pos) brk = next;           /* only spaces: they make the line */
            if (!add_line(&a, &n, &cap, pos, brk - pos)) goto fail;
            pos = next;
            if (pos >= e) break;
        }
        if (!nl) break;
        p = e + 1;
    }
    for (i = 0; i < n; i++) a[i].y = (long)i * lineh;
    free(v->line);
    v->line = a;
    v->nlines = n;
    v->cap = cap;
    v->width = width;
    v->lineh = lineh;
    v->height = (long)n * lineh;
    return 1;
fail:
    free(a);
    return 0;
}

size_t ze_line_of(const ze_view *v, size_t off)
{
    size_t lo = 0, hi = v->nlines;
    if (!v->nlines) return 0;
    while (hi - lo > 1) {
        size_t mid = lo + (hi - lo) / 2;
        if (v->line[mid].start <= off) lo = mid; else hi = mid;
    }
    return lo;
}

long ze_caret_x(const ze_view *v, const ze_doc *d, zt_font *f, size_t off, size_t *line)
{
    size_t i = ze_line_of(v, off);
    const ze_line *l;
    if (line) *line = i;
    if (!v->nlines) return 0;
    l = &v->line[i];
    if (off <= l->start) return 0;
    if (off >= l->start + l->len) return zt_measure(f, d->t + l->start, l->len, v->flags);
    return zt_x(f, d->t + l->start, l->len, off - l->start, v->flags);
}

size_t ze_hit(const ze_view *v, const ze_doc *d, zt_font *f, long x, long y)
{
    size_t i;
    const ze_line *l;
    if (!v->nlines) return 0;
    if (y < 0) return 0;
    i = v->lineh > 0 ? (size_t)(y / v->lineh) : 0;
    if (i >= v->nlines) return d->n;
    l = &v->line[i];
    return l->start + zt_hit(f, d->t + l->start, l->len, x, v->flags);
}

size_t ze_vertical(const ze_view *v, const ze_doc *d, zt_font *f, size_t off, long dlines, long *goal)
{
    size_t i;
    long target;
    if (!v->nlines) return off;
    if (*goal < 0) *goal = ze_caret_x(v, d, f, off, &i);
    else i = ze_line_of(v, off);
    target = (long)i + dlines;
    if (target < 0) return 0;
    if (target >= (long)v->nlines) return d->n;
    return ze_hit(v, d, f, *goal, (long)target * v->lineh);
}

size_t ze_line_home(const ze_view *v, size_t off)
{
    return v->nlines ? v->line[ze_line_of(v, off)].start : 0;
}

size_t ze_line_end(const ze_view *v, size_t off)
{
    const ze_line *l;
    if (!v->nlines) return off;
    l = &v->line[ze_line_of(v, off)];
    return l->start + l->len;
}
