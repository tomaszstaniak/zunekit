/* SPDX-License-Identifier: MIT */
#include "linktext_core.h"
#include "zk_text.h"
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
    {
        /* TAB separates a Table block's cells and is a space anywhere else,
           and past the last column a table has. */
        lt_ulong bi = 0, tabs = 0;
        for (i = 0; i < len; i++) {
            unsigned char ch = (unsigned char)text[i];
            int in_table;
            while (bi < nb && blk[bi].start + blk[bi].length < i) bi++;
            in_table = bi < nb && blk[bi].start <= i && (blk[bi].flags & LT_BLOCK_TABLE);
            if (ch == '\n') tabs = 0;
            if (ch == '\t' && in_table && ++tabs < LT_TABLE_COLS) { p[i] = '\t'; continue; }
            p[i] = (ch < 32 && ch != '\n') || ch == 127 ? ' ' : (char)ch;
        }
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
void lt_set_utf8(lt_content *c, int on) { c->utf8 = on != 0; }

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
    if (lt_width(c, pos, hi, m, ctx) <= width) lo = hi;
    else while (hi - lo > 1) {
        lt_ulong mid = lo + (hi - lo) / 2;
        if (lt_width(c, pos, mid, m, ctx) <= width) lo = mid; else hi = mid;
    }
    if (c->utf8 && pos + lo < pos + max) {
        /* back to the cluster it is in; a cluster too wide for the line
           still goes on it, whole */
        lt_ulong b = zt_cluster_start(c->text, c->len, pos + lo);
        if (b <= pos) b = zt_next_cluster(c->text, c->len, pos);
        if (b > pos + max) b = pos + max;
        lo = b - pos;
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
    (*a)[*n].y = (long)*n;           /* one unit each until lt_heights */
    (*a)[*n].h = 1;
    (*a)[*n].pstart = start;
    (*a)[*n].cell = -1;
    (*a)[*n].trow = -1;
    (*a)[*n].cx0 = (*a)[*n].cx1 = 0;
    (*a)[*n].cont = 0;
    (*a)[*n].large = 0;
    (*a)[*n].ty = 0;
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

/* Lines for bytes pos..e of one paragraph or cell, w pixels wide, at x
   (with right: each line at its own width, moved later). */
static int wrap_range(const lt_content *c, lt_ulong pos, lt_ulong e, long w, lt_measure m, void *ctx,
                      long x, int right, long blk, lt_line **a, lt_ulong *cnt, lt_ulong *cap)
{
    const char *t = c->text;
    lt_ulong i;
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
        if (!push(a, cnt, cap, pos, brk - pos, shown,
                  right ? lt_width(c, pos, shown, m, ctx) : x, blk)) return 0;
        pos = brk;
    }
    return 1;
}

/* The end of row paragraph [ps, pe)'s cell that starts at cs. */
static lt_ulong cell_end(const lt_content *c, lt_ulong cs, lt_ulong pe)
{
    const char *q = cs < pe ? (const char *)memchr(c->text + cs, '\t', pe - cs) : NULL;
    return q ? (lt_ulong)(q - c->text) : pe;
}

/* Cell spacing: PadX eighths of a character at each side of a cell's text,
   one character without Pad. */
static long table_padx(const lt_block *b, long avg)
{
    if (!(b->flags & LT_BLOCK_PAD)) return avg;
    return (long)(b->padx > LT_PAD_MAX ? LT_PAD_MAX : b->padx) * avg / 8;
}

/* The widths of a Table block's columns (text only, without the cells'
   room) in w[], how many in *nc, for a column `width` wide. A table that
   fits keeps each column as wide as its widest cell; one that does not
   gives each column at least its longest word, and what is left in
   proportion to what each would still need. */
static void table_cols(const lt_content *c, const lt_block *b, long width, long avg, lt_measure m, void *ctx,
                       long *w, int *nc)
{
    long nat[LT_TABLE_COLS], minw[LT_TABLE_COLS], left, area, pad, avail, sumn = 0, summ = 0;
    lt_ulong ps = b->start, end = b->start + b->length;
    int j, n = 1;
    if (avg < 1) avg = 1;
    for (j = 0; j < LT_TABLE_COLS; j++) nat[j] = minw[j] = 0;
    while (ps <= end) {
        const char *q = ps < end ? (const char *)memchr(c->text + ps, '\n', end - ps) : NULL;
        lt_ulong pe = q ? (lt_ulong)(q - c->text) : end, cs = ps;
        for (j = 0; j < LT_TABLE_COLS; j++) {
            lt_ulong ce = cell_end(c, cs, pe), ws = cs, k;
            long cw = lt_width(c, cs, ce - cs, m, ctx);
            if (cw > nat[j]) nat[j] = cw;
            for (k = cs; k <= ce; k++)
                if (k == ce || c->text[k] == ' ') {
                    long ww = lt_width(c, ws, k - ws, m, ctx);
                    if (ww > minw[j]) minw[j] = ww;
                    ws = k + 1;
                }
            if (j + 1 > n) n = j + 1;
            if (ce >= pe) break;
            cs = ce + 1;
        }
        if (!q) break;
        ps = pe + 1;
    }
    lt_block_area(b, width, avg, &left, &area);
    pad = table_padx(b, avg);
    avail = area - (long)n * 2 * pad;
    for (j = 0; j < n; j++) {
        if (nat[j] < avg) nat[j] = avg;
        if (minw[j] < avg) minw[j] = avg;
        if (minw[j] > nat[j]) minw[j] = nat[j];
        sumn += nat[j];
        summ += minw[j];
    }
    if (avail < (long)n * avg) avail = (long)n * avg;
    for (j = 0; j < n; j++) {
        if (sumn <= avail) w[j] = nat[j];
        else if (summ >= avail) w[j] = minw[j] * avail / summ;
        else w[j] = minw[j] + (avail - summ) * (nat[j] - minw[j]) / (sumn - summ);
        if (w[j] < avg) w[j] = avg;
    }
    *nc = n;
}

/* A Table block's lines: each row's cells wrapped in their columns, then
   put in the order they are shown, line by line across the row. */
static int wrap_table(const lt_content *c, lt_ulong bi, long width, long avg, lt_measure m, void *ctx,
                      lt_line **ap, lt_ulong *cntp, lt_ulong *capp)
{
    const lt_block *b = &c->blocks[bi];
    long w[LT_TABLE_COLS], cx[LT_TABLE_COLS + 1], left, area, pad;
    lt_ulong ps = b->start, end = b->start + b->length;
    int nc, j;
    long row = 0;
    table_cols(c, b, width, avg, m, ctx, w, &nc);
    lt_block_area(b, width, avg, &left, &area);
    pad = table_padx(b, avg);
    cx[0] = left;
    for (j = 0; j < nc; j++) cx[j + 1] = cx[j] + w[j] + 2 * pad;
    while (ps <= end) {
        const char *q = ps < end ? (const char *)memchr(c->text + ps, '\n', end - ps) : NULL;
        lt_ulong pe = q ? (lt_ulong)(q - c->text) : end, cs = ps, first = *cntp, k;
        lt_ulong at[LT_TABLE_COLS], num[LT_TABLE_COLS], most = 0, v, tot;
        lt_line *tmp;
        for (j = 0; j < nc; j++) {
            lt_ulong ce = cs <= pe ? cell_end(c, cs, pe) : pe, i;
            unsigned al = (unsigned)(b->align >> (2 * j)) & 3;
            at[j] = *cntp;
            if (cs < ce && !wrap_range(c, cs, ce, w[j], m, ctx, cx[j] + pad, 0, (long)bi, ap, cntp, capp)) return 0;
            if (*cntp == at[j] && !push(ap, cntp, capp, cs < pe ? cs : pe, 0, 0, cx[j] + pad, (long)bi)) return 0;
            num[j] = *cntp - at[j];
            if (num[j] > most) most = num[j];
            for (i = at[j]; i < *cntp; i++) {
                lt_line *l = &(*ap)[i];
                long tw = al ? lt_width(c, l->start, l->shown, m, ctx) : 0;
                if (al == LT_ALIGN_RIGHT) l->x = cx[j] + pad + w[j] - tw;
                else if (al == LT_ALIGN_CENTER) l->x = cx[j] + pad + (w[j] - tw) / 2;
                l->cell = j;
                l->trow = row;
                l->cx0 = cx[j];
                l->cx1 = cx[j + 1];
                l->pstart = ps;
            }
            cs = ce < pe ? ce + 1 : pe + 1;
        }
        tot = *cntp - first;
        if (!(tmp = (lt_line *)lt_alloc(sizeof *tmp * tot))) return 0;
        /* Line v of every cell, left to right; the first of them is the
           one the others are beside. */
        for (v = 0, k = 0; v < most; v++) {
            int lead = 1;
            for (j = 0; j < nc; j++)
                if (v < num[j]) {
                    tmp[k] = (*ap)[at[j] + v];
                    tmp[k].cont = !lead;
                    lead = 0;
                    k++;
                }
        }
        memcpy(*ap + first, tmp, sizeof *tmp * tot);
        lt_free(tmp);
        row++;
        if (!q) break;
        ps = pe + 1;
    }
    return 1;
}

/* Wraps the paragraphs from `para` (a paragraph's start) to the end onto
   a, which holds *cnt lines already, final. */
static int wrap_from(const lt_content *c, long width, long avg, lt_measure m, void *ctx, lt_ulong para,
                     lt_line **ap, lt_ulong *cntp, lt_ulong *capp)
{
    lt_line *a = *ap;
    lt_ulong cnt = *cntp, cap = *capp, bi = 0, i, first_new = *cntp;
    const char *t = c->text;
    int ok = 0;

    while (para <= c->len) {
        const char *nlp = para < c->len ? (const char *)memchr(t + para, '\n', c->len - para) : NULL;
        lt_ulong e = nlp ? (lt_ulong)(nlp - t) : c->len, pos = para;
        long w = width, x = 0, blk = -1;
        int right = 0;

        while (bi < c->nblocks && c->blocks[bi].start + c->blocks[bi].length < para) bi++;
        if (bi < c->nblocks && c->blocks[bi].start == para && (c->blocks[bi].flags & LT_BLOCK_TABLE)) {
            /* A table is laid out whole, from its first row. */
            lt_ulong be = c->blocks[bi].start + c->blocks[bi].length;
            if (!wrap_table(c, bi, width, avg, m, ctx, &a, &cnt, &cap)) goto fail;
            if (be >= c->len) break;
            para = be + 1;
            continue;
        }
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
        if (!wrap_range(c, pos, e, w, m, ctx, x, right, blk, &a, &cnt, &cap)) goto fail;
        if (!nlp) break;
        para = e + 1;
    }

    for (i = first_new; i < cnt; ) {
        lt_ulong j;
        long wide = 0, left, area, x;
        const lt_block *b;
        if (a[i].block < 0 || !(c->blocks[a[i].block].flags & LT_BLOCK_RIGHT)
            || (c->blocks[a[i].block].flags & LT_BLOCK_TABLE)) { i++; continue; }
        b = &c->blocks[a[i].block];
        for (j = i; j < cnt && a[j].block == a[i].block; j++)
            if (a[j].x > wide) wide = a[j].x;
        lt_block_area(b, width, avg, &left, &area);
        x = wide < area ? left + area - wide : left;
        for (; i < j; i++) a[i].x = x;
    }
    ok = 1;
fail:
    *ap = a;
    *cntp = cnt;
    *capp = cap;
    return ok;
}

int lt_layout(const lt_content *c, long width, long avg, lt_measure m, void *ctx, lt_line **out, lt_ulong *n)
{
    lt_line *a = NULL;
    lt_ulong cnt = 0, cap = 0;
    if (!wrap_from(c, width, avg, m, ctx, 0, &a, &cnt, &cap)) { lt_free(a); return 0; }
    *out = a;
    *n = cnt;
    return 1;
}

static int same_block(const lt_block *x, const lt_block *y)
{
    return x->start == y->start && x->length == y->length && x->flags == y->flags && x->indent == y->indent
        && x->rindent == y->rindent && x->background == y->background && x->padx == y->padx && x->pady == y->pady
        && x->align == y->align && x->rule == y->rule && x->head == y->head;
}

/* The font flags of the text at off, and in *next the first offset after
   it where they may change. */
static lt_ulong font_at(const lt_content *c, lt_ulong off, lt_ulong *next)
{
    lt_ulong i = span_from(c, off);
    if (i < c->nspans && c->spans[i].start <= off) {
        *next = c->spans[i].start + c->spans[i].length;
        return c->spans[i].flags & LT_FONT_FLAGS;
    }
    *next = i < c->nspans ? c->spans[i].start : c->len;
    return 0;
}

lt_ulong lt_font_prefix(const lt_content *a, const lt_content *b, lt_ulong upto)
{
    lt_ulong pos = 0;
    if (!a->any_font && !b->any_font) return upto;
    while (pos < upto) {
        lt_ulong na, nb, fa = font_at(a, pos, &na), fb = font_at(b, pos, &nb);
        if (fa != fb) return pos;
        if (na <= pos) na = pos + 1;
        if (nb <= pos) nb = pos + 1;
        pos = na < nb ? na : nb;
    }
    return upto;
}

int lt_layout_keep(const lt_content *c, long width, long avg, lt_measure m, void *ctx,
                   const lt_content *old, const lt_line *ol, lt_ulong on, lt_ulong keep,
                   lt_line **out, lt_ulong *n)
{
    lt_line *a = NULL;
    lt_ulong cnt = 0, cap = 0, p = keep, i, k;
    if (!old || !ol || !on || !keep) return lt_layout(c, width, avg, m, ctx, out, n);
    if (p > c->len) p = c->len;
    if (p > old->len) p = old->len;
    /* The same bytes in another font wrap at other places. */
    p = lt_font_prefix(old, c, p);
    while (p && c->text[p - 1] != '\n') p--;
    /* A block that differs, or that goes on past p, is laid out again from
       its start: a Right block's lines all move with its widest. */
    for (i = 0; i < c->nblocks || i < old->nblocks; i++) {
        const lt_block *nb = i < c->nblocks ? &c->blocks[i] : NULL;
        const lt_block *obk = i < old->nblocks ? &old->blocks[i] : NULL;
        lt_ulong s = nb && obk ? (nb->start < obk->start ? nb->start : obk->start) : nb ? nb->start : obk->start;
        if (s >= p) break;
        if (!nb || !obk || !same_block(nb, obk) || nb->start + nb->length >= p) { p = s; break; }
    }
    if (!p) return lt_layout(c, width, avg, m, ctx, out, n);
    /* Old lines before p are the same lines. */
    for (k = 0; k < on && ol[k].pstart < p; k++) ;
    cap = k + 32;
    if (!(a = (lt_line *)lt_alloc(sizeof *a * cap))) return 0;
    memcpy(a, ol, sizeof *a * k);
    cnt = k;
    if (!wrap_from(c, width, avg, m, ctx, p, &a, &cnt, &cap)) { lt_free(a); return 0; }
    *out = a;
    *n = cnt;
    return 1;
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
    if (c->blocks[b].flags & LT_BLOCK_PAD)
        pad = (long)(c->blocks[b].padx > LT_PAD_MAX ? LT_PAD_MAX : c->blocks[b].padx) * avg / 8;
    lt_block_area(&c->blocks[b], width, avg, &left, &area);
    *x0 = (c->blocks[b].flags & LT_BLOCK_RIGHT ? l[*first].x : left) - pad;
    *x1 = left + area + pad;
    return 1;
}

/* Line i holds Large text. */
static int has_large(const lt_content *c, const lt_line *l)
{
    lt_ulong i = span_from(c, l->start), e = l->start + (l->shown ? l->shown : l->len);
    for (; i < c->nspans && c->spans[i].start < e; i++)
        if (c->spans[i].flags & LT_LARGE) return 1;
    return 0;
}

/* The next line that is not beside another, from i + 1; n when none. */
static lt_ulong next_lead(const lt_line *l, lt_ulong n, lt_ulong i)
{
    for (i++; i < n && l[i].cont; i++) ;
    return i;
}

void lt_heights(const lt_content *c, lt_line *l, lt_ulong n, long lineh, long largeh, long empty)
{
    lt_ulong i, prev = n;
    long y = 0;
    if (lineh < 1) lineh = 1;
    if (largeh < lineh) largeh = lineh;
    if (empty < 1 || empty > lineh) empty = lineh;
    for (i = 0; i < n; i++) l[i].large = l[i].len && c->any_font && has_large(c, &l[i]);
    for (i = 0; i < n; i++) {
        long h, ty = 0;
        lt_ulong j;
        if (l[i].cont && i) continue;
        h = l[i].len || l[i].cell >= 0 ? lineh : empty;
        for (j = i; j < n && (j == i || l[j].cont); j++)
            if (l[j].large) h = largeh;
        if (!l[i].len && l[i].cell < 0 && l[i].block >= 0 && (c->blocks[l[i].block].flags & LT_BLOCK_SPACE)) {
            lt_ulong e = c->blocks[l[i].block].pady;
            if (e > LT_SPACE_MAX) e = LT_SPACE_MAX;
            h = (long)e * lineh / 8;
            if (h < 1) h = 1;
        }
        if (l[i].cell >= 0) {
            /* A table row's room above its first line and below its last. */
            const lt_block *b = &c->blocks[l[i].block];
            lt_ulong nx = next_lead(l, n, i);
            long pad = (long)(b->pady > 8 ? 8 : b->pady) * lineh / 8;
            if (prev == n || l[prev].block != l[i].block || l[prev].trow != l[i].trow) { ty = pad; h += pad; }
            if (nx >= n || l[nx].block != l[i].block || l[nx].trow != l[i].trow) h += pad;
        }
        for (j = i; j < n && (j == i || l[j].cont); j++) {
            l[j].y = y;
            l[j].h = h;
            l[j].ty = ty;
        }
        y += h;
        prev = i;
    }
}

lt_ulong lt_last_top(const lt_line *l, lt_ulong n, long height)
{
    lt_ulong k;
    long sum;
    if (!n) return 0;
    k = n - 1;
    sum = l[k].y + l[k].h;               /* the text's bottom */
    while (k > 0 && sum - l[k - 1].y <= height) k--;
    while (k > 0 && l[k].cont) k--;
    return k;
}

lt_ulong lt_fit_lines(const lt_line *l, lt_ulong n, lt_ulong first, long height)
{
    lt_ulong k = 0;
    long top;
    if (first >= n) return 1;
    top = l[first].y;
    while (first + k < n && l[first + k].y + l[first + k].h - top <= height) k++;
    return k ? k : 1;
}

lt_ulong lt_line_at_y(const lt_line *l, lt_ulong n, lt_ulong first, long dy)
{
    lt_ulong i;
    long top;
    if (first >= n) return n;
    top = l[first].y;
    for (i = first; i < n; i++)
        if (dy < l[i].y - top + l[i].h) return i;
    return n;
}

long lt_block_pady(const lt_block *b, long lineh)
{
    lt_ulong p;
    if (!(b->flags & LT_BLOCK_PAD)) return 0;
    p = b->pady > 8 ? 8 : b->pady;
    return (long)p * lineh / 8;
}

lt_ulong lt_line_of(const lt_line *l, lt_ulong n, lt_ulong off)
{
    lt_ulong lo = 0, hi = n, i, best;
    if (!n) return 0;
    while (hi - lo > 1) {
        lt_ulong mid = lo + (hi - lo) / 2;
        if (l[mid].pstart <= off) lo = mid; else hi = mid;
    }
    if (l[lo].cell < 0) return lo;
    /* In a table row: back to its first line, then the line whose bytes
       hold off, or the last one starting before it. */
    while (lo > 0 && l[lo - 1].cell >= 0 && l[lo - 1].block == l[lo].block && l[lo - 1].trow == l[lo].trow) lo--;
    best = lo;
    for (i = lo; i < n && l[i].cell >= 0 && l[i].block == l[lo].block && l[i].trow == l[lo].trow; i++) {
        if (l[i].start <= off && off < l[i].start + (l[i].len ? l[i].len : 1)) return i;
        if (l[i].start <= off && l[i].start >= l[best].start) best = i;
    }
    return best;
}

lt_ulong lt_line_at_x(const lt_line *l, lt_ulong n, lt_ulong i, long x)
{
    lt_ulong j;
    if (i >= n || l[i].cell < 0) return i;
    for (j = i; j < n && (j == i || l[j].cont); j++)
        if (x >= l[j].cx0 && x < l[j].cx1) return j;
    return i;
}

void lt_block_lines(const lt_line *l, lt_ulong n, lt_ulong i, lt_ulong *first, lt_ulong *last)
{
    lt_ulong a = i, b = i;
    if (i >= n) { *first = *last = i; return; }
    if (l[i].block >= 0) {
        while (a > 0 && l[a - 1].block == l[i].block) a--;
        while (b + 1 < n && l[b + 1].block == l[i].block) b++;
    }
    *first = a;
    *last = b;
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
    if (c->utf8) {
        /* cluster by cluster: x is on the cluster whose span holds it */
        lt_ulong a = line->start, end = line->start + line->shown;
        long w0 = 0;
        while (a < end) {
            lt_ulong b = zt_next_cluster(c->text, c->len, a);
            long w1;
            if (b > end) b = end;
            w1 = lt_width(c, line->start, b - line->start, m, ctx);
            if (x < w1) {
                *caret = x - w0 >= w1 - x ? b : a;
                return (long)a;
            }
            w0 = w1;
            a = b;
        }
        *caret = end;
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
    /* In a table a cell's next line can follow lines of other cells. */
    for (; i < nl && (l[i].start < s->start + s->length || l[i].cell >= 0); i++) {
        if (l[i].start >= s->start + s->length) continue;
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
