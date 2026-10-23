/* SPDX-License-Identifier: MIT */
/* zk_steps: see zk_steps.h.
 *
 * Why a subclass of Numeric and not of Slider: Slider keeps its knob
 * rectangle, scale length and drag offset in private instance data that only
 * its own MUIM_Draw computes (slider.c), so drawing ticks under it or moving
 * its knob to whole steps would leave its hit test and drag on a different
 * geometry. Numeric keeps what is standard: the value, min and max, the
 * notifications, cursor keys and the wheel (Numeric__MUIM_HandleEvent). The
 * idea comes from Boxer's slider (aros-boxerapp, BxSlider); the drawing here
 * uses the preferences' pens and the font instead of fixed colours and sizes,
 * and the steps are the values themselves, not a snap done by the mouse code. */
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include <aros/asmcall.h>
#include <devices/inputevent.h>
#include <stdio.h>
#include "zk_steps.h"

struct zs_data {
    struct MUI_EventHandlerNode ehn;
    int added;
    int dragging;
};

static struct MUI_CustomClass *zs_class;
static void (*zs_trace)(const char *line);

void zk_steps_set_trace(void (*fn)(const char *line))
{
    zs_trace = fn;
}

/* Sizes from the font, so the slider grows with it: the knob is a little
 * taller than a line of text and half as wide; the ticks are a quarter. */
static int knob_w(Object *obj) { return _font(obj)->tf_YSize / 2 + 4; }
static int knob_h(Object *obj) { return _font(obj)->tf_YSize + 2; }
static int tick_h(Object *obj) { return _font(obj)->tf_YSize / 4 + 2; }

static LONG steps_max(Object *obj)
{
    IPTR mx = 0;
    get(obj, MUIA_Numeric_Max, &mx);
    return (LONG)mx;
}

static LONG steps_value(Object *obj)
{
    IPTR v = 0;
    get(obj, MUIA_Numeric_Value, &v);
    return (LONG)v;
}

/* The x of the knob's left edge for step i. */
static int step_x(Object *obj, LONG i)
{
    LONG mx = steps_max(obj);
    int span = _mwidth(obj) - knob_w(obj);
    return _mleft(obj) + (mx > 0 ? (int)(span * i / mx) : 0);
}

/* The step nearest to a pointer x. */
static LONG nearest_step(Object *obj, int x)
{
    LONG mx = steps_max(obj), i;
    int span = _mwidth(obj) - knob_w(obj);
    if (mx <= 0 || span <= 0)
        return 0;
    x -= _mleft(obj) + knob_w(obj) / 2;
    i = (LONG)((2 * (LONG)x * mx + span) / (2 * span));
    return i < 0 ? 0 : i > mx ? mx : i;
}

static void box(struct RastPort *rp, int l, int t, int r, int b, ULONG lt, ULONG rb)
{
    SetAPen(rp, lt);
    Move(rp, l, b);
    Draw(rp, l, t);
    Draw(rp, r, t);
    SetAPen(rp, rb);
    Draw(rp, r, b);
    Draw(rp, l + 1, b);
}

static void zs_draw(Object *obj, struct zs_data *d)
{
    struct RastPort *rp = _rp(obj);
    UWORD *pens = _pens(obj);
    int kw = knob_w(obj), kh = knob_h(obj), th = tick_h(obj);
    int l = _mleft(obj), t = _mtop(obj), cy = t + kh / 2;
    int gl = l + kw / 2, gr = l + _mwidth(obj) - 1 - kw / 2;
    LONG mx = steps_max(obj), v = steps_value(obj), i;
    int kx = step_x(obj, v);

    /* Groove: recessed, the part up to the knob filled like a gauge. */
    box(rp, gl, cy - 2, gr, cy + 2, pens[MPEN_SHADOW], pens[MPEN_SHINE]);
    SetAPen(rp, pens[MPEN_HALFSHADOW]);
    RectFill(rp, gl + 1, cy - 1, gr - 1, cy + 1);
    if (kx + kw / 2 > gl + 1) {
        SetAPen(rp, pens[MPEN_FILL]);
        RectFill(rp, gl + 1, cy - 1, kx + kw / 2, cy + 1);
    }
    /* A tick under every step. */
    SetAPen(rp, pens[MPEN_SHADOW]);
    for (i = 0; i <= mx; i++) {
        int x = step_x(obj, i) + kw / 2;
        Move(rp, x, t + kh + 1);
        Draw(rp, x, t + kh + th);
    }
    /* Knob: raised, sunken while it is held, with a grip line. */
    SetAPen(rp, pens[d->dragging ? MPEN_HALFSHADOW : MPEN_HALFSHINE]);
    RectFill(rp, kx + 1, t + 1, kx + kw - 2, t + kh - 2);
    box(rp, kx, t, kx + kw - 1, t + kh - 1, pens[d->dragging ? MPEN_SHADOW : MPEN_SHINE],
        pens[d->dragging ? MPEN_SHINE : MPEN_SHADOW]);
    SetAPen(rp, pens[MPEN_SHADOW]);
    Move(rp, kx + kw / 2, t + 3);
    Draw(rp, kx + kw / 2, t + kh - 4);
}

static void set_from_x(Object *obj, int x)
{
    LONG i = nearest_step(obj, x);
    if (i != steps_value(obj))
        set(obj, MUIA_Numeric_Value, i);
}

/* Test aid: what a press saw, against the geometry it was measured on. */
static void trace_press(Object *obj, int x, LONG step)
{
    char b[160], *p = b;
    LONG i, mx = steps_max(obj);
    if (!zs_trace)
        return;
    p += snprintf(b, sizeof(b), "press MouseX=%d left=%d width=%d knob=%d ticks", x, _mleft(obj),
                  _mwidth(obj), knob_w(obj));
    for (i = 0; i <= mx && p < b + sizeof(b) - 8; i++)
        p += snprintf(p, b + sizeof(b) - p, "%s%d", i ? "," : "=", step_x(obj, i) + knob_w(obj) / 2);
    snprintf(p, b + sizeof(b) - p, " -> step %ld", (long)step);
    zs_trace(b);
}

/* A drag ends when the button is let go. Zune gives a hidden or disabled
 * object no input, so that release can be missed: the drag also ends when
 * the object is hidden or disabled, and a move without the button held. */
static void end_drag(Object *obj, struct zs_data *d, int redraw)
{
    if (!d->dragging)
        return;
    d->dragging = 0;
    if (redraw)
        MUI_Redraw(obj, MADF_DRAWUPDATE);
}

BOOPSI_DISPATCHER(IPTR, zs_dispatcher, cl, obj, msg)
{
    struct zs_data *d;
    switch (msg->MethodID) {
    case OM_SET: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        struct TagItem *t = FindTagItem(MUIA_Disabled, ((struct opSet *)msg)->ops_AttrList);
        if (t && t->ti_Data)
            end_drag(obj, INST_DATA(cl, obj), 0);
        return r;
    }
    case MUIM_Hide:
        end_drag(obj, INST_DATA(cl, obj), 0);
        break;
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        int fh = _font(obj)->tf_YSize, h = knob_h(obj) + 1 + tick_h(obj);
        LONG n = steps_max(obj) + 1;
        int minw = (int)n * (knob_w(obj) + 4);
        if (minw < fh * 4)
            minw = fh * 4;
        mm->MinWidth += minw;
        mm->DefWidth += fh * 12 > minw ? fh * 12 : minw;
        mm->MaxWidth = MUI_MAXMAX;
        mm->MinHeight += h;
        mm->DefHeight += h;
        mm->MaxHeight += h;
        return r;
    }
    case MUIM_Setup:
        if (!DoSuperMethodA(cl, obj, msg))
            return FALSE;
        d = INST_DATA(cl, obj);
        d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE;
        d->ehn.ehn_Priority = 1;
        d->ehn.ehn_Flags = MUI_EHF_GUIMODE;
        d->ehn.ehn_Object = obj;
        d->ehn.ehn_Class = cl;
        DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&d->ehn);
        d->added = 1;
        return TRUE;
    case MUIM_Cleanup:
        d = INST_DATA(cl, obj);
        if (d->added) {
            DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn);
            d->added = 0;
        }
        d->dragging = 0;
        break;
    case MUIM_HandleEvent: {
        /* Only this class's handler arrives here: Numeric registers its key
         * handler with its own class. */
        struct MUIP_HandleEvent *he = (struct MUIP_HandleEvent *)msg;
        IPTR dis = 0;
        int x, y;
        d = INST_DATA(cl, obj);
        if (!he->imsg)
            return 0;
        x = he->imsg->MouseX;
        y = he->imsg->MouseY;
        if (he->imsg->Class == IDCMP_MOUSEMOVE && d->dragging) {
            if (!(he->imsg->Qualifier & IEQUALIFIER_LEFTBUTTON)) {
                end_drag(obj, d, 1);
                return 0;
            }
            set_from_x(obj, x);
            return MUI_EventHandlerRC_Eat;
        }
        if (he->imsg->Class != IDCMP_MOUSEBUTTONS)
            return 0;
        get(obj, MUIA_Disabled, &dis);
        if (he->imsg->Code == SELECTDOWN && !dis && x >= _mleft(obj) && x <= _mright(obj) &&
            y >= _mtop(obj) && y <= _mbottom(obj)) {
            d->dragging = 1;
            trace_press(obj, x, nearest_step(obj, x));
            if (nearest_step(obj, x) != steps_value(obj))
                set_from_x(obj, x);
            else
                MUI_Redraw(obj, MADF_DRAWUPDATE);
            return MUI_EventHandlerRC_Eat;
        }
        if (he->imsg->Code == SELECTUP && d->dragging) {
            end_drag(obj, d, 1);
            return MUI_EventHandlerRC_Eat;
        }
        return 0;
    }
    case MUIM_Draw: {
        struct MUIP_Draw *dm = (struct MUIP_Draw *)msg;
        IPTR r = DoSuperMethodA(cl, obj, msg);
        if (!(dm->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)))
            return r;
        /* An update (new value, knob held or let go) clears the old knob
         * first; a full draw already has the background from Area. */
        if (dm->flags & MADF_DRAWUPDATE)
            DoMethod(obj, MUIM_DrawBackground, _mleft(obj), _mtop(obj), _mwidth(obj),
                     _mheight(obj), _mleft(obj), _mtop(obj), 0);
        zs_draw(obj, INST_DATA(cl, obj));
        return r;
    }
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

int zk_steps_init(void)
{
    if (!zs_class)
        zs_class = MUI_CreateCustomClass(NULL, MUIC_Numeric, NULL, sizeof(struct zs_data),
                                         (APTR)zs_dispatcher);
    return zs_class != NULL;
}

void zk_steps_exit(void)
{
    if (zs_class) {
        MUI_DeleteCustomClass(zs_class);
        zs_class = NULL;
    }
}

Object *zk_steps_new(ULONG count, ULONG value, const struct TagItem *extra)
{
    if (!zs_class || count < 2)
        return NULL;
    return NewObject(zs_class->mcc_Class, NULL,
                     MUIA_Numeric_Min, 0,
                     MUIA_Numeric_Max, count - 1,
                     MUIA_Numeric_Value, value < count ? value : count - 1,
                     MUIA_FillArea, TRUE,
                     extra ? TAG_MORE : TAG_DONE, (IPTR)extra);
}
