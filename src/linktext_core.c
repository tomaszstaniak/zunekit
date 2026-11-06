/* SPDX-License-Identifier: MIT */
#include "linktext_core.h"
#include <string.h>

#define MAX_TEXT   (64UL * 1024 * 1024)
#define MAX_SPANS  1000000UL
#define MAX_TARGET (16UL * 1024 * 1024)

lt_content *lt_content_new(const char *text, lt_ulong len, const lt_span_in *in,
                           lt_ulong n, int *status)
{
    return lt_document_new(text, len, in, n, NULL, 0, status);
}

/* A block covers whole paragraphs: it starts where one starts and ends
   where one ends. Each one after the previous block's end and its newline. */
static int check_blocks(const char *text, lt_ulong len, const lt_block_in *b, lt_ulong nb)
{
    lt_ulong i, next = 0;
    /* Every block holds at least one paragraph of its own. */
    if (nb && nb - 1 > len) return LT_ORDER;
    if (nb && !b) return LT_RANGE;
    for (i = 0; i < nb; i++) {
        lt_ulong s = b[i].start, e;
        if (s > len || b[i].length > len - s) return LT_RANGE;
        e = s + b[i].length;
        if ((s && text[s - 1] != '\n') || (e < len && text[e] != '\n')) return LT_RANGE;
        if (s < next) return LT_ORDER;
        next = e + 1;
    }
    return LT_OK;
}

lt_content *lt_document_new(const char *text, lt_ulong len, const lt_span_in *in,
                            lt_ulong n, const lt_block_in *blk, lt_ulong nb, int *status)
{
    lt_content *c;
    size_t need, tsum = 0;
    lt_ulong i, nl = 0, end = 0;
    char *p;
    int st;

    if (!text) { text = ""; len = 0; }
    if (len == (lt_ulong)-1) {
        size_t sl = strlen(text);
        if (sl > MAX_TEXT) { *status = LT_TOOLARGE; return NULL; }
        len = (lt_ulong)sl;
    }
    if (len > MAX_TEXT || n > MAX_SPANS) { *status = LT_TOOLARGE; return NULL; }
    if (n && !in) { *status = LT_RANGE; return NULL; }

    for (i = 0; i < n; i++) {
        /* Compared this way so that a huge Length cannot wrap around. */
        if (!in[i].length || in[i].start > len || in[i].length > len - in[i].start) {
            *status = LT_RANGE;
            return NULL;
        }
        if (in[i].start < end) { *status = LT_ORDER; return NULL; }
        end = in[i].start + in[i].length;
        if (in[i].target) {
            size_t tl = strlen(in[i].target);
            if (!tl) { *status = LT_TARGET; return NULL; }
            if (tl > MAX_TARGET) { *status = LT_TOOLARGE; return NULL; }
            if (tl + 1 > (size_t)-1 - tsum) { *status = LT_TOOLARGE; return NULL; }
            tsum += tl + 1;
            nl++;
        }
    }
    if ((st = check_blocks(text, len, blk, nb)) != LT_OK) { *status = st; return NULL; }

    /* Many targets can each be within the limit and still pass size_t on a
       32-bit system, so the sum is tested, not only its parts. */
    need = sizeof *c + sizeof(lt_span) * n + sizeof(lt_block) * nb + sizeof(lt_ulong) * nl
           + 2 * sizeof(void *);
    if (tsum > (size_t)-1 - need || (size_t)len + 1 > (size_t)-1 - need - tsum) {
        *status = LT_TOOLARGE;
        return NULL;
    }
    need += tsum + (size_t)len + 1;
    c = (lt_content *)lt_alloc(need);
    if (!c) { *status = LT_NOMEM; return NULL; }

    memset(c, 0, sizeof *c);
    c->refs = 1;
    c->spans = (lt_span *)(c + 1);
    c->blocks = (lt_block *)(c->spans + n);
    c->links = (lt_ulong *)(c->blocks + nb);
    p = (char *)(c->links + nl);
    c->text = p;
    c->len = len;
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];
        p[i] = (ch < 32 && ch != '\n') || ch == 127 ? ' ' : (char)ch;
    }
    p[len] = 0;
    p += len + 1;
    c->nblocks = nb;
    for (i = 0; i < nb; i++) c->blocks[i] = blk[i];
    c->nspans = n;
    for (i = 0; i < n; i++) {
        lt_span *s = &c->spans[i];
        s->start = in[i].start;
        s->length = in[i].length;
        s->flags = in[i].flags & LT_SPAN_FLAGS;
        s->id = in[i].id;
        s->target = NULL;
        s->target_len = 0;
        if (in[i].flags & LT_FONT_FLAGS) c->any_font = 1;
        if (in[i].target) {
            size_t tl = strlen(in[i].target);
            memcpy(p, in[i].target, tl + 1);
            s->target = p;
            s->target_len = (lt_ulong)tl;
            p += tl + 1;
            c->links[c->nlinks++] = i;
        }
    }
    *status = LT_OK;
    return c;
}

lt_ulong lt_text_length(const char *text, lt_ulong len)
{
    if (!text) return 0;
    return len == (lt_ulong)-1 ? (lt_ulong)strlen(text) : len;
}

void lt_content_ref(lt_content *c) { c->refs++; }

void lt_content_unref(lt_content *c)
{
    if (c && --c->refs == 0) lt_free(c);
}

/* The first span that ends after off. */
static lt_ulong span_from(const lt_content *c, lt_ulong off)
{
    lt_ulong lo = 0, hi = c->nspans;
    while (lo < hi) {
        lt_ulong mid = lo + (hi - lo) / 2;
        if (c->spans[mid].start + c->spans[mid].length <= off) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

long lt_width(const lt_content *c, lt_ulong start, lt_ulong n, lt_measure m, void *ctx)
{
    lt_ulong end = start + n, i;
    long w = 0;
    if (!n) return 0;
    if (!c->any_font) return m(ctx, c->text + start, n, 0);
    for (i = span_from(c, start); start < end; ) {
        lt_ulong stop = end, fl = 0;
        if (i < c->nspans && c->spans[i].start <= start) {
            lt_ulong se = c->spans[i].start + c->spans[i].length;
            if (se < stop) stop = se;
            fl = c->spans[i].flags & LT_FONT_FLAGS;
            i++;
        } else if (i < c->nspans && c->spans[i].start < stop) {
            stop = c->spans[i].start;
        }
        w += m(ctx, c->text + start, stop - start, fl);
        start = stop;
    }
    return w;
}

/* How many bytes from pos, at most max, fit in width; at least 1. */
static lt_ulong fit(const lt_content *c, lt_ulong pos, lt_ulong max, long width, lt_measure m, void *ctx)
{
    lt_ulong lo = 1, hi = max;
    /* A character is at least a pixel wide, or the search would be over the
       whole paragraph for every line. */
    if (width >= 0 && hi > (lt_ulong)width + 1) hi = (lt_ulong)width + 1;
    if (hi < 1) hi = 1;
    if (lt_width(c, pos, hi, m, ctx) <= width) return hi;
    while (hi - lo > 1) {
        lt_ulong mid = lo + (hi - lo) / 2;
        if (lt_width(c, pos, mid, m, ctx) <= width) lo = mid; else hi = mid;
    }
    return lo;
}

static int push(lt_line **a, lt_ulong *n, lt_ulong *cap, lt_ulong start, lt_ulong len, lt_ulong shown,
                long x, long block)
{
    if (*n == *cap) {
        lt_ulong nc = *cap ? *cap * 2 : 32;
        lt_line *b = (lt_line *)lt_alloc(sizeof *b * nc);
        if (!b) return 0;
        if (*a) { memcpy(b, *a, sizeof *b * *n); lt_free(*a); }
        *a = b;
        *cap = nc;
    }
    (*a)[*n].start = start;
    (*a)[*n].len = len;
    (*a)[*n].shown = shown;
    (*a)[*n].x = x;
    (*a)[*n].block = block;
    (*n)++;
    return 1;
}

/* Inside an address: after these a break reads better than in mid-word. */
static int soft_break(char ch)
{
    return ch == '/' || ch == '?' || ch == '&' || ch == '=' || ch == '-' || ch == '.';
}

/* Indents in pixels, limited to the width first so that a huge indent
   cannot overflow. */
static long indent_px(lt_ulong chars, long width, long avg)
{
    if (width < 0) width = 0;
    return chars > (lt_ulong)(width / avg) ? width : (long)chars * avg;
}

void lt_block_area(const lt_block *b, long width, long avg, long *left, long *area)
{
    long l, r, w;
    if (avg < 1) avg = 1;
    l = indent_px(b->indent, width, avg);
    r = indent_px(b->rindent, width, avg);
    w = width - l - r;
    if (w < avg) w = avg;
    *left = l;
    *area = w;
}

int lt_layout(const lt_content *c, long width, long avg, lt_measure m, void *ctx, lt_line **out, lt_ulong *n)
{
    lt_line *a = NULL;
    lt_ulong cnt = 0, cap = 0, para = 0, bi = 0, i;
    const char *t = c->text;

    while (para <= c->len) {
        const char *nlp = para < c->len ? (const char *)memchr(t + para, '\n', c->len - para) : NULL;
        lt_ulong e = nlp ? (lt_ulong)(nlp - t) : c->len, pos = para;
        long w = width, x = 0, blk = -1;
        int right = 0;

        while (bi < c->nblocks && c->blocks[bi].start + c->blocks[bi].length < para) bi++;
        if (bi < c->nblocks && c->blocks[bi].start <= para) {
            lt_block_area(&c->blocks[bi], width, avg, &x, &w);
            blk = (long)bi;
            right = (c->blocks[bi].flags & LT_BLOCK_RIGHT) != 0;
        }
        /* A Right block's lines keep their widths in x until the block is
           whole; then they all move to the place of the widest. */
        if (pos == e) {
            if (!push(&a, &cnt, &cap, pos, 0, 0, right ? 0 : x, blk)) goto fail;
        }
        while (pos < e) {
            lt_ulong f = fit(c, pos, e - pos, w, m, ctx), brk, shown;
            if (pos + f >= e) {
                brk = e;
            } else if (t[pos + f] == ' ') {
                brk = pos + f;           /* the word ends exactly at the edge */
            } else {
                brk = 0;
                for (i = pos + f; i > pos; i--)
                    if (t[i - 1] == ' ') { brk = i; break; }
                if (brk) {
                    /* A word longer than a whole line would leave a gap here
                       and be cut anyway: start it on this line. */
                    lt_ulong we = brk;
                    while (we < e && t[we] != ' ') we++;
                    if (lt_width(c, brk, we - brk, m, ctx) > w) brk = 0;
                }
                if (!brk) {
                    for (i = pos + f; i > pos + f / 2; i--)
                        if (soft_break(t[i - 1])) { brk = i; break; }
                    if (!brk) brk = pos + f;
                }
            }
            shown = brk - pos;
            while (shown && t[pos + shown - 1] == ' ') shown--;
            /* Spaces at the break hang past the edge, on this line. */
            while (brk < e && t[brk] == ' ') brk++;
            if (!push(&a, &cnt, &cap, pos, brk - pos, shown,
                      right ? lt_width(c, pos, shown, m, ctx) : x, blk)) goto fail;
            pos = brk;
        }
        if (!nlp) break;
        para = e + 1;
    }

    for (i = 0; i < cnt; ) {
        lt_ulong j;
        long wide = 0, left, area, x;
        const lt_block *b;
        if (a[i].block < 0 || !(c->blocks[a[i].block].flags & LT_BLOCK_RIGHT)) { i++; continue; }
        b = &c->blocks[a[i].block];
        for (j = i; j < cnt && a[j].block == a[i].block; j++)
            if (a[j].x > wide) wide = a[j].x;
        lt_block_area(b, width, avg, &left, &area);
        x = wide < area ? left + area - wide : left;
        for (; i < j; i++) a[i].x = x;
    }
    *out = a;
    *n = cnt;
    return 1;
fail:
    lt_free(a);
    return 0;
}

int lt_block_box(const lt_content *c, const lt_line *l, lt_ulong nl, lt_ulong b,
                 long width, long avg, lt_ulong *first, lt_ulong *last, long *x0, long *x1)
{
    lt_ulong i;
    long left, area, pad;
    if (b >= c->nblocks || !nl) return 0;
    i = lt_line_of(l, nl, c->blocks[b].start);
    if (l[i].block != (long)b) return 0;
    *first = i;
    while (i + 1 < nl && l[i + 1].block == (long)b) i++;
    *last = i;
    if (avg < 1) avg = 1;
    pad = avg / 2;
    lt_block_area(&c->blocks[b], width, avg, &left, &area);
    *x0 = (c->blocks[b].flags & LT_BLOCK_RIGHT ? l[*first].x : left) - pad;
    *x1 = left + area + pad;
    return 1;
}

lt_ulong lt_line_of(const lt_line *l, lt_ulong n, lt_ulong off)
{
    lt_ulong lo = 0, hi = n;
    if (!n) return 0;
    while (hi - lo > 1) {
        lt_ulong mid = lo + (hi - lo) / 2;
        if (l[mid].start <= off) lo = mid; else hi = mid;
    }
    return lo;
}

long lt_hit(const lt_content *c, const lt_line *line, long x, lt_measure m, void *ctx, lt_ulong *caret)
{
    lt_ulong lo = 0, hi = line->shown;
    long wlo, whi;
    x -= line->x;
    if (x <= 0 || !line->shown) {
        *caret = line->start;
        return x >= 0 && line->shown ? (long)line->start : -1;
    }
    if (lt_width(c, line->start, hi, m, ctx) <= x) {
        *caret = line->start + line->shown;
        return -1;
    }
    /* Largest k whose width is at most x: x is on character k. */
    while (hi - lo > 1) {
        lt_ulong mid = lo + (hi - lo) / 2;
        if (lt_width(c, line->start, mid, m, ctx) <= x) lo = mid; else hi = mid;
    }
    wlo = lt_width(c, line->start, lo, m, ctx);
    whi = lt_width(c, line->start, lo + 1, m, ctx);
    *caret = line->start + (x - wlo >= whi - x ? lo + 1 : lo);
    return (long)(line->start + lo);
}

long lt_link_at(const lt_content *c, lt_ulong off)
{
    lt_ulong i = span_from(c, off), lo = 0, hi = c->nlinks;
    if (i >= c->nspans || c->spans[i].start > off || !c->spans[i].target) return -1;
    while (lo < hi) {
        lt_ulong mid = lo + (hi - lo) / 2;
        if (c->links[mid] < i) lo = mid + 1; else hi = mid;
    }
    return (long)lo;
}

const lt_span *lt_link(const lt_content *c, lt_ulong link)
{
    return link < c->nlinks ? &c->spans[c->links[link]] : NULL;
}

lt_ulong lt_common_prefix(const lt_content *a, const lt_content *b)
{
    lt_ulong n = a->len < b->len ? a->len : b->len, i = 0;
    while (i < n && a->text[i] == b->text[i]) i++;
    return i;
}

int lt_same_link(const lt_content *a, const lt_content *b, long link, lt_ulong upto)
{
    const lt_span *x, *y;
    if (link < 0) return 0;
    x = lt_link(a, (lt_ulong)link);
    y = lt_link(b, (lt_ulong)link);
    if (!x || !y) return 0;
    return x->start == y->start && x->length == y->length && x->start + x->length <= upto
        && x->target_len == y->target_len && !memcmp(x->target, y->target, x->target_len);
}

long lt_link_step(const lt_content *c, long cur, int dir)
{
    long next;
    if (!c->nlinks) return -1;
    if (cur < 0) return dir > 0 ? 0 : (long)c->nlinks - 1;
    next = cur + (dir > 0 ? 1 : -1);
    return next >= 0 && next < (long)c->nlinks ? next : -1;
}

int lt_link_fragment(const lt_content *c, const lt_line *l, lt_ulong nl, lt_ulong link,
                     lt_ulong f, lt_measure m, void *ctx, lt_ulong *line, long *x0, long *x1)
{
    const lt_span *s = lt_link(c, link);
    lt_ulong i, a, b;
    if (!s || !nl) return 0;
    i = lt_line_of(l, nl, s->start);
    for (; i < nl && l[i].start < s->start + s->length; i++) {
        lt_ulong le = l[i].start + l[i].shown;
        a = s->start > l[i].start ? s->start : l[i].start;
        b = s->start + s->length < le ? s->start + s->length : le;
        if (a >= b) continue;
        if (f--) continue;
        *line = i;
        *x0 = l[i].x + lt_width(c, l[i].start, a - l[i].start, m, ctx);
        *x1 = *x0 + lt_width(c, a, b - a, m, ctx);
        return 1;
    }
    return 0;
}

lt_ulong lt_run(const lt_content *c, const lt_line *line, lt_ulong pos,
                lt_ulong sel_a, lt_ulong sel_b, lt_ulong *flags)
{
    lt_ulong end = line->start + line->shown, stop = end, fl = 0, i;
    if (pos >= end) return 0;
    i = span_from(c, pos);
    if (i < c->nspans) {
        const lt_span *s = &c->spans[i];
        if (s->start <= pos) {
            if (s->start + s->length < stop) stop = s->start + s->length;
            fl |= s->flags & LT_SPAN_FLAGS;
            if (s->target) fl |= LT_RUN_LINK;
        } else if (s->start < stop) {
            stop = s->start;
        }
    }
    if (sel_a < sel_b) {
        if (pos >= sel_a && pos < sel_b) {
            fl |= LT_RUN_SEL;
            if (sel_b < stop) stop = sel_b;
        } else if (pos < sel_a && sel_a < stop) {
            stop = sel_a;
        }
    }
    *flags = fl;
    return stop - pos;
}
