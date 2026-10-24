/* SPDX-License-Identifier: MIT */
/* zk_drop: see zk_drop.h.
 *
 * Why its own class: Zune's frames have no dashed style, which is how a drop
 * target is usually told apart from a text field, and a Text object cannot
 * hold the arrow. Everything is sized from the font, so the area grows with
 * it, as zk_steps does. */
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <graphics/gfxmacros.h>
#include <clib/alib_protos.h>
#include <aros/asmcall.h>
#include <string.h>
#include "zk_drop.h"

#define ZKA_Drop_Title (TAG_USER | 0x5A4B0201)   /* private: the first line */
#define ZKA_Drop_Text  (TAG_USER | 0x5A4B0202)   /* private: the second line */

struct zd_data {
    char title[160];
    char text[200];
};

static struct MUI_CustomClass *zd_class;

static void copy_line(char *to, size_t n, const char *from)
{
    if (!from)
        return;
    strncpy(to, from, n - 1);
    to[n - 1] = 0;
}

/* The arrow's height and the space around it, from the font. */
static int fh(Object *obj) { return _font(obj)->tf_YSize; }
static int arrow_h(Object *obj) { return fh(obj) * 2; }
static int pad(Object *obj) { return fh(obj) * 3 / 4 + 2; }

/* Measured on a RastPort of its own, as Zune's text engine does
 * (textengine.c): the window's may not exist yet in AskMinMax. */
static int text_w(Object *obj, const char *s, int bold)
{
    struct RastPort rp;
    InitRastPort(&rp);
    SetFont(&rp, _font(obj));
    if (bold)
        SetSoftStyle(&rp, FSF_BOLD, AskSoftStyle(&rp));
    return TextLength(&rp, (STRPTR)s, strlen(s));
}

/* One line centred between l and r, cut to fit. */
static void centre(struct RastPort *rp, const char *s, int l, int r, int baseline)
{
    struct TextExtent te;
    ULONG n = strlen(s), fit;
    if (!n || r <= l)
        return;
    fit = TextFit(rp, (STRPTR)s, n, &te, NULL, 1, r - l, 32767);
    if (!fit)
        return;
    Move(rp, l + (r - l - te.te_Width) / 2, baseline);
    Text(rp, (STRPTR)s, fit);
}

static void zd_draw(Object *obj, struct zd_data *d)
{
    struct RastPort *rp = _rp(obj);
    UWORD *pens = _pens(obj);
    int l = _mleft(obj), t = _mtop(obj), r = _mright(obj), b = _mbottom(obj);
    int f = fh(obj), p = pad(obj), ah = arrow_h(obj);
    int lines = (d->title[0] ? 1 : 0) + (d->text[0] ? 1 : 0);
    int block = ah + (lines ? f / 2 + lines * (f + 2) : 0);
    int y = t + (b - t + 1 - block) / 2, cx = (l + r) / 2, i;
    int base = _font(obj)->tf_Baseline;

    /* Dashed border, one pixel inside the area. */
    SetAPen(rp, pens[MPEN_SHADOW]);
    SetDrPt(rp, 0xF0F0);
    Move(rp, l + 1, t + 1);
    Draw(rp, r - 1, t + 1);
    Draw(rp, r - 1, b - 1);
    Draw(rp, l + 1, b - 1);
    Draw(rp, l + 1, t + 1);
    SetDrPt(rp, 0xFFFF);

    /* Arrow down into a tray: shaft, head, tray. */
    {
        int sw = f / 6 + 1, hw = f / 2 + 1, tw = f;
        int ty = y + ah - 1, hy = y + ah * 5 / 8;
        SetAPen(rp, pens[MPEN_SHADOW]);
        RectFill(rp, cx - sw, y, cx + sw, hy);
        for (i = 0; i <= hw; i++) {
            Move(rp, cx - hw + i, hy + i);
            Draw(rp, cx + hw - i, hy + i);
        }
        RectFill(rp, cx - tw, ty - 1, cx + tw, ty);
        RectFill(rp, cx - tw, ty - f / 3, cx - tw + 1, ty);
        RectFill(rp, cx + tw - 1, ty - f / 3, cx + tw, ty);
    }

    y += ah + f / 2;
    SetAPen(rp, pens[MPEN_TEXT]);
    SetDrMd(rp, JAM1);
    if (d->title[0]) {
        SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
        centre(rp, d->title, l + p, r - p, y + base);
        SetSoftStyle(rp, FS_NORMAL, AskSoftStyle(rp));
        y += f + 2;
    }
    if (d->text[0])
        centre(rp, d->text, l + p, r - p, y + base);
}

BOOPSI_DISPATCHER(IPTR, zd_dispatcher, cl, obj, msg)
{
    struct zd_data *d;
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *o = (Object *)DoSuperMethodA(cl, obj, msg);
        if (o) {
            struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList;
            d = INST_DATA(cl, o);
            copy_line(d->title, sizeof(d->title), (const char *)GetTagData(ZKA_Drop_Title, 0, tags));
            copy_line(d->text, sizeof(d->text), (const char *)GetTagData(ZKA_Drop_Text, 0, tags));
        }
        return (IPTR)o;
    }
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        int f = fh(obj), w, h;
        d = INST_DATA(cl, obj);
        w = text_w(obj, d->title, 1);
        if (text_w(obj, d->text, 0) > w)
            w = text_w(obj, d->text, 0);
        w += 2 * pad(obj);
        h = 2 * pad(obj) + arrow_h(obj) + f / 2 + 2 * (f + 2);
        mm->MinWidth += w;
        mm->DefWidth += w > f * 24 ? w : f * 24;
        mm->MaxWidth = MUI_MAXMAX;
        mm->MinHeight += h;
        mm->DefHeight += h + f;
        mm->MaxHeight = MUI_MAXMAX;
        return r;
    }
    case MUIM_Draw: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) {
            /* An update (new text) clears the old one first; a full draw
             * already has the background from Area. */
            if (((struct MUIP_Draw *)msg)->flags & MADF_DRAWUPDATE)
                DoMethod(obj, MUIM_DrawBackground, _mleft(obj), _mtop(obj), _mwidth(obj),
                         _mheight(obj), _mleft(obj), _mtop(obj), 0);
            zd_draw(obj, INST_DATA(cl, obj));
        }
        return r;
    }
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

int zk_drop_init(void)
{
    if (!zd_class)
        zd_class = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct zd_data),
                                         (APTR)zd_dispatcher);
    return zd_class != NULL;
}

void zk_drop_exit(void)
{
    if (zd_class) {
        MUI_DeleteCustomClass(zd_class);
        zd_class = NULL;
    }
}

Object *zk_drop_new(const char *title, const char *text, const struct TagItem *extra)
{
    if (!zd_class)
        return NULL;
    return NewObject(zd_class->mcc_Class, NULL,
                     ZKA_Drop_Title, (IPTR)title,
                     ZKA_Drop_Text, (IPTR)text,
                     MUIA_FillArea, TRUE,
                     extra ? TAG_MORE : TAG_DONE, (IPTR)extra);
}

void zk_drop_set(Object *obj, const char *title, const char *text)
{
    struct zd_data *d;
    if (!obj || !zd_class)
        return;
    d = INST_DATA(zd_class->mcc_Class, obj);
    copy_line(d->title, sizeof(d->title), title);
    copy_line(d->text, sizeof(d->text), text);
    MUI_Redraw(obj, MADF_DRAWUPDATE);
}
