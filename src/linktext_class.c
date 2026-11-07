/* SPDX-License-Identifier: MIT */
/* Linktext: see linktext.h. The class on top of linktext_core.c: drawing,
 * input and the MUI interface. Built two ways: as an external class from
 * its .conf file, or compiled into a program with LINKTEXT_EMBEDDED, which
 * adds the dispatcher (zk_linktext.c creates the class from it). */
#define MUIMASTER_YES_INLINE_STDARG

#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <devices/inputevent.h>
#include <devices/rawkeycodes.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <libraries/iffparse.h>
#include <libraries/mui.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/diskfont.h>
#include <graphics/text.h>
#include <clib/alib_protos.h>
#include <string.h>

#define __IFFPARSE_NOLIBBASE__
#define __IFFPARSE_LIBBASE iff_base
#include <proto/iffparse.h>

#ifdef LINKTEXT_EMBEDDED
#include "linktext.h"
#else
#include <zune/linktext.h>
#endif
#include "linktext_core.h"
#include "linktext_private.h"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif

#define DRAG_START 4              /* pixels before a press becomes a selection */
#define WHEEL_LINES 3
#define HELP_COLUMNS 64

/* Set by tests of a build with LINKTEXT_TEST: the next n allocations
   succeed, then one fails. */
#ifdef LINKTEXT_TEST
long linktext_fail_after = -1;
#endif

void *lt_alloc(size_t n)
{
#ifdef LINKTEXT_TEST
    if (linktext_fail_after == 0) { linktext_fail_after = -1; return NULL; }
    if (linktext_fail_after > 0) linktext_fail_after--;
#endif
    return AllocVec(n, MEMF_ANY);
}

void lt_free(void *p) { if (p) FreeVec(p); }

/* ---------------------------------------------------------------- measure */

/* What measuring and drawing need: the RastPort and the two fonts. */
struct lt_rp
{
    struct RastPort *rp;
    struct TextFont *normal, *fixed, *large;
};

static void use_font(struct lt_rp *r, lt_ulong flags)
{
    struct TextFont *f = flags & LT_FIXED ? r->fixed : flags & LT_LARGE ? r->large : r->normal;
    if (r->rp->Font != f) SetFont(r->rp, f);
}

static long measure(void *ctx, const char *s, lt_ulong n, lt_ulong flags)
{
    struct lt_rp *r = (struct lt_rp *)ctx;
    struct RastPort *rp = r->rp;
    long w;
    use_font(r, flags);
    if (flags & LT_BOLD) {
        ULONG old = SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
        w = TextLength(rp, (STRPTR)s, n);
        SetSoftStyle(rp, old, AskSoftStyle(rp));
        return w;
    }
    return TextLength(rp, (STRPTR)s, n);
}

#define AVG_SAMPLE "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"

/* The RastPort in the normal font, or NULL outside Show and Hide. The
   average character width comes from it the first time after Setup. */
static struct RastPort *ready(struct Linktext_DATA *d, Object *obj, struct lt_rp *r)
{
    r->rp = _rp(obj);
    r->normal = _font(obj);
    r->fixed = d->fixed ? d->fixed : r->normal;
    r->large = d->large ? d->large : r->normal;
    if (!r->rp) return NULL;
    SetFont(r->rp, r->normal);
    if (!d->avg) {
        d->avg = TextLength(r->rp, (STRPTR)AVG_SAMPLE, sizeof AVG_SAMPLE - 1) / (LONG)(sizeof AVG_SAMPLE - 1);
        if (d->avg < 1) d->avg = 1;
    }
    return r->rp;
}

/* MaxColumns average characters, or the whole width when that is less. */
static LONG column_width(struct Linktext_DATA *d, Object *obj)
{
    LONG w = _mwidth(obj);
    if (d->max_columns && d->avg > 0 && d->max_columns < (ULONG)(w / d->avg))
        w = (LONG)d->max_columns * d->avg;
    return w;
}

/* The column's left edge in the window, for the lines laid out now. */
static LONG column_left(struct Linktext_DATA *d, Object *obj)
{
    LONG w = d->laid_w >= 0 ? d->laid_w : column_width(d, obj);
    return _mleft(obj) + (_mwidth(obj) > w ? (_mwidth(obj) - w) / 2 : 0);
}

/* Whole lines from First that fit; lines have their heights once wrapped. */
static LONG visible(struct Linktext_DATA *d, Object *obj)
{
    LONG v;
    if (d->lines && d->nlines && d->first < (LONG)d->nlines)
        return (LONG)lt_fit_lines(d->lines, d->nlines, (lt_ulong)d->first, _mheight(obj));
    v = d->lineh ? _mheight(obj) / d->lineh : 1;
    return v > 0 ? v : 1;
}

/* Line i's top in the window, from the line at the top. */
static LONG line_top(struct Linktext_DATA *d, Object *obj, LONG i)
{
    LONG f = d->first < (LONG)d->nlines ? d->first : 0;
    if (!d->lines || !d->nlines) return _mtop(obj) + (i - d->first) * d->lineh;
    if (i >= (LONG)d->nlines)
        return _mtop(obj) + d->lines[d->nlines - 1].y + d->lines[d->nlines - 1].h - d->lines[f].y
               + (i - (LONG)d->nlines) * d->lineh;
    return _mtop(obj) + d->lines[i].y - d->lines[f].y;
}

static LONG empty_height(struct Linktext_DATA *d)
{
    ULONG e = d->empty8 && d->empty8 <= 8 ? d->empty8 : 8;
    LONG h = d->lineh * (LONG)e / 8;
    return h > 0 ? h : 1;
}

/* Wraps c for the object's column, with the lines' heights. Lines laid out
   for the content shown now at this width are kept for the bytes both
   start with (a reply growing at the end wraps only its end). */
static int wrap(struct Linktext_DATA *d, Object *obj, const lt_content *c, struct lt_rp *r,
                lt_line **lines, lt_ulong *n)
{
    LONG w = column_width(d, obj);
    if (d->content && d->content != c && d->lines && d->nlines && d->laid_w == w) {
        if (!lt_layout_keep(c, w, d->avg, measure, r, d->content, d->lines, d->nlines,
                            lt_common_prefix(d->content, c), lines, n)) return 0;
    } else if (!lt_layout(c, w, d->avg, measure, r, lines, n)) return 0;
    lt_heights(c, *lines, *n, d->lineh, d->largeh, empty_height(d));
    return 1;
}

/* The scroller counts pixels: lines are not all of one height, and a count
   of lines that fit changes with the place. */
static void update_prop(struct Linktext_DATA *d, Object *obj)
{
    LONG total, top;
    if (!d->scroller || !d->shown) return;
    if (!d->lines || !d->nlines) {
        SetAttrs(d->scroller, MUIA_NoNotify, TRUE, MUIA_Prop_Entries, 0, MUIA_Prop_Visible, _mheight(obj),
                 MUIA_Prop_First, 0, TAG_DONE);
        return;
    }
    total = d->lines[d->nlines - 1].y + d->lines[d->nlines - 1].h;
    top = d->first < (LONG)d->nlines ? d->lines[d->first].y : 0;
    SetAttrs(d->scroller, MUIA_NoNotify, TRUE,
             MUIA_Prop_Entries, total,
             MUIA_Prop_Visible, _mheight(obj),
             MUIA_Prop_First, top,
             MUIA_Prop_DeltaFactor, d->lineh > 0 ? d->lineh : 1,   /* an arrow: a line */
             TAG_DONE);
}

/* The first line whose top is at or below px. */
static LONG line_from_px(struct Linktext_DATA *d, LONG px)
{
    lt_ulong lo = 0, hi = d->nlines;
    while (lo < hi) {
        lt_ulong mid = lo + (hi - lo) / 2;
        if (d->lines[mid].y < px) lo = mid + 1; else hi = mid;
    }
    return (LONG)lo;
}

static void clamp_first(struct Linktext_DATA *d, Object *obj)
{
    LONG max = d->lines && d->nlines ? (LONG)lt_last_top(d->lines, d->nlines, _mheight(obj))
                                     : (LONG)d->nlines - visible(d, obj);
    if (d->first > max) d->first = max;
    if (d->first < 0) d->first = 0;
    if (d->lines) while (d->first > 0 && d->first < (LONG)d->nlines && d->lines[d->first].cont) d->first--;
}

/* The last whole lines were shown at the last draw: a view made smaller or
   larger then stays at its end (a panel opened under a conversation that
   follows its end). */
static BOOL was_at_end(struct Linktext_DATA *d)
{
    return d->content && d->vis && d->first + (LONG)d->vis >= (LONG)d->nlines;
}

static void keep_end(struct Linktext_DATA *d, Object *obj)
{
    d->first = (LONG)d->nlines;
    clamp_first(d, obj);
}

/* Wraps for the current width and font. Needs a RastPort, so only between
   Show and Hide. The top line keeps showing the same text. */
static int layout(struct Linktext_DATA *d, Object *obj)
{
    struct lt_rp r;
    lt_line *lines = NULL;
    lt_ulong n = 0;
    ULONG anchor;
    LONG w;
    BOOL has_anchor;
    if (!d->content || !ready(d, obj, &r)) return 0;
    w = column_width(d, obj);
    if (!wrap(d, obj, d->content, &r, &lines, &n)) return 0;
    SetFont(r.rp, r.normal);
    /* Lines not yet wrapped keep the line number First was set to. */
    has_anchor = d->lines && d->first < (LONG)d->nlines;
    anchor = has_anchor ? d->lines[d->first].start : 0;
    lt_free(d->lines);
    d->lines = lines;
    d->nlines = n;
    d->laid_w = w;
    if (has_anchor) d->first = lt_line_of(lines, n, anchor);
    clamp_first(d, obj);
    return 1;
}

/* ------------------------------------------------------------------- draw */

static void draw_line(struct Linktext_DATA *d, Object *obj, struct lt_rp *r, ULONG i, LONG y)
{
    struct RastPort *rp = r->rp;
    const lt_line *l = &d->lines[i];
    lt_ulong pos = l->start, fl = 0, n;
    LONG x = column_left(d, obj) + l->x, th = l->large ? d->largeh : d->lineh, base;
    if (!l->len) return;                       /* an empty paragraph */
    y += l->ty;                                /* a table row's room */
    base = y + (l->large ? d->large_base : d->base);
    ULONG sa = d->sel_anchor < d->sel_caret ? d->sel_anchor : d->sel_caret;
    ULONG sb = d->sel_anchor < d->sel_caret ? d->sel_caret : d->sel_anchor;

    while ((n = lt_run(d->content, l, pos, sa, sb, &fl)) != 0) {
        LONG w = measure(r, d->content->text + pos, n, fl & LT_FONT_FLAGS);
        UBYTE style = 0;
        if (fl & LT_RUN_SEL) {
            SetAPen(rp, _pens(obj)[MPEN_FILL]);
            RectFill(rp, x, y, x + w - 1, y + th - 1);
        } else if (fl & LT_SHADE) {
            /* A little wider than the text where a space is next to it. */
            const char *t = d->content->text;
            LONG a = pos > l->start && t[pos - 1] == ' ' ? 2 : 0;
            LONG b = pos + n < l->start + l->shown && t[pos + n] == ' ' ? 2 : 0;
            SetAPen(rp, d->pen[3] >= 0 ? (ULONG)d->pen[3] : _pens(obj)[MPEN_HALFSHINE]);
            RectFill(rp, x - a, y + 1, x + w - 1 + b, y + th - 2);
        }
        /* Links stay in the text pen unless an accent colour is set: the
           Mark pen equals the list background in stock settings (AROS
           2026.09), and the underline tells them apart. */
        if (fl & LT_RUN_SEL) SetAPen(rp, _pens(obj)[MPEN_SHINE]);
        else if ((fl & (LT_ACCENT | LT_RUN_LINK)) && d->pen[2] >= 0) SetAPen(rp, (ULONG)d->pen[2]);
        else if (fl & LT_DIM) SetAPen(rp, d->pen[1] >= 0 ? (ULONG)d->pen[1] : _pens(obj)[d->dim_pen]);
        else SetAPen(rp, d->pen[0] >= 0 ? (ULONG)d->pen[0] : _pens(obj)[MPEN_TEXT]);
        if (fl & LT_BOLD) style |= FSF_BOLD;
        if ((fl & LT_RUN_LINK) && !(fl & LT_NOUNDERLINE)) style |= FSF_UNDERLINED;
        SetSoftStyle(rp, style, AskSoftStyle(rp));
        Move(rp, x, base);
        Text(rp, (STRPTR)d->content->text + pos, n);
        SetSoftStyle(rp, FS_NORMAL, AskSoftStyle(rp));
        x += w;
        pos += n;
    }
    use_font(r, 0);
}

/* A pen for 0xrrggbb on the object's screen, -1 when there is none. */
static LONG obtain_pen(Object *obj, ULONG rgb)
{
    struct ColorMap *cm = _screen(obj) ? _screen(obj)->ViewPort.ColorMap : NULL;
    ULONG r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
    if (!cm) return -1;
    return ObtainBestPen(cm, r * 0x01010101UL, g * 0x01010101UL, b * 0x01010101UL,
                         OBP_Precision, PRECISION_IMAGE, OBP_FailIfBad, FALSE, TAG_DONE);
}

static void release_pens(struct Linktext_DATA *d, Object *obj)
{
    struct ColorMap *cm = _screen(obj) ? _screen(obj)->ViewPort.ColorMap : NULL;
    ULONG i;
    for (i = 0; i < 4; i++) {
        if (d->pen[i] >= 0 && cm) ReleasePen(cm, (ULONG)d->pen[i]);
        d->pen[i] = -1;
    }
    for (i = 0; i < d->nbg; i++)
        if (d->bg[i].pen >= 0 && cm) ReleasePen(cm, (ULONG)d->bg[i].pen);
    d->nbg = 0;
}

static void obtain_pens(struct Linktext_DATA *d, Object *obj)
{
    ULONG i;
    for (i = 0; i < 4; i++)
        d->pen[i] = d->rgb[i] & 0x80000000UL ? obtain_pen(obj, d->rgb[i] & 0xffffffUL) : -1;
}

/* The pen a block's Background asks for. RGB pens are obtained the first
   time one is drawn and kept until Cleanup; past eight colours, the
   nearest of the screen's current colours. */
static ULONG block_pen(struct Linktext_DATA *d, Object *obj, ULONG bg)
{
    ULONG i;
    if (!(bg & 0x80000000UL))
        return _pens(obj)[bg && bg - 1 < MPEN_COUNT ? bg - 1 : MPEN_HALFSHINE];
    bg &= 0xffffffUL;
    for (i = 0; i < d->nbg; i++)
        if (d->bg[i].rgb == bg) return d->bg[i].pen >= 0 ? (ULONG)d->bg[i].pen : _pens(obj)[MPEN_HALFSHINE];
    if (d->nbg < 8) {
        d->bg[d->nbg].rgb = bg;
        d->bg[d->nbg].pen = obtain_pen(obj, bg);
        i = d->nbg++;
        return d->bg[i].pen >= 0 ? (ULONG)d->bg[i].pen : _pens(obj)[MPEN_HALFSHINE];
    }
    {
        struct ColorMap *cm = _screen(obj) ? _screen(obj)->ViewPort.ColorMap : NULL;
        LONG p = -1;
        if (cm) p = FindColor(cm, ((bg >> 16) & 0xff) * 0x01010101UL, ((bg >> 8) & 0xff) * 0x01010101UL,
                              (bg & 0xff) * 0x01010101UL, -1);
        return p >= 0 ? (ULONG)p : _pens(obj)[MPEN_HALFSHINE];
    }
}

/* Fill boxes of the blocks on lines first..last, and of the blocks next to
   them whose room reaches into them, before their text. Drawn whole: the
   clip keeps them to the lines being drawn. */
static void draw_fills(struct Linktext_DATA *d, Object *obj, LONG first, LONG last)
{
    struct RastPort *rp = _rp(obj);
    LONG i, left = column_left(d, obj);
    long done = -1;
    if (d->laid_w < 0 || !d->lines) return;
    if (first > 0) first--;
    last++;
    for (i = first; i <= last && i < (LONG)d->nlines; i++) {
        long b = d->lines[i].block;
        const lt_block *bl;
        lt_ulong f0, f1;
        long x0, x1, pady;
        LONG top, bottom;
        if (b < 0 || b == done) continue;
        done = b;
        bl = &d->content->blocks[b];
        if (!(bl->flags & LT_BLOCK_FILL) || (bl->flags & LT_BLOCK_TABLE)) continue;
        if (!lt_block_box(d->content, d->lines, d->nlines, (lt_ulong)b, d->laid_w, d->avg, &f0, &f1, &x0, &x1))
            continue;
        pady = lt_block_pady(bl, d->lineh);
        top = line_top(d, obj, (LONG)f0) - pady;
        bottom = line_top(d, obj, (LONG)f1) + d->lines[f1].h - 1 + pady;
        if (bottom < _mtop(obj) || top > _mbottom(obj)) continue;
        if (top < _mtop(obj)) top = _mtop(obj);
        if (bottom > _mbottom(obj)) bottom = _mbottom(obj);
        SetAPen(rp, block_pen(d, obj, bl->background));
        RectFill(rp, left + x0, top, left + x1 - 1, bottom);
    }
}

/* Tables on lines first..last: the body's background, the header row's,
   then the lines between rows and columns and around the table. Each table
   is drawn whole; the clip keeps it to the lines being drawn. */
static void draw_tables(struct Linktext_DATA *d, Object *obj, LONG first, LONG last)
{
    struct RastPort *rp = _rp(obj);
    LONG left = column_left(d, obj), i;
    long done = -1;
    if (d->laid_w < 0 || !d->lines) return;
    for (i = first > 0 ? first - 1 : 0; i <= last + 1 && i < (LONG)d->nlines; i++) {
        const lt_block *bl;
        lt_ulong a, z, k;
        long b = d->lines[i].block, x0, x1, trow;
        LONG top, bottom;
        ULONG rule;
        if (d->lines[i].cell < 0 || b == done) continue;
        done = b;
        bl = &d->content->blocks[b];
        lt_block_lines(d->lines, d->nlines, (lt_ulong)i, &a, &z);
        x0 = d->lines[a].cx0;
        x1 = d->lines[a].cx1;
        for (k = a; k <= z; k++) if (d->lines[k].cx1 > x1) x1 = d->lines[k].cx1;
        top = line_top(d, obj, (LONG)a);
        bottom = line_top(d, obj, (LONG)z) + d->lines[z].h - 1;
        if (bottom < _mtop(obj) || top > _mbottom(obj)) continue;
        if (bl->flags & LT_BLOCK_FILL) {
            SetAPen(rp, block_pen(d, obj, bl->background));
            RectFill(rp, left + x0, top, left + x1 - 1, bottom);
        }
        if (bl->flags & LT_BLOCK_HEAD) {
            LONG hb = top;
            for (k = a; k <= z && d->lines[k].trow == 0; k++)
                hb = line_top(d, obj, (LONG)k) + d->lines[k].h - 1;
            SetAPen(rp, bl->head ? block_pen(d, obj, bl->head)
                                 : d->pen[3] >= 0 ? (ULONG)d->pen[3] : _pens(obj)[MPEN_HALFSHINE]);
            RectFill(rp, left + x0, top, left + x1 - 1, hb);
        }
        rule = bl->rule ? block_pen(d, obj, bl->rule) : _pens(obj)[MPEN_HALFSHADOW];
        SetAPen(rp, rule);
        /* between rows */
        trow = d->lines[a].trow;
        for (k = a; k <= z; k++) {
            LONG lt = line_top(d, obj, (LONG)k);
            if (d->lines[k].trow != trow) {
                Move(rp, left + x0, lt);
                Draw(rp, left + x1 - 1, lt);
                if (trow == 0 && (bl->flags & LT_BLOCK_HEAD)) {   /* the header's line is heavier */
                    Move(rp, left + x0, lt - 1);
                    Draw(rp, left + x1 - 1, lt - 1);
                }
                trow = d->lines[k].trow;
            }
        }
        /* between columns: the left edges of the first row's cells */
        for (k = a; k <= z && d->lines[k].trow == d->lines[a].trow; k++)
            if (d->lines[k].cx0 > x0) {
                Move(rp, left + d->lines[k].cx0, top);
                Draw(rp, left + d->lines[k].cx0, bottom);
            }
        /* around */
        Move(rp, left + x0, top);
        Draw(rp, left + x1 - 1, top);
        Draw(rp, left + x1 - 1, bottom);
        Draw(rp, left + x0, bottom);
        Draw(rp, left + x0, top);
    }
}

/* The keyboard's link gets a frame on each of its fragments. */
static void draw_focus(struct Linktext_DATA *d, Object *obj, struct lt_rp *r, LONG first, LONG last)
{
    struct RastPort *rp = r->rp;
    lt_ulong line, f;
    long x0, x1;
    LONG left = column_left(d, obj);
    if (d->focus < 0 || !d->lines) return;
    for (f = 0; lt_link_fragment(d->content, d->lines, d->nlines, d->focus, f, measure, r, &line, &x0, &x1); f++) {
        LONG y;
        if ((LONG)line < first || (LONG)line > last) continue;
        y = line_top(d, obj, (LONG)line) + d->lines[line].ty;
        SetAPen(rp, _pens(obj)[MPEN_TEXT]);
        Move(rp, left + x0, y);
        Draw(rp, left + x1 - 1, y);
        Draw(rp, left + x1 - 1, y + d->lineh - 1);
        Draw(rp, left + x0, y + d->lineh - 1);
        Draw(rp, left + x0, y);
    }
    use_font(r, 0);
}

/* Lines first..last only (dirty_first >= 0) or all of them. Zune tells a
   class what to draw only through the superclass's reply, so a partial draw
   is an ordinary object draw under a clip made first: the Area then repaints
   the background of just those lines, skins' custom frames included. */
IPTR Linktext__MUIM_Draw(Class *cl, Object *obj, struct MUIP_Draw *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    struct lt_rp r;
    APTR clip = NULL, tclip;
    BOOL partial = d->dirty_first >= 0;
    LONG i, first = d->first, last = d->first + visible(d, obj), top = _mtop(obj), height = _mheight(obj);

    if (partial) {
        if (d->dirty_first > first) first = d->dirty_first;
        if (d->dirty_last < last) last = d->dirty_last;
        /* A table's lines are not in byte order: its rows are drawn whole. */
        if (d->lines && first < (LONG)d->nlines && first <= last) {
            lt_ulong a, b;
            if (d->lines[first].cell >= 0) { lt_block_lines(d->lines, d->nlines, (lt_ulong)first, &a, &b); first = (LONG)a; }
            while (first > 0 && d->lines[first].cont) first--;
            if (last < (LONG)d->nlines && d->lines[last].cell >= 0) { lt_block_lines(d->lines, d->nlines, (lt_ulong)last, &a, &b); last = (LONG)b; }
            while (last + 1 < (LONG)d->nlines && d->lines[last + 1].cont) last++;
            if (first < d->first) first = d->first;
        }
        top = line_top(d, obj, first);
        height = line_top(d, obj, last + 1) - top;
        if (top + height > _mtop(obj) + _mheight(obj)) height = _mtop(obj) + _mheight(obj) - top;
        if (height <= 0) return 0;
        clip = MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), top, _mwidth(obj), height);
    }
    DoSuperMethodA(cl, obj, (Msg)msg);
    if (was_at_end(d) && d->vis != (ULONG)visible(d, obj)) keep_end(d, obj);
    d->vis = visible(d, obj);
    if ((msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) && d->content && ready(d, obj, &r)) {
        /* The window can be drawn at a new size before Show when only its
           height changed; a failed wrap keeps the lines there are. */
        if (d->laid_w != column_width(d, obj)) {
            layout(d, obj);
            if (!partial) { first = d->first; last = d->first + visible(d, obj); }
        }
        tclip = MUI_AddClipping(muiRenderInfo(obj), _mleft(obj), top, _mwidth(obj), height);
        /* Text over the background: a JAM2 left by whatever drew last fills
           a rectangle behind every run. */
        SetDrMd(r.rp, JAM1);
        if (d->lines) {
            draw_fills(d, obj, first, last);
            draw_tables(d, obj, first, last);
            for (i = first; i <= last && i < (LONG)d->nlines; i++)
                draw_line(d, obj, &r, i, line_top(d, obj, i));
        }
        draw_focus(d, obj, &r, first, last);
        MUI_RemoveClipping(muiRenderInfo(obj), tclip);
    }
    /* The handle of the first clip can be NULL: it is the region before it. */
    if (partial) MUI_RemoveClipping(muiRenderInfo(obj), clip);
    return 0;
}

static void redraw_lines(struct Linktext_DATA *d, Object *obj, LONG first, LONG last)
{
    if (first > last) { LONG t = first; first = last; last = t; }
    if (!d->shown || last < d->first || first > d->first + visible(d, obj)) return;
    d->dirty_first = first;
    d->dirty_last = last;
    MUI_Redraw(obj, MADF_DRAWOBJECT);
    d->dirty_first = -1;
}

static void redraw_all(struct Linktext_DATA *d, Object *obj)
{
    redraw_lines(d, obj, d->first, d->first + visible(d, obj));
}

static void scroll_to(struct Linktext_DATA *d, Object *obj, LONG first)
{
    LONG old = d->first;
    d->first = first;
    /* Lines beside another (a table's cells) are never at the top: on to
       the next line across going down, back to its first going up. */
    if (d->lines && first > old)
        while (d->first < (LONG)d->nlines && d->lines[d->first].cont) d->first++;
    clamp_first(d, obj);
    if (d->first == old) return;
    update_prop(d, obj);
    redraw_all(d, obj);
}

/* -------------------------------------------------------------- clipboard */

static BOOL clip_write(const char *s, ULONG n)
{
    struct Library *iff_base = OpenLibrary((CONST_STRPTR)"iffparse.library", 39);
    struct IFFHandle *iff;
    BOOL ok = FALSE;
    if (!iff_base) return FALSE;
    if ((iff = AllocIFF())) {
        if ((iff->iff_Stream = (IPTR)OpenClipboard(0))) {
            InitIFFasClip(iff);
            if (!OpenIFF(iff, IFFF_WRITE)) {
                ok = !PushChunk(iff, MAKE_ID('F','T','X','T'), MAKE_ID('F','O','R','M'), IFFSIZE_UNKNOWN)
                     && !PushChunk(iff, 0, MAKE_ID('C','H','R','S'), (LONG)n)
                     && WriteChunkBytes(iff, (APTR)s, (LONG)n) == (LONG)n
                     && !PopChunk(iff)
                     && !PopChunk(iff);
                CloseIFF(iff);
            }
            CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
        }
        FreeIFF(iff);
    }
    CloseLibrary(iff_base);
    return ok;
}

static void copy_selection(struct Linktext_DATA *d)
{
    ULONG a = d->sel_anchor < d->sel_caret ? d->sel_anchor : d->sel_caret;
    ULONG b = d->sel_anchor < d->sel_caret ? d->sel_caret : d->sel_anchor;
    if (d->content && a < b) clip_write(d->content->text + a, b - a);
}

/* ----------------------------------------------------------------- action */

/* Last thing in any event: the hook may replace the content or close the
   window, so the caller does not look at `d` afterwards. The reference
   keeps Target valid through that. */
static void do_action(struct Linktext_DATA *d, Object *obj, lt_content *c, ULONG link, ULONG action, ULONG qual)
{
    const lt_span *s = lt_link(c, link);
    struct Hook *h = d->hook;
    struct MUI_LinktextAction a;
    IPTR handled = FALSE;
    if (!s) return;
    a.Action = action;
    a.Link = link;
    a.ID = s->id;
    a.Target = (CONST_STRPTR)s->target;
    a.TargetLength = s->target_len;
    a.Qualifier = qual;
    lt_content_ref(c);
    if (h) handled = CallHookPkt(h, obj, &a);
    if (!handled && action == MUIV_Linktext_Action_Copy) clip_write(s->target, s->target_len);
    lt_content_unref(c);
}

/* ------------------------------------------------------------------ input */

static BOOL inside(Object *obj, LONG x, LONG y)
{
    return x >= _mleft(obj) && x <= _mright(obj) && y >= _mtop(obj) && y <= _mbottom(obj);
}

/* The character under the pointer (-1: none, also beyond the text) and the
   caret position nearest to it, for selecting. */
static long at_pointer(struct Linktext_DATA *d, Object *obj, LONG mx, LONG my, ULONG *caret)
{
    struct lt_rp r;
    LONG line;
    lt_ulong c = 0;
    long off;
    *caret = 0;
    if (!d->lines || !d->nlines || !ready(d, obj, &r)) return -1;
    line = my < _mtop(obj) ? d->first - 1
         : (LONG)lt_line_at_y(d->lines, d->nlines, (lt_ulong)d->first, my - _mtop(obj));
    if (line < 0) return -1;
    if (line >= (LONG)d->nlines) { *caret = d->content->len; return -1; }
    line = (LONG)lt_line_at_x(d->lines, d->nlines, (lt_ulong)line, mx - column_left(d, obj));
    off = lt_hit(d->content, &d->lines[line], mx - column_left(d, obj), measure, &r, &c);
    use_font(&r, 0);
    *caret = c;
    return inside(obj, mx, my) ? off : -1;
}

static long link_at_pointer(struct Linktext_DATA *d, Object *obj, LONG mx, LONG my)
{
    ULONG caret;
    long off = at_pointer(d, obj, mx, my, &caret);
    return off >= 0 ? lt_link_at(d->content, off) : -1;
}

static void motion(struct Linktext_DATA *d, Object *obj, BOOL on)
{
    if (on == d->motion) return;
    d->motion = on;
    if (on) DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&d->ehn_move);
    else DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn_move);
}

static void cancel_press(struct Linktext_DATA *d, Object *obj)
{
    d->pressed = FALSE;
    d->dragging = FALSE;
    motion(d, obj, FALSE);
}

static void set_caret(struct Linktext_DATA *d, Object *obj, ULONG caret)
{
    ULONG old = d->sel_caret, lo = old < caret ? old : caret, hi = old < caret ? caret : old;
    if (old == caret) return;
    d->sel_caret = caret;
    redraw_lines(d, obj, lt_line_of(d->lines, d->nlines, lo), lt_line_of(d->lines, d->nlines, hi));
}

static void clear_selection(struct Linktext_DATA *d, Object *obj)
{
    ULONG a = d->sel_anchor, b = d->sel_caret;
    d->sel_anchor = d->sel_caret = 0;
    if (a != b && d->lines)
        redraw_lines(d, obj, lt_line_of(d->lines, d->nlines, a < b ? a : b), lt_line_of(d->lines, d->nlines, a < b ? b : a));
}

static void reveal_link(struct Linktext_DATA *d, Object *obj, long link)
{
    struct lt_rp r;
    lt_ulong line = 0, f = 0, last;
    long x0, x1;
    if (link < 0 || !ready(d, obj, &r)) return;
    if (!lt_link_fragment(d->content, d->lines, d->nlines, link, 0, measure, &r, &line, &x0, &x1)) return;
    last = line;
    for (f = 1; lt_link_fragment(d->content, d->lines, d->nlines, link, f, measure, &r, &last, &x0, &x1); f++) ;
    use_font(&r, 0);
    if ((LONG)line < d->first) scroll_to(d, obj, line);
    else if ((LONG)last >= d->first + visible(d, obj)) {
        LONG top = (LONG)last - visible(d, obj) + 1;
        scroll_to(d, obj, top > (LONG)line ? (LONG)line : top);
    }
}

static void set_focus(struct Linktext_DATA *d, Object *obj, long link)
{
    d->focus = link;
    reveal_link(d, obj, link);
    redraw_all(d, obj);
}

IPTR Linktext__MUIM_HandleEvent(Class *cl, Object *obj, struct MUIP_HandleEvent *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    struct IntuiMessage *im = msg->imsg;

    if (!d->shown || !d->content) return 0;

    /* Keys come here for every object in the window, active or not. */
    if (d->active && msg->muikey != MUIKEY_NONE) {
        ULONG qual = im ? im->Qualifier : 0;
        switch (msg->muikey) {
        case MUIKEY_UP:       scroll_to(d, obj, d->first - 1); return MUI_EventHandlerRC_Eat;
        case MUIKEY_DOWN:     scroll_to(d, obj, d->first + 1); return MUI_EventHandlerRC_Eat;
        case MUIKEY_PAGEUP:   scroll_to(d, obj, d->first - visible(d, obj)); return MUI_EventHandlerRC_Eat;
        case MUIKEY_PAGEDOWN: scroll_to(d, obj, d->first + visible(d, obj)); return MUI_EventHandlerRC_Eat;
        case MUIKEY_TOP:      scroll_to(d, obj, 0); return MUI_EventHandlerRC_Eat;
        case MUIKEY_BOTTOM:   scroll_to(d, obj, d->nlines); return MUI_EventHandlerRC_Eat;
        case MUIKEY_LEFT:
        case MUIKEY_RIGHT:
            /* Tab stays with the window, which handles it before any object,
               so it always leaves this object. */
            set_focus(d, obj, lt_link_step(d->content, d->focus, msg->muikey == MUIKEY_RIGHT ? 1 : -1));
            return MUI_EventHandlerRC_Eat;
        case MUIKEY_PRESS:
            if (d->focus >= 0) {
                do_action(d, obj, d->content, d->focus, MUIV_Linktext_Action_Open, qual);
                return MUI_EventHandlerRC_Eat;
            }
            break;
        default:
            break;
        }
    }
    if (!im) return 0;

    /* Amiga+C, as in String: MUI has no key setting for copy. */
    if (d->active && im->Class == IDCMP_RAWKEY && im->Code == RAWKEY_C
        && (im->Qualifier & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))) {
        if (d->focus >= 0)
            do_action(d, obj, d->content, d->focus, MUIV_Linktext_Action_Copy, im->Qualifier);
        else
            copy_selection(d);
        return MUI_EventHandlerRC_Eat;
    }

    switch (im->Class) {
    case IDCMP_RAWKEY:
        if ((im->Code == RAWKEY_NM_WHEEL_UP || im->Code == RAWKEY_NM_WHEEL_DOWN) && inside(obj, im->MouseX, im->MouseY)) {
            scroll_to(d, obj, d->first + (im->Code == RAWKEY_NM_WHEEL_UP ? -WHEEL_LINES : WHEEL_LINES));
            return MUI_EventHandlerRC_Eat;
        }
        break;

    case IDCMP_MOUSEBUTTONS:
        if (im->Code == SELECTDOWN && inside(obj, im->MouseX, im->MouseY)) {
            ULONG caret;
            IPTR off = FALSE;
            get(obj, MUIA_Disabled, &off);
            if (off) return 0;
            d->press_link = link_at_pointer(d, obj, im->MouseX, im->MouseY);
            at_pointer(d, obj, im->MouseX, im->MouseY, &caret);
            d->press_caret = caret;
            d->press_x = im->MouseX;
            d->press_y = im->MouseY;
            d->pressed = TRUE;
            d->dragging = FALSE;
            clear_selection(d, obj);
            if (d->focus >= 0) set_focus(d, obj, -1);
            motion(d, obj, TRUE);
            set(_win(obj), MUIA_Window_ActiveObject, (IPTR)obj);
            return MUI_EventHandlerRC_Eat;
        }
        if (im->Code == SELECTUP && d->pressed) {
            BOOL was_drag = d->dragging;
            long link = d->press_link;
            cancel_press(d, obj);
            if (!was_drag && link >= 0 && link_at_pointer(d, obj, im->MouseX, im->MouseY) == link)
                do_action(d, obj, d->content, link, MUIV_Linktext_Action_Open, im->Qualifier);
            return MUI_EventHandlerRC_Eat;
        }
        break;

    case IDCMP_MOUSEMOVE:
    case IDCMP_INTUITICKS:
        if (d->pressed) {
            LONG dx = im->MouseX - d->press_x, dy = im->MouseY - d->press_y;
            ULONG caret;
            if (!d->dragging) {
                if (dx * dx + dy * dy < DRAG_START * DRAG_START) break;
                d->dragging = TRUE;
                d->sel_anchor = d->sel_caret = d->press_caret;
            }
            /* Held above or below the text: it scrolls under the pointer. */
            if (im->Class == IDCMP_INTUITICKS) {
                if (im->MouseY < _mtop(obj)) scroll_to(d, obj, d->first - 1);
                else if (im->MouseY > _mbottom(obj)) scroll_to(d, obj, d->first + 1);
            }
            at_pointer(d, obj, im->MouseX, im->MouseY, &caret);
            set_caret(d, obj, caret);
            return MUI_EventHandlerRC_Eat;
        }
        break;
    }
    return 0;
}

IPTR Linktext__MUIM_GoActive(Class *cl, Object *obj, Msg msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    d->active = TRUE;
    return DoSuperMethodA(cl, obj, msg);
}

IPTR Linktext__MUIM_GoInactive(Class *cl, Object *obj, Msg msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    d->active = FALSE;
    if (d->focus >= 0) set_focus(d, obj, -1);
    return DoSuperMethodA(cl, obj, msg);
}

/* ---------------------------------------------------------- content menu */

static void menu_hold(struct Linktext_DATA *d, lt_content *c)
{
    if (c) lt_content_ref(c);
    if (d->menu_content) lt_content_unref(d->menu_content);
    d->menu_content = c;
}

IPTR Linktext__MUIM_ContextMenuBuild(Class *cl, Object *obj, struct MUIP_ContextMenuBuild *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    /* The content is kept so that the choice, which comes later, acts on the
       link that was under the pointer whatever has been set since. */
    menu_hold(d, d->content);
    d->menu_link = d->content ? link_at_pointer(d, obj, msg->mx, msg->my) : -1;
    set(d->item_open, MUIA_Menuitem_Enabled, d->menu_link >= 0);
    set(d->item_copy, MUIA_Menuitem_Enabled, d->menu_link >= 0);
    set(d->item_text, MUIA_Menuitem_Enabled, d->sel_anchor != d->sel_caret);
    return (IPTR)d->menu;
}

IPTR Linktext__MUIM_ContextMenuChoice(Class *cl, Object *obj, struct MUIP_ContextMenuChoice *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    if (msg->item == d->item_text) copy_selection(d);
    else if (d->menu_content && d->menu_link >= 0)
        do_action(d, obj, d->menu_content, d->menu_link,
                  msg->item == d->item_open ? MUIV_Linktext_Action_Open : MUIV_Linktext_Action_Copy, 0);
    return 0;
}

/* ------------------------------------------------------------ bubble help */

/* A bubble as wide as the target is long would pass the screen's edge, so
   the target is broken into lines; every line is the exact text. */
IPTR Linktext__MUIM_CreateBubble(Class *cl, Object *obj, struct MUIP_CreateBubble *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    long link = d->content ? link_at_pointer(d, obj, msg->x, msg->y) : -1;
    const lt_span *s;
    struct MUIP_CreateBubble m = *msg;
    ULONG i, k = 0;
    if (link < 0) return 0;
    s = lt_link(d->content, link);
    lt_free(d->help);
    d->help = (char *)lt_alloc(s->target_len + s->target_len / HELP_COLUMNS + 1);
    if (!d->help) return 0;
    for (i = 0; i < s->target_len; i++) {
        if (i && i % HELP_COLUMNS == 0) d->help[k++] = '\n';
        d->help[k++] = s->target[i];
    }
    d->help[k] = 0;
    m.txt = (APTR)d->help;
    /* Zune puts a bubble at the pointer, where it takes the next click. A
       long target's bubble is large and a click on a link is the point of
       hovering over it. */
    m.x += 12;
    m.y += 16;
    return DoSuperMethodA(cl, obj, (Msg)&m);
}

/* ------------------------------------------------------------- MUI basics */

IPTR Linktext__MUIM_Linktext_SetDocument(Class *cl, Object *obj, struct MUIP_Linktext_SetDocument *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    lt_span_in stack[16], *in = stack;
    lt_block_in bstack[8], *bin = bstack;
    lt_content *c = NULL;
    lt_line *lines = NULL;
    lt_ulong n = 0, i;
    int status = LT_OK;
    struct lt_rp r;

    if (msg->SpanCount > 16) {
        if (msg->SpanCount > LINKTEXT_MAX_SPANS) return MUIV_Linktext_Status_TooLarge;
        in = (lt_span_in *)lt_alloc(sizeof *in * msg->SpanCount);
        if (!in) return MUIV_Linktext_Status_NoMemory;
    }
    if (msg->BlockCount > 8) {
        /* Each block has a paragraph of its own: more blocks than the text
           can have paragraphs are refused before anything is allocated. */
        lt_ulong tl = lt_text_length((const char *)msg->Text,
                                     msg->Length == (ULONG)~0UL ? (lt_ulong)-1 : msg->Length);
        if (msg->BlockCount - 1 > tl) status = LT_ORDER;
        else if (!(bin = (lt_block_in *)lt_alloc(sizeof *bin * msg->BlockCount))) status = LT_NOMEM;
    }
    if (status == LT_OK && msg->SpanCount && !msg->Spans) status = LT_RANGE;
    if (status == LT_OK && msg->BlockCount && !msg->Blocks) status = LT_RANGE;
    for (i = 0; status == LT_OK && i < msg->SpanCount; i++) {
        in[i].start = msg->Spans[i].Start;
        in[i].length = msg->Spans[i].Length;
        in[i].flags = msg->Spans[i].Flags & LT_SPAN_FLAGS;
        in[i].target = (const char *)msg->Spans[i].Target;
        in[i].id = msg->Spans[i].ID;
    }
    for (i = 0; status == LT_OK && i < msg->BlockCount; i++) {
        bin[i].start = msg->Blocks[i].Start;
        bin[i].length = msg->Blocks[i].Length;
        bin[i].flags = msg->Blocks[i].Flags;
        bin[i].indent = msg->Blocks[i].Indent;
        bin[i].rindent = msg->Blocks[i].RightIndent;
        bin[i].background = msg->Blocks[i].Background;
        bin[i].padx = msg->Blocks[i].PadX;
        bin[i].pady = msg->Blocks[i].PadY;
        bin[i].align = msg->Blocks[i].Align;
        bin[i].rule = msg->Blocks[i].Rule;
        bin[i].head = msg->Blocks[i].Head;
    }
    if (status == LT_OK)
        c = lt_document_new((const char *)msg->Text, msg->Length == (ULONG)~0UL ? (lt_ulong)-1 : msg->Length,
                            in, msg->SpanCount, bin, msg->BlockCount, &status);
    if (in != stack) lt_free(in);
    if (bin != bstack) lt_free(bin);
    if (!c) return status;           /* the status numbers are the same */

    /* Wrapped before anything changes: a failure here keeps the old text too. */
    if (d->shown && ready(d, obj, &r)) {
        if (!wrap(d, obj, c, &r, &lines, &n)) {
            lt_content_unref(c);
            return MUIV_Linktext_Status_NoMemory;
        }
        use_font(&r, 0);
    }
    /* What lies in the bytes both texts start with stays: a selection, a
       drag that makes one, the press on a link and the keyboard's link (a
       reply growing at the end under a selection). The rest is reset. */
    {
        lt_ulong keep = d->content ? lt_common_prefix(d->content, c) : 0;
        ULONG sel_end = d->sel_anchor > d->sel_caret ? d->sel_anchor : d->sel_caret;
        BOOL keep_sel = sel_end <= keep;
        BOOL keep_press = d->pressed && (d->press_link >= 0
                              ? lt_same_link(d->content, c, d->press_link, keep)
                              : d->press_caret <= keep && keep_sel);
        BOOL keep_focus = d->focus >= 0 && lt_same_link(d->content, c, d->focus, keep);
        if (d->pressed && !keep_press) cancel_press(d, obj);
        lt_content_unref(d->content);
        lt_free(d->lines);
        d->content = c;
        d->lines = lines;
        d->nlines = n;
        d->laid_w = lines ? column_width(d, obj) : -1;
        if (lines) clamp_first(d, obj);
        if (!keep_sel) d->sel_anchor = d->sel_caret = 0;
        if (!keep_focus) d->focus = -1;
        if (!keep_press) d->press_link = -1;
    }
    update_prop(d, obj);
    if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
    return MUIV_Linktext_Status_OK;
}

IPTR Linktext__MUIM_Linktext_SetContent(Class *cl, Object *obj, struct MUIP_Linktext_SetContent *msg)
{
    struct MUIP_Linktext_SetDocument m;
    m.MethodID = MUIM_Linktext_SetDocument;
    m.Text = msg->Text;
    m.Length = msg->Length;
    m.Spans = msg->Spans;
    m.SpanCount = msg->SpanCount;
    m.Blocks = NULL;
    m.BlockCount = 0;
    return Linktext__MUIM_Linktext_SetDocument(cl, obj, &m);
}

IPTR Linktext__MUIM_Linktext_LinkBox(Class *cl, Object *obj, struct MUIP_Linktext_LinkBox *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    struct lt_rp r;
    lt_ulong line;
    long x0, x1;
    LONG y;
    int found;
    if (!d->shown || !d->lines || !ready(d, obj, &r)) return FALSE;
    found = lt_link_fragment(d->content, d->lines, d->nlines, msg->Link, msg->Fragment, measure, &r, &line, &x0, &x1);
    use_font(&r, 0);
    if (!found) return FALSE;
    y = line_top(d, obj, (LONG)line);
    msg->Box->Left = column_left(d, obj) + x0;
    msg->Box->Top = y;
    msg->Box->Width = x1 - x0;
    msg->Box->Height = d->lines[line].h;
    return y + d->lines[line].h > _mtop(obj) && y < _mtop(obj) + _mheight(obj);
}

/* The column changed: wrapped again now when shown, else at Show. */
static void relayout(struct Linktext_DATA *d, Object *obj)
{
    d->laid_w = -1;
    if (!d->shown || !d->content) return;
    layout(d, obj);
    update_prop(d, obj);
    MUI_Redraw(obj, MADF_DRAWOBJECT);
}

static void set_tags(struct Linktext_DATA *d, Object *obj, struct TagItem *tags, BOOL init)
{
    struct TagItem *tag, *state = tags;
    while ((tag = NextTagItem(&state))) {
        switch (tag->ti_Tag) {
        case MUIA_Linktext_ActionHook: d->hook = (struct Hook *)tag->ti_Data; break;
        case MUIA_Linktext_Lines:      if (init && tag->ti_Data) d->want_lines = tag->ti_Data; break;
        case MUIA_Linktext_FixHeight:  if (init) d->fix_height = tag->ti_Data != 0; break;
        case MUIA_Linktext_Scroller:   if (init) d->scroller = (Object *)tag->ti_Data; break;
        case LT_SCROLL_PX:
            if (d->shown && d->lines && d->nlines) scroll_to(d, obj, line_from_px(d, (LONG)tag->ti_Data));
            break;
        case MUIA_Linktext_First:
            /* ~0UL and anything else past the end go to the end. */
            if (d->shown) scroll_to(d, obj, tag->ti_Data > 0x7fffffffUL ? 0x7fffffffL : (LONG)tag->ti_Data);
            else d->first = tag->ti_Data > 0x7fffffffUL ? 0x7fffffffL : (LONG)tag->ti_Data;
            break;
        case MUIA_Linktext_EmptyLine:
            if (d->empty8 != (ULONG)tag->ti_Data) {
                d->empty8 = (ULONG)tag->ti_Data;
                if (!init) relayout(d, obj);
            }
            break;
        case MUIA_Linktext_TextColor:
        case MUIA_Linktext_DimColor:
        case MUIA_Linktext_AccentColor:
        case MUIA_Linktext_ShadeColor:
        {
            ULONG k = tag->ti_Tag == MUIA_Linktext_TextColor ? 0 : tag->ti_Tag == MUIA_Linktext_DimColor ? 1
                    : tag->ti_Tag == MUIA_Linktext_AccentColor ? 2 : 3;
            if (d->rgb[k] == (ULONG)tag->ti_Data) break;
            d->rgb[k] = (ULONG)tag->ti_Data;
            if (d->setup) {
                struct ColorMap *cm = _screen(obj) ? _screen(obj)->ViewPort.ColorMap : NULL;
                if (d->pen[k] >= 0 && cm) ReleasePen(cm, (ULONG)d->pen[k]);
                d->pen[k] = d->rgb[k] & 0x80000000UL ? obtain_pen(obj, d->rgb[k] & 0xffffffUL) : -1;
                if (d->shown) MUI_Redraw(obj, MADF_DRAWOBJECT);
            }
            break;
        }
        case MUIA_Linktext_MaxColumns:
            if (d->max_columns != (ULONG)tag->ti_Data) {
                d->max_columns = (ULONG)tag->ti_Data;
                if (!init) relayout(d, obj);
            }
            break;
        }
    }
}

static void make_menu(struct Linktext_DATA *d)
{
    d->menu = MenustripObject,
        MUIA_Family_Child, (IPTR)(MenuObject, MUIA_Menu_Title, (IPTR)"Link",
            MUIA_Family_Child, (IPTR)(d->item_open = MenuitemObject, MUIA_Menuitem_Title, (IPTR)"Open link", End),
            MUIA_Family_Child, (IPTR)(d->item_copy = MenuitemObject, MUIA_Menuitem_Title, (IPTR)"Copy link address", End),
            MUIA_Family_Child, (IPTR)(d->item_text = MenuitemObject, MUIA_Menuitem_Title, (IPTR)"Copy selected text", End),
        End),
    End;
}

Object *Linktext__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    struct Linktext_DATA *d;
    obj = (Object *)DoSuperNewTags(cl, obj, NULL,
                                   MUIA_FillArea, TRUE,
                                   /* What is not NULL here only asks Zune to
                                      offer bubbles; MUIM_CreateBubble gives the text. */
                                   MUIA_ShortHelp, (IPTR)" ",
                                   TAG_MORE, (IPTR)msg->ops_AttrList);
    if (!obj) return NULL;
    d = INST_DATA(cl, obj);
    d->want_lines = 4;
    d->laid_w = -1;
    d->focus = -1;
    d->press_link = -1;
    d->menu_link = -1;
    d->dirty_first = -1;
    d->pen[0] = d->pen[1] = d->pen[2] = d->pen[3] = -1;
    set_tags(d, obj, msg->ops_AttrList, TRUE);
    d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY;
    d->ehn.ehn_Object = obj;
    d->ehn.ehn_Class = cl;
    d->ehn.ehn_Flags = MUI_EHF_GUIMODE;
    d->ehn_move = d->ehn;
    d->ehn_move.ehn_Events = IDCMP_MOUSEMOVE | IDCMP_INTUITICKS;
    if (d->scroller)
        DoMethod(d->scroller, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (IPTR)obj, 3,
                 MUIM_NoNotifySet, LT_SCROLL_PX, MUIV_TriggerValue);
    make_menu(d);
    if (d->menu) set(obj, MUIA_ContextMenu, (IPTR)d->menu);
    return obj;
}

IPTR Linktext__OM_DISPOSE(Class *cl, Object *obj, Msg msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    menu_hold(d, NULL);
    lt_content_unref(d->content);
    lt_free(d->lines);
    lt_free(d->help);
    if (d->menu) {
        set(obj, MUIA_ContextMenu, 0);
        MUI_DisposeObject(d->menu);
    }
    return DoSuperMethodA(cl, obj, msg);
}

IPTR Linktext__OM_SET(Class *cl, Object *obj, struct opSet *msg)
{
    set_tags(INST_DATA(cl, obj), obj, msg->ops_AttrList, FALSE);
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

IPTR Linktext__OM_GET(Class *cl, Object *obj, struct opGet *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    switch (msg->opg_AttrID) {
    case MUIA_Linktext_ActionHook: *msg->opg_Storage = (IPTR)d->hook; return TRUE;
    case MUIA_Linktext_Lines:      *msg->opg_Storage = d->want_lines; return TRUE;
    case MUIA_Linktext_First:      *msg->opg_Storage = d->first; return TRUE;
    case MUIA_Linktext_LineCount:  *msg->opg_Storage = d->nlines; return TRUE;
    case MUIA_Linktext_MaxColumns: *msg->opg_Storage = d->max_columns; return TRUE;
    case MUIA_Linktext_Visible:    *msg->opg_Storage = d->shown ? (ULONG)visible(d, obj) : d->vis; return TRUE;
    case MUIA_Linktext_EmptyLine:  *msg->opg_Storage = d->empty8 ? d->empty8 : 8; return TRUE;
    case MUIA_Linktext_TextColor:  *msg->opg_Storage = d->rgb[0]; return TRUE;
    case MUIA_Linktext_DimColor:   *msg->opg_Storage = d->rgb[1]; return TRUE;
    case MUIA_Linktext_AccentColor: *msg->opg_Storage = d->rgb[2]; return TRUE;
    case MUIA_Linktext_ShadeColor: *msg->opg_Storage = d->rgb[3]; return TRUE;
    }
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

IPTR Linktext__MUIM_AskMinMax(Class *cl, Object *obj, struct MUIP_AskMinMax *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    struct TextFont *f = _font(obj);
    LONG h;
    DoSuperMethodA(cl, obj, (Msg)msg);
    h = d->lineh * (LONG)d->want_lines;
    msg->MinMaxInfo->MinWidth += f->tf_XSize * 12;
    msg->MinMaxInfo->DefWidth += f->tf_XSize * 40;
    msg->MinMaxInfo->MaxWidth = MUI_MAXMAX;
    msg->MinMaxInfo->MinHeight += d->fix_height ? h : d->lineh;
    msg->MinMaxInfo->DefHeight += h;
    msg->MinMaxInfo->MaxHeight = d->fix_height ? msg->MinMaxInfo->MinHeight : MUI_MAXMAX;
    return 0;
}

/* The user's fixed font, as MUI opens it for any object that asks for
   MUIV_Font_Fixed: a frameless Rectangle set up with this object's render
   info, the way a group sets up its children. The window keeps the font
   open until its own cleanup. Without it, fixed spans use the normal font. */
static struct TextFont *font_setup(Object *obj, IPTR which, Object **holder)
{
    Object *o = MUI_NewObject((CONST_STRPTR)MUIC_Rectangle, MUIA_Font, which, TAG_DONE);
    *holder = NULL;
    if (!o) return NULL;
    DoMethod(o, MUIM_ConnectParent, (IPTR)obj);
    if (DoMethod(o, MUIM_Setup, (IPTR)muiRenderInfo(obj)) && _font(o)) {
        *holder = o;
        return _font(o);
    }
    DoMethod(o, MUIM_DisconnectParent);
    MUI_DisposeObject(o);
    return NULL;
}

static void font_cleanup(Object **holder)
{
    if (!*holder) return;
    DoMethod(*holder, MUIM_Cleanup);
    DoMethod(*holder, MUIM_DisconnectParent);
    MUI_DisposeObject(*holder);
    *holder = NULL;
}

/* The normal font a quarter larger (at least 3 pixels), through
   diskfont.library: for Large text when the user's big font is not larger
   than the normal one (ABIv11 2026.09's settings). NULL when it cannot be
   opened larger. */
static struct TextFont *larger_font(Object *obj)
{
    struct TextFont *f = _font(obj), *l = NULL;
    struct Library *DiskfontBase = OpenLibrary((CONST_STRPTR)"diskfont.library", 0);
    struct TextAttr ta;
    UWORD grow;
    if (!DiskfontBase || !f || !f->tf_Message.mn_Node.ln_Name) {
        if (DiskfontBase) CloseLibrary(DiskfontBase);
        return NULL;
    }
    grow = f->tf_YSize / 4 > 3 ? f->tf_YSize / 4 : 3;
    ta.ta_Name = (STRPTR)f->tf_Message.mn_Node.ln_Name;
    ta.ta_YSize = f->tf_YSize + grow;
    ta.ta_Style = FS_NORMAL;
    ta.ta_Flags = 0;
    l = OpenDiskFont(&ta);
    CloseLibrary(DiskfontBase);           /* an open font keeps what it needs */
    if (l && l->tf_YSize <= f->tf_YSize) { CloseFont(l); l = NULL; }
    return l;
}

static void fixed_font_setup(struct Linktext_DATA *d, Object *obj)
{
    d->fixed = font_setup(obj, MUIV_Font_Fixed, &d->fixed_obj);
    /* Large text: the user's big font when it is larger, else the normal
       one opened larger, else the normal one. */
    d->large = font_setup(obj, MUIV_Font_Big, &d->large_obj);
    if (d->large && d->large->tf_YSize <= _font(obj)->tf_YSize) {
        font_cleanup(&d->large_obj);
        d->large = NULL;
    }
    d->large_own = NULL;
    if (!d->large) d->large = d->large_own = larger_font(obj);
}

static void fixed_font_cleanup(struct Linktext_DATA *d)
{
    font_cleanup(&d->fixed_obj);
    d->fixed = NULL;
    font_cleanup(&d->large_obj);
    if (d->large_own) CloseFont(d->large_own);
    d->large_own = NULL;
    d->large = NULL;
}

static ULONG brightness(struct ColorMap *cm, LONG pen)
{
    ULONG rgb[3];
    GetRGB32(cm, pen, 1, rgb);
    return (rgb[0] >> 24) * 30 + (rgb[1] >> 24) * 59 + (rgb[2] >> 24) * 11;
}

/* Dim text is the half pen on the text's side of the background: half
   shadow under dark text on a light scheme, half shine under light text on
   a dark one. Either way it stays readable and quieter than the text. */
static UWORD dim_pen(Object *obj)
{
    struct ColorMap *cm = _screen(obj) ? _screen(obj)->ViewPort.ColorMap : NULL;
    if (cm && brightness(cm, _pens(obj)[MPEN_TEXT]) > brightness(cm, _pens(obj)[MPEN_BACKGROUND]))
        return MPEN_HALFSHINE;
    return MPEN_HALFSHADOW;
}

IPTR Linktext__MUIM_Setup(Class *cl, Object *obj, struct MUIP_Setup *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    struct TextFont *f, *x;
    LONG above, below;
    if (!DoSuperMethodA(cl, obj, (Msg)msg)) return FALSE;
    fixed_font_setup(d, obj);
    /* One line height for both fonts, on a common baseline. */
    f = _font(obj);
    x = d->fixed ? d->fixed : f;
    above = f->tf_Baseline > x->tf_Baseline ? f->tf_Baseline : x->tf_Baseline;
    below = f->tf_YSize - f->tf_Baseline > x->tf_YSize - x->tf_Baseline
            ? f->tf_YSize - f->tf_Baseline : x->tf_YSize - x->tf_Baseline;
    d->base = above;
    d->lineh = above + below + 1;
    /* A line with Large text: the big font's height on its own baseline,
       the other fonts on it too. */
    if (d->large) {
        LONG la = d->large->tf_Baseline > above ? d->large->tf_Baseline : above;
        LONG lb = d->large->tf_YSize - d->large->tf_Baseline > below ? d->large->tf_YSize - d->large->tf_Baseline : below;
        d->large_base = la;
        d->largeh = la + lb + 1;
    } else {
        d->large_base = d->base;
        d->largeh = d->lineh;
    }
    d->avg = 0;                    /* measured again at Show */
    d->dim_pen = dim_pen(obj);
    d->laid_w = -1;                /* the font may be another one now */
    obtain_pens(d, obj);
    d->setup = TRUE;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&d->ehn);
    return TRUE;
}

IPTR Linktext__MUIM_Cleanup(Class *cl, Object *obj, struct MUIP_Cleanup *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    cancel_press(d, obj);
    DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn);
    fixed_font_cleanup(d);
    release_pens(d, obj);
    d->setup = FALSE;
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

IPTR Linktext__MUIM_Show(Class *cl, Object *obj, struct MUIP_Show *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    IPTR r = DoSuperMethodA(cl, obj, (Msg)msg);
    struct lt_rp lr;
    BOOL at_end = was_at_end(d);
    d->shown = TRUE;
    ready(d, obj, &lr);
    if (d->content && d->laid_w != column_width(d, obj)) layout(d, obj);
    if (at_end) keep_end(d, obj);
    d->vis = visible(d, obj);
    clamp_first(d, obj);
    update_prop(d, obj);
    return r;
}

IPTR Linktext__MUIM_Hide(Class *cl, Object *obj, struct MUIP_Hide *msg)
{
    struct Linktext_DATA *d = INST_DATA(cl, obj);
    cancel_press(d, obj);
    d->shown = FALSE;
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

#ifdef LINKTEXT_EMBEDDED
BOOPSI_DISPATCHER(IPTR, Linktext_Dispatcher, cl, obj, msg)
{
    switch (msg->MethodID) {
    case OM_NEW:                  return (IPTR)Linktext__OM_NEW(cl, obj, (struct opSet *)msg);
    case OM_DISPOSE:              return Linktext__OM_DISPOSE(cl, obj, msg);
    case OM_SET:                  return Linktext__OM_SET(cl, obj, (struct opSet *)msg);
    case OM_GET:                  return Linktext__OM_GET(cl, obj, (struct opGet *)msg);
    case MUIM_AskMinMax:          return Linktext__MUIM_AskMinMax(cl, obj, (APTR)msg);
    case MUIM_Setup:              return Linktext__MUIM_Setup(cl, obj, (APTR)msg);
    case MUIM_Cleanup:            return Linktext__MUIM_Cleanup(cl, obj, (APTR)msg);
    case MUIM_Show:               return Linktext__MUIM_Show(cl, obj, (APTR)msg);
    case MUIM_Hide:               return Linktext__MUIM_Hide(cl, obj, (APTR)msg);
    case MUIM_Draw:               return Linktext__MUIM_Draw(cl, obj, (APTR)msg);
    case MUIM_HandleEvent:        return Linktext__MUIM_HandleEvent(cl, obj, (APTR)msg);
    case MUIM_GoActive:           return Linktext__MUIM_GoActive(cl, obj, msg);
    case MUIM_GoInactive:         return Linktext__MUIM_GoInactive(cl, obj, msg);
    case MUIM_ContextMenuBuild:   return Linktext__MUIM_ContextMenuBuild(cl, obj, (APTR)msg);
    case MUIM_ContextMenuChoice:  return Linktext__MUIM_ContextMenuChoice(cl, obj, (APTR)msg);
    case MUIM_CreateBubble:       return Linktext__MUIM_CreateBubble(cl, obj, (APTR)msg);
    case MUIM_Linktext_SetContent: return Linktext__MUIM_Linktext_SetContent(cl, obj, (APTR)msg);
    case MUIM_Linktext_LinkBox:   return Linktext__MUIM_Linktext_LinkBox(cl, obj, (APTR)msg);
    case MUIM_Linktext_SetDocument: return Linktext__MUIM_Linktext_SetDocument(cl, obj, (APTR)msg);
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

ULONG Linktext_DataSize(void) { return sizeof(struct Linktext_DATA); }
APTR Linktext_Entry(void) { return (APTR)Linktext_Dispatcher; }
#endif
