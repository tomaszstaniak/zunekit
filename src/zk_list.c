/* SPDX-License-Identifier: MIT */
/* zk_list: see zk_list.h. Two classes: the NList subclass the program
 * makes, and the Area subclass of the one image object each list gives
 * NList for its cells (NList.mcc/MUIM_NList_CreateImage: "ESC O[<ptr>;
 * <tag>;<value>]": NList sets tag to value on the object just before it
 * draws it, with MUIA_NLIMG_EntryCurrent the entry). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <mui/NList_mcc.h>
#include <clib/alib_protos.h>
#include "zk_list.h"
#include "zk_list_core.h"
#include "zk_text.h"
#include "zk_text_aros.h"

#define ZK_COLS   16               /* columns made UTF-8; more stay NList's */
#define ZK_RUNS   32               /* style runs drawn in one cell */
#define ZK_WCACHE 128              /* widths kept */
#define ZK_STALE_W 24              /* a cell that could not be kept */

/* The tags NList sets on the cell object (its value is the column). */
#define ZKCELL_COL   (ZKLIST_TAGBASE + 0x80)
#define ZKCELL_OWNER (ZKLIST_TAGBASE + 0x81)
#define ZKCELL_LOST  0xffffUL      /* the column value of a cell not kept */

struct zl_width { unsigned long h; size_t n; LONG w; int used; };

struct ZkListData {
    int utf8;
    ULONG status;
    const zt_fontset *set;
    zt_font *font;
    Object *list;
    Object *cell;                  /* the image object, ours to dispose */
    APTR img;                      /* NList's handle for it, between Setup and Cleanup */
    int ncols;
    LONG kept_ent;                 /* the entry the kept texts are of */
    char *kept[ZK_COLS];
    size_t kept_len[ZK_COLS], kept_cap[ZK_COLS];
    char esc[ZK_COLS][96];
    char *copy[ZK_COLS];           /* 8-bit copies (ZKLIST_COPIES) */
    struct zl_width wc[ZK_WCACHE];
};

struct ZkCellData {
    struct ZkListData *owner;
    LONG ent;
    ULONG col;
};

static struct MUI_CustomClass *ZkListClass, *ZkCellClass;
static int layer;

/* Told with a notification at Setup and Cleanup; while drawing it is only
   kept (no notification from inside NList's drawing), and read with get. */
static void set_status(struct ZkListData *d, ULONG st)
{
    if (d->status == st) return;
    d->status = st;
    if (d->list) SetAttrs(d->list, MUIA_ZkList_Status, st, TAG_DONE);
}
static void note_status(struct ZkListData *d, ULONG st) { d->status = st; }

/* ---------------------------------------------------------- measuring */

static unsigned run_flags(unsigned zl) { return (zl & ZL_BOLD) ? ZT_BOLD : 0; }

/* The width of a cell's text; -1 when out of memory for its shaping. */
static LONG text_width(struct ZkListData *d, const char *s, size_t n)
{
    unsigned long h = zl_hash(s, n);
    struct zl_width *w = &d->wc[h % ZK_WCACHE];
    zl_run r[ZK_RUNS];
    size_t k, i;
    LONG sum = 0;
    if (w->used && w->h == h && w->n == n) return w->w;
    k = zl_runs(s, n, r, ZK_RUNS);
    if (k > ZK_RUNS) k = ZK_RUNS;
    for (i = 0; i < k; i++) {
        unsigned f = run_flags(r[i].flags);
        if (!zt_ready(d->font, s + r[i].start, r[i].len, f)) return -1;
        sum += zt_measure(d->font, s + r[i].start, r[i].len, f);
    }
    w->h = h; w->n = n; w->w = sum; w->used = 1;
    return sum;
}

static int keep(struct ZkListData *d, int c, const char *s, size_t n)
{
    if (n + 1 > d->kept_cap[c]) {
        size_t cap = n + 64;
        char *p = (char *)realloc(d->kept[c], cap);
        if (!p) return 0;
        d->kept[c] = p;
        d->kept_cap[c] = cap;
    }
    memcpy(d->kept[c], s, n);
    d->kept[c][n] = 0;
    d->kept_len[c] = n;
    return 1;
}

static void free_texts(struct ZkListData *d)
{
    int c;
    for (c = 0; c < ZK_COLS; c++) {
        free(d->kept[c]); d->kept[c] = NULL; d->kept_cap[c] = d->kept_len[c] = 0;
        free(d->copy[c]); d->copy[c] = NULL;
    }
    d->kept_ent = -2;
}

/* ------------------------------------------------------------ the cell */

/* The pen NList draws the entry's text in: title, cursor, selected or
   list pen. */
static ULONG cell_pen(struct ZkListData *d, LONG ent)
{
    IPTR p = 0, act = (IPTR)MUIV_NList_Active_Off;
    ULONG tag = MUIA_NList_ListPen;
    if (ent < 0) tag = MUIA_NList_TitlePen;
    else {
        LONG st = MUIV_NList_Select_Off;
        DoMethod(d->list, MUIM_NList_Select, ent, MUIV_NList_Select_Ask, (IPTR)&st);
        GetAttr(MUIA_NList_Active, d->list, &act);
        if ((LONG)act == ent) tag = MUIA_NList_CursorPen;
        else if (st != MUIV_NList_Select_Off) tag = MUIA_NList_SelectPen;
    }
    if (!GetAttr(tag, d->list, &p)) return _pens(d->list)[MPEN_TEXT];
    return MUIPEN(p);
}

static void cell_draw(struct ZkCellData *c, Object *obj)
{
    struct ZkListData *d = c->owner;
    struct RastPort *rp = _rp(obj);
    struct ColorMap *cm = _screen(obj) ? _screen(obj)->ViewPort.ColorMap : NULL;
    LONG x = _left(obj), y = _top(obj), w = _width(obj), h = _height(obj);
    LONG fh, asc, top, xx;
    ULONG pen;
    zl_run r[ZK_RUNS];
    size_t k, i;
    const char *s;
    if (!d || !d->font || !rp || !cm || w <= 0 || h <= 0) return;
    pen = cell_pen(d, c->ent);
    if (c->col == ZKCELL_LOST || c->col >= ZK_COLS || d->kept_ent != c->ent || !d->kept[c->col]) {
        /* nothing kept for this cell (memory, or NList drew a cell of
           another entry than the one it displayed last): marked, never
           another row's text */
        zt_stale(rp, pen, x, y, x + w - 1, y + h - 1);
        note_status(d, ZKLIST_MEMORY);
        return;
    }
    s = d->kept[c->col];
    fh = zt_font_height(d->font);
    asc = zt_font_ascent(d->font);
    top = h > fh ? y + (h - fh) / 2 : y;
    k = zl_runs(s, d->kept_len[c->col], r, ZK_RUNS);
    if (k > ZK_RUNS) k = ZK_RUNS;
    xx = x;
    for (i = 0; i < k && xx < x + w; i++) {
        unsigned f = run_flags(r[i].flags);
        LONG rw, bw, c1;
        UBYTE *cov;
        if (!zt_ready(d->font, s + r[i].start, r[i].len, f)) {
            zt_stale(rp, pen, xx, y, x + w - 1, y + h - 1);
            note_status(d, ZKLIST_MEMORY);
            break;
        }
        rw = zt_measure(d->font, s + r[i].start, r[i].len, f);
        bw = rw + 4;                       /* ink can reach a little past the advance */
        if (!(cov = (UBYTE *)AllocVec((ULONG)(bw * fh), MEMF_ANY | MEMF_CLEAR))) {
            zt_stale(rp, pen, xx, y, x + w - 1, y + h - 1);
            note_status(d, ZKLIST_MEMORY);
            break;
        }
        zt_draw(d->font, s + r[i].start, r[i].len, f, cov, (int)bw, (int)fh, (int)bw, 0, asc);
        c1 = x + w - xx;
        if (c1 > bw) c1 = bw;
        zt_blit(rp, cm, pen, cov, bw, fh, 0, c1, xx, top);
        if (r[i].flags & ZL_UNDERLINE) {
            LONG e = xx + rw - 1;
            if (e > x + w - 1) e = x + w - 1;
            SetAPen(rp, pen);
            Move(rp, xx, top + asc + 1);
            Draw(rp, e, top + asc + 1);
        }
        FreeVec(cov);
        xx += rw;
    }
}

static IPTR ZkCell_New(struct IClass *cl, Object *obj, struct opSet *msg)
{
    struct ZkCellData *c;
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, (Msg)msg))) return 0;
    c = INST_DATA(cl, obj);
    c->owner = (struct ZkListData *)GetTagData(ZKCELL_OWNER, 0, msg->ops_AttrList);
    c->ent = -2;
    c->col = ZKCELL_LOST;
    return (IPTR)obj;
}

/* What NList sets before drawing: taken here and not passed on, so no
   redraw follows (NList's documentation asks for that). */
static IPTR ZkCell_Set(struct IClass *cl, Object *obj, struct opSet *msg)
{
    struct ZkCellData *c = INST_DATA(cl, obj);
    struct TagItem *tags = msg->ops_AttrList, *t;
    int other = 0;
    while ((t = NextTagItem(&tags))) {
        switch (t->ti_Tag) {
        case MUIA_NLIMG_EntryCurrent: c->ent = (LONG)t->ti_Data; break;
        case MUIA_NLIMG_EntryHeight: break;
        case ZKCELL_COL: c->col = (ULONG)t->ti_Data; break;
        default: other = 1; break;
        }
    }
    return other ? DoSuperMethodA(cl, obj, (Msg)msg) : 0;
}

static IPTR ZkCell_AskMinMax(struct IClass *cl, Object *obj, struct MUIP_AskMinMax *msg)
{
    struct ZkCellData *c = INST_DATA(cl, obj);
    LONG lh = c->owner && c->owner->font ? zt_font_height(c->owner->font) : 8;
    DoSuperMethodA(cl, obj, (Msg)msg);
    msg->MinMaxInfo->MinWidth += 1;
    msg->MinMaxInfo->DefWidth += 1;
    msg->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    /* NList takes the image's height as the entries' least height: the
       faces' line, which can be taller than the list's font */
    msg->MinMaxInfo->MinHeight += lh;
    msg->MinMaxInfo->DefHeight += lh;
    msg->MinMaxInfo->MaxHeight += MUI_MAXMAX;
    return 0;
}

BOOPSI_DISPATCHER(IPTR, ZkCellDispatcher, cl, obj, msg)
{
    switch (msg->MethodID) {
    case OM_NEW: return ZkCell_New(cl, obj, (struct opSet *)msg);
    case OM_SET: return ZkCell_Set(cl, obj, (struct opSet *)msg);
    case MUIM_AskMinMax: return ZkCell_AskMinMax(cl, obj, (struct MUIP_AskMinMax *)msg);
    case MUIM_Draw: {
        struct MUIP_Draw *m = (struct MUIP_Draw *)msg;
        DoSuperMethodA(cl, obj, msg);
        if (m->flags & MADF_DRAWOBJECT) cell_draw(INST_DATA(cl, obj), obj);
        return 0;
    }
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

/* ------------------------------------------------------------ the list */

static void count_columns(struct ZkListData *d, Object *obj)
{
    IPTR f = 0;
    GetAttr(MUIA_NList_Format, obj, &f);
    d->ncols = zl_format_columns((const char *)f);
}

static IPTR ZkList_New(struct IClass *cl, Object *obj, struct opSet *msg)
{
    struct ZkListData *d;
    if (!(obj = (Object *)DoSuperMethodA(cl, obj, (Msg)msg))) return 0;
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->list = obj;
    d->kept_ent = -2;
    d->status = ZKLIST_NOT_SET_UP;
    d->utf8 = GetTagData(MUIA_ZkList_UTF8, FALSE, msg->ops_AttrList) != 0;
    d->set = (const zt_fontset *)GetTagData(MUIA_ZkList_FontSet, 0, msg->ops_AttrList);
    count_columns(d, obj);
    if (d->utf8 && ZkCellClass)
        d->cell = (Object *)NewObject(ZkCellClass->mcc_Class, NULL,
                                      ZKCELL_OWNER, (IPTR)d, MUIA_FillArea, FALSE,
                                      MUIA_Frame, MUIV_Frame_None,
                                      MUIA_InnerLeft, 0, MUIA_InnerRight, 0,
                                      MUIA_InnerTop, 0, MUIA_InnerBottom, 0, TAG_DONE);
    return (IPTR)obj;
}

static IPTR ZkList_Dispose(struct IClass *cl, Object *obj, Msg msg)
{
    struct ZkListData *d = INST_DATA(cl, obj);
    /* NList gave the object back at Cleanup (MUIM_NList_DeleteImage) */
    if (d->cell) MUI_DisposeObject(d->cell);
    d->cell = NULL;
    free_texts(d);
    return DoSuperMethodA(cl, obj, msg);
}

static IPTR ZkList_Set(struct IClass *cl, Object *obj, struct opSet *msg)
{
    struct ZkListData *d = INST_DATA(cl, obj);
    IPTR r = DoSuperMethodA(cl, obj, (Msg)msg);
    if (FindTagItem(MUIA_NList_Format, msg->ops_AttrList)) count_columns(d, obj);
    return r;
}

static IPTR ZkList_Get(struct IClass *cl, Object *obj, struct opGet *msg)
{
    struct ZkListData *d = INST_DATA(cl, obj);
    switch (msg->opg_AttrID) {
    case MUIA_ZkList_UTF8: *msg->opg_Storage = (IPTR)d->utf8; return TRUE;
    case MUIA_ZkList_Status: *msg->opg_Storage = (IPTR)d->status; return TRUE;
    }
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

static IPTR ZkList_Setup(struct IClass *cl, Object *obj, Msg msg)
{
    struct ZkListData *d = INST_DATA(cl, obj);
    struct TextFont *tf;
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    if (!d->utf8) return TRUE;
    zt_charset_refresh();                   /* a changed CHARSET is seen at each window opening */
    memset(d->wc, 0, sizeof d->wc);
    d->kept_ent = -2;
    tf = _font(obj);
    if (layer) d->font = zt_open_for(tf, tf ? tf->tf_YSize : 13, d->set ? d->set : zt_system_fontset(), 0, NULL);
    if (d->font && d->cell) d->img = (APTR)DoMethod(obj, MUIM_NList_CreateImage, (IPTR)d->cell, 0);
    if (!d->img && d->font) { zt_font_close(d->font); d->font = NULL; }
    set_status(d, d->img ? ZKLIST_DRAWN : ZKLIST_COPIES);
    return TRUE;
}

static IPTR ZkList_Cleanup(struct IClass *cl, Object *obj, Msg msg)
{
    struct ZkListData *d = INST_DATA(cl, obj);
    if (d->img) DoMethod(obj, MUIM_NList_DeleteImage, (IPTR)d->img);
    d->img = NULL;
    zt_font_close(d->font);
    d->font = NULL;
    d->kept_ent = -2;
    if (d->utf8) set_status(d, ZKLIST_NOT_SET_UP);
    return DoSuperMethodA(cl, obj, msg);
}

/* NList's display first (the program's hook), then each column's text
   kept and an image escape given in its place; or, without faces, an
   8-bit copy. */
static IPTR ZkList_Display(struct IClass *cl, Object *obj, struct MUIP_NList_Display *msg)
{
    struct ZkListData *d = INST_DATA(cl, obj);
    IPTR r = DoSuperMethodA(cl, obj, (Msg)msg);
    int c;
    if (!d->utf8 || !msg->strings) return r;
    for (c = 0; c < d->ncols && c < ZK_COLS; c++) {
        const char *s = (const char *)msg->strings[c];
        size_t n, lead;
        if (!s || !*s) continue;
        n = strlen(s);
        if (!d->img) {
            /* no faces: the system's set, '?' for what it lacks, said in
               the status rather than shown as if it were the text */
            size_t cn;
            free(d->copy[c]);
            d->copy[c] = zt_local_copy(s, n, &cn);
            msg->strings[c] = (STRPTR)(d->copy[c] ? d->copy[c] : "");
            continue;
        }
        lead = zl_lead(s, n);
        if (lead > 16) lead = 16;
        if (keep(d, c, s + lead, n - lead)) {
            LONG w = text_width(d, d->kept[c], d->kept_len[c]);
            if (w >= 0) {
                /* the width as ",minx" after the tag: NList reads a tag's
                   value up to ']', ';' or ',' only, so a "|dx" there
                   would be taken into the value */
                snprintf(d->esc[c], sizeof d->esc[c], "%.*s\033O[%lx;%lx;%lx,%ld]", (int)lead, s,
                         (unsigned long)(IPTR)d->img, (unsigned long)ZKCELL_COL, (unsigned long)c, (long)w);
                msg->strings[c] = (STRPTR)d->esc[c];
                continue;
            }
        }
        /* not kept (memory): a marked place, never the bytes as 8-bit text */
        snprintf(d->esc[c], sizeof d->esc[c], "\033O[%lx;%lx;%lx,%d]", (unsigned long)(IPTR)d->img,
                 (unsigned long)ZKCELL_COL, ZKCELL_LOST, ZK_STALE_W);
        msg->strings[c] = (STRPTR)d->esc[c];
    }
    d->kept_ent = msg->entry_pos;
    return r;
}

BOOPSI_DISPATCHER(IPTR, ZkListDispatcher, cl, obj, msg)
{
    switch (msg->MethodID) {
    case OM_NEW: return ZkList_New(cl, obj, (struct opSet *)msg);
    case OM_DISPOSE: return ZkList_Dispose(cl, obj, msg);
    case OM_SET: return ZkList_Set(cl, obj, (struct opSet *)msg);
    case OM_GET: return ZkList_Get(cl, obj, (struct opGet *)msg);
    case MUIM_Setup: return ZkList_Setup(cl, obj, msg);
    case MUIM_Cleanup: return ZkList_Cleanup(cl, obj, msg);
    case MUIM_NList_Display: return ZkList_Display(cl, obj, (struct MUIP_NList_Display *)msg);
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

int zk_list_init(void)
{
    if (ZkListClass) return 1;
    if (!layer) layer = zt_layer_open() == ZT_OK;
    ZkCellClass = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(struct ZkCellData), ZkCellDispatcher);
    ZkListClass = MUI_CreateCustomClass(NULL, (ClassID)MUIC_NList, NULL, sizeof(struct ZkListData), ZkListDispatcher);
    if (!ZkListClass) {
        if (ZkCellClass) MUI_DeleteCustomClass(ZkCellClass);
        ZkCellClass = NULL;
        if (layer) zt_layer_close();
        layer = 0;
    }
    return ZkListClass != NULL;
}

void zk_list_exit(void)
{
    if (ZkListClass) { MUI_DeleteCustomClass(ZkListClass); ZkListClass = NULL; }
    if (ZkCellClass) { MUI_DeleteCustomClass(ZkCellClass); ZkCellClass = NULL; }
    if (layer) zt_layer_close();
    layer = 0;
}

struct MUI_CustomClass *zk_list_class(void) { return ZkListClass; }

Object *zk_list_new(const struct TagItem *tags)
{
    if (!ZkListClass) return NULL;
    return (Object *)NewObjectA(ZkListClass->mcc_Class, NULL, (struct TagItem *)tags);
}
