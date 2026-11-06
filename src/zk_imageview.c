/* SPDX-License-Identifier: MIT */
/* zk_imageview: see zk_imageview.h.
 *
 * Drawing: the view keeps one ARGB buffer of its own size with the picture
 * as shown (zoomed, placed, background around it), writes it with
 * WritePixelArray into an off-screen bitmap (a friend of the window's),
 * lets the program's DrawHook draw on top, and copies the result into the
 * window. The buffer is composed again only after a zoom, a scroll, a new
 * size or zk_imageview_changed(), and then only the part that changed; a
 * refresh of the window copies the bitmap.
 *
 * Scaling: from 100 % every view pixel takes the nearest picture pixel
 * (a column and a row table per layout). Below 100 % the picture is
 * scaled down once by box averaging into a second buffer, which is kept
 * until the zoom or the pixels change; scrolling then only copies. The
 * arithmetic is in zk_imageview_core.c. */
#include <string.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/cybergraphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <cybergraphx/cybergraphics.h>
#include <devices/inputevent.h>
#include <devices/rawkeycodes.h>
#include <graphics/rpattr.h>
#include <clib/alib_protos.h>
#include <aros/asmcall.h>
#include "zk_imageview.h"
#include "zk_imageview_core.h"

#define RAWKEY_SPACE_ 0x40
#define RAWKEY_ESC_   0x45

struct iv_data {
    UBYTE *pix;
    LONG pw, ph;
    LONG zoom, shown;
    long sx, sy;                    /* scroll, in shown pixels */
    LONG ox, oy;                    /* view position of picture point 0,0 */
    UBYTE bg[4];
    struct Hook *draw_hook, *input_hook;
    LONG px, py;
    UBYTE *scaled;                  /* the picture below 100 % */
    LONG sw, sh, scaled_zoom;
    UBYTE *vbuf;                    /* the view, composed */
    LONG vw, vh;
    LONG *cmap, *rmap;              /* view column/row -> source column/row, -1 */
    struct BitMap *bm;
    struct RastPort brp;
    int bm_ok;                      /* bm shows vbuf and the program's layer */
    int dirty;
    struct Rectangle dr;            /* part to compose again, view coordinates */
    struct MUI_EventHandlerNode ehn;
    int added, shown_ok;
    int space, pan, press;
    WORD mx0, my0;
    long sx0, sy0;
};

static struct MUI_CustomClass *iv_class;

/* ---- composing ---- */

static void box_rows(struct iv_data *d, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG x, y;
    for (y = y0; y < y1; y++) {
        long r0, r1;
        UBYTE *out = d->scaled + ((size_t)y * d->sw + x0) * 4;
        zk_iv_box(y, d->ph, d->shown, &r0, &r1);
        for (x = x0; x < x1; x++, out += 4) {
            long c0, c1, r, c;
            ULONG a = 0, rr = 0, g = 0, b = 0, n;
            zk_iv_box(x, d->pw, d->shown, &c0, &c1);
            for (r = r0; r < r1; r++) {
                const UBYTE *p = d->pix + ((size_t)r * d->pw + c0) * 4;
                for (c = c0; c < c1; c++, p += 4) {
                    a += p[0];
                    rr += p[1];
                    g += p[2];
                    b += p[3];
                }
            }
            n = (ULONG)((r1 - r0) * (c1 - c0));
            out[0] = (UBYTE)(a / n);
            out[1] = (UBYTE)(rr / n);
            out[2] = (UBYTE)(g / n);
            out[3] = (UBYTE)(b / n);
        }
    }
}

/* The scaled-down picture for the shown zoom, made when missing. */
static void ensure_scaled(struct iv_data *d)
{
    if (!d->pix || d->shown >= 100) {
        FreeVec(d->scaled);
        d->scaled = NULL;
        d->scaled_zoom = 0;
        return;
    }
    if (d->scaled && d->scaled_zoom == d->shown)
        return;
    FreeVec(d->scaled);
    d->sw = zk_iv_scaled(d->pw, d->shown);
    d->sh = zk_iv_scaled(d->ph, d->shown);
    d->scaled = AllocVec((size_t)d->sw * d->sh * 4, MEMF_ANY);
    d->scaled_zoom = d->scaled ? d->shown : 0;
    if (d->scaled)
        box_rows(d, 0, 0, d->sw, d->sh);
}

static void make_maps(struct iv_data *d)
{
    LONG i, lim;
    int down = d->shown < 100 && d->scaled;
    if (!d->cmap || !d->rmap)
        return;
    lim = down ? d->sw : d->pw;
    for (i = 0; i < d->vw; i++) {
        long s = down ? i - d->ox : zk_iv_to_pic(i, d->ox, d->shown);
        d->cmap[i] = (d->pix && s >= 0 && s < lim) ? (LONG)s : -1;
    }
    lim = down ? d->sh : d->ph;
    for (i = 0; i < d->vh; i++) {
        long s = down ? i - d->oy : zk_iv_to_pic(i, d->oy, d->shown);
        d->rmap[i] = (d->pix && s >= 0 && s < lim) ? (LONG)s : -1;
    }
}

static void compose(struct iv_data *d, LONG x0, LONG y0, LONG x1, LONG y1)
{
    int down = d->shown < 100 && d->scaled;
    const UBYTE *src = down ? d->scaled : d->pix;
    LONG srcw = down ? d->sw : d->pw, x, y;
    const UBYTE *bg = d->bg;
    for (y = y0; y < y1; y++) {
        UBYTE *out = d->vbuf + ((size_t)y * d->vw + x0) * 4;
        LONG r = src ? d->rmap[y] : -1;
        const UBYTE *row = r >= 0 ? src + (size_t)r * srcw * 4 : NULL;
        for (x = x0; x < x1; x++, out += 4) {
            LONG c = row ? d->cmap[x] : -1;
            const UBYTE *p;
            ULONG a;
            if (c < 0) {
                memcpy(out, bg, 4);
                continue;
            }
            p = row + (size_t)c * 4;
            a = p[0];
            if (a == 255) {
                memcpy(out, p, 4);
                continue;
            }
            out[0] = 255;
            out[1] = (UBYTE)((p[1] * a + bg[1] * (255 - a) + 127) / 255);
            out[2] = (UBYTE)((p[2] * a + bg[2] * (255 - a) + 127) / 255);
            out[3] = (UBYTE)((p[3] * a + bg[3] * (255 - a) + 127) / 255);
        }
    }
}

/* Composes, writes and copies the view rectangle x0..x1-1, y0..y1-1. */
static void render(Object *obj, struct iv_data *d, LONG x0, LONG y0, LONG x1, LONG y1)
{
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > d->vw)
        x1 = d->vw;
    if (y1 > d->vh)
        y1 = d->vh;
    if (x1 <= x0 || y1 <= y0 || !d->bm || !d->vbuf)
        return;
    compose(d, x0, y0, x1, y1);
    WritePixelArray(d->vbuf, x0, y0, d->vw * 4, &d->brp, x0, y0, x1 - x0, y1 - y0,
                    RECTFMT_ARGB);
    if (d->draw_hook) {
        struct zk_imageview_draw_msg m;
        m.rp = &d->brp;
        m.left = d->ox;
        m.top = d->oy;
        m.shown = d->shown;
        m.clip.MinX = x0;
        m.clip.MinY = y0;
        m.clip.MaxX = x1 - 1;
        m.clip.MaxY = y1 - 1;
        /* Graphics clips a RastPort without a layer to this rectangle too
         * (AROS RPTAG_ClipRectangle), so a stray line stays out of parts
         * that are not composed again. */
        SetRPAttrs(&d->brp, RPTAG_ClipRectangle, (IPTR)&m.clip, TAG_DONE);
        CallHookPkt(d->draw_hook, obj, &m);
        SetRPAttrs(&d->brp, RPTAG_ClipRectangle, (IPTR)NULL, TAG_DONE);
    }
    BltBitMapRastPort(d->bm, x0, y0, _rp(obj), _mleft(obj) + x0, _mtop(obj) + y0, x1 - x0,
                      y1 - y0, 0xC0);
}

/* ---- layout ---- */

/* Zoom, scaled picture, placement and maps for the current size; notifies
 * a new shown zoom. Everything is composed again at the next draw. */
static void relayout(struct IClass *cl, Object *obj, struct iv_data *d)
{
    LONG old = d->shown;
    d->shown = d->zoom ? zk_iv_clamp(d->zoom) : zk_iv_fit(d->vw, d->vh, d->pw, d->ph);
    ensure_scaled(d);
    if (d->shown < 100 && !d->scaled && d->pix) {
        /* No memory for the scaled picture: show it at 100 %. */
        d->shown = 100;
    }
    d->ox = zk_iv_place(d->vw, zk_iv_scaled(d->pw, d->shown), &d->sx);
    d->oy = zk_iv_place(d->vh, zk_iv_scaled(d->ph, d->shown), &d->sy);
    make_maps(d);
    d->bm_ok = 0;
    d->dirty = 0;
    if (old != d->shown)
        SetSuperAttrs(cl, obj, MUIA_ZkImageView_Shown, d->shown, TAG_DONE);
}

static void free_view(struct iv_data *d)
{
    if (d->bm) {
        /* The clip rectangle's extra data (SetRPAttrs autodoc). */
        FreeVec(d->brp.RP_Extra);
        d->brp.RP_Extra = NULL;
        FreeBitMap(d->bm);
    }
    d->bm = NULL;
    FreeVec(d->vbuf);
    FreeVec(d->cmap);
    FreeVec(d->rmap);
    d->vbuf = NULL;
    d->cmap = d->rmap = NULL;
    d->vw = d->vh = 0;
    d->bm_ok = 0;
}

/* Buffers of the object's current size; 0 when there is no memory. */
static int ensure_view(struct IClass *cl, Object *obj, struct iv_data *d)
{
    LONG w = _mwidth(obj), h = _mheight(obj);
    ULONG depth;
    if (d->bm && d->vbuf && d->vw == w && d->vh == h)
        return 1;
    free_view(d);
    if (w <= 0 || h <= 0)
        return 0;
    depth = GetBitMapAttr(_rp(obj)->BitMap, BMA_DEPTH);
    d->bm = AllocBitMap(w, h, depth, BMF_MINPLANES, _rp(obj)->BitMap);
    d->vbuf = AllocVec((size_t)w * h * 4, MEMF_ANY);
    d->cmap = AllocVec(w * sizeof(LONG), MEMF_ANY);
    d->rmap = AllocVec(h * sizeof(LONG), MEMF_ANY);
    if (!d->bm || !d->vbuf || !d->cmap || !d->rmap) {
        free_view(d);
        return 0;
    }
    d->vw = w;
    d->vh = h;
    InitRastPort(&d->brp);
    d->brp.BitMap = d->bm;
    SetFont(&d->brp, _font(obj));
    relayout(cl, obj, d);
    return 1;
}

static void redraw_all(Object *obj, struct iv_data *d)
{
    d->bm_ok = 0;
    MUI_Redraw(obj, MADF_DRAWUPDATE);
}

static void add_dirty(struct iv_data *d, LONG x0, LONG y0, LONG x1, LONG y1)
{
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > d->vw)
        x1 = d->vw;
    if (y1 > d->vh)
        y1 = d->vh;
    if (x1 <= x0 || y1 <= y0)
        return;
    if (!d->dirty) {
        d->dr.MinX = x0;
        d->dr.MinY = y0;
        d->dr.MaxX = x1 - 1;
        d->dr.MaxY = y1 - 1;
        d->dirty = 1;
        return;
    }
    if (x0 < d->dr.MinX)
        d->dr.MinX = x0;
    if (y0 < d->dr.MinY)
        d->dr.MinY = y0;
    if (x1 - 1 > d->dr.MaxX)
        d->dr.MaxX = x1 - 1;
    if (y1 - 1 > d->dr.MaxY)
        d->dr.MaxY = y1 - 1;
}

/* A new zoom (0: fit) with the picture point at view position vx, vy kept
 * where it is. Notifies MUIA_ZkImageView_Zoom when notify is set. */
static void set_zoom(struct IClass *cl, Object *obj, struct iv_data *d, LONG zoom, LONG vx,
                     LONG vy, int notify)
{
    LONG old = d->shown, nz = zoom ? zk_iv_clamp(zoom) : 0, ns;
    int changed = nz != d->zoom;
    d->zoom = nz;
    if (!d->vw || !d->vh) {
        if (changed && notify)
            SetSuperAttrs(cl, obj, MUIA_ZkImageView_Zoom, d->zoom, TAG_DONE);
        return;
    }
    ns = nz ? nz : zk_iv_fit(d->vw, d->vh, d->pw, d->ph);
    if (old > 0 && ns != old) {
        d->sx = zk_iv_scroll_about(vx, d->ox, old, ns);
        d->sy = zk_iv_scroll_about(vy, d->oy, old, ns);
    }
    relayout(cl, obj, d);
    if (changed && notify)
        SetSuperAttrs(cl, obj, MUIA_ZkImageView_Zoom, d->zoom, TAG_DONE);
    redraw_all(obj, d);
}

static void scroll_to(struct IClass *cl, Object *obj, struct iv_data *d, long sx, long sy)
{
    long ox = d->sx, oy = d->sy;
    d->sx = sx;
    d->sy = sy;
    d->ox = zk_iv_place(d->vw, zk_iv_scaled(d->pw, d->shown), &d->sx);
    d->oy = zk_iv_place(d->vh, zk_iv_scaled(d->ph, d->shown), &d->sy);
    (void)cl;
    if (d->sx == ox && d->sy == oy)
        return;
    make_maps(d);
    redraw_all(obj, d);
}

/* ---- input ---- */

static int inside(Object *obj, WORD x, WORD y)
{
    return x >= _mleft(obj) && x <= _mright(obj) && y >= _mtop(obj) && y <= _mbottom(obj);
}

static void track(struct IClass *cl, Object *obj, struct iv_data *d, WORD mx, WORD my)
{
    LONG x = -1, y = -1;
    struct TagItem t[3];
    int n = 0;
    if (d->pix && inside(obj, mx, my)) {
        x = zk_iv_to_pic(mx - _mleft(obj), d->ox, d->shown);
        y = zk_iv_to_pic(my - _mtop(obj), d->oy, d->shown);
        if (x < 0 || y < 0 || x >= d->pw || y >= d->ph)
            x = y = -1;
    }
    if (x != d->px) {
        d->px = x;
        t[n].ti_Tag = MUIA_ZkImageView_PointerX;
        t[n++].ti_Data = (IPTR)(SIPTR)x;
    }
    if (y != d->py) {
        d->py = y;
        t[n].ti_Tag = MUIA_ZkImageView_PointerY;
        t[n++].ti_Data = (IPTR)(SIPTR)y;
    }
    if (!n)
        return;
    t[n].ti_Tag = TAG_DONE;
    t[n].ti_Data = 0;
    DoSuperMethod(cl, obj, OM_SET, (IPTR)t, (IPTR)NULL);
}

static IPTR call_input(Object *obj, struct iv_data *d, ULONG ev, struct IntuiMessage *im)
{
    struct zk_imageview_input_msg m;
    if (!d->input_hook)
        return 0;
    m.event = ev;
    m.x = zk_iv_to_pic(im->MouseX - _mleft(obj), d->ox, d->shown);
    m.y = zk_iv_to_pic(im->MouseY - _mtop(obj), d->oy, d->shown);
    m.qualifier = im->Qualifier;
    return CallHookPkt(d->input_hook, obj, &m);
}

static void start_pan(Object *obj, struct iv_data *d, struct IntuiMessage *im, int how)
{
    (void)obj;
    d->pan = how;
    d->mx0 = im->MouseX;
    d->my0 = im->MouseY;
    d->sx0 = d->sx;
    d->sy0 = d->sy;
}

static char key_char(struct IntuiMessage *im)
{
    struct InputEvent ie;
    char buf[4];
    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = im->Code;
    ie.ie_Qualifier = im->Qualifier;
    ie.ie_EventAddress = NULL;
    if (MapRawKey(&ie, (STRPTR)buf, sizeof(buf), NULL) != 1)
        return 0;
    return buf[0];
}

static IPTR handle_key(struct IClass *cl, Object *obj, struct iv_data *d, struct IntuiMessage *im)
{
    UWORD code = im->Code & ~IECODE_UP_PREFIX;
    int up = (im->Code & IECODE_UP_PREFIX) != 0;
    IPTR a = 0;
    char c;

    if (code == RAWKEY_SPACE_) {
        /* Followed, not taken: Space alone means nothing to the view. */
        d->space = !up;
        return 0;
    }
    if (up)
        return 0;
    if (code == RAWKEY_ESC_ && d->press) {
        d->press = 0;
        call_input(obj, d, ZK_IV_CANCEL, im);
        return MUI_EventHandlerRC_Eat;
    }
    if (code == RAWKEY_NM_WHEEL_UP || code == RAWKEY_NM_WHEEL_DOWN ||
        code == RAWKEY_NM_WHEEL_LEFT || code == RAWKEY_NM_WHEEL_RIGHT) {
        int back = code == RAWKEY_NM_WHEEL_UP || code == RAWKEY_NM_WHEEL_LEFT;
        int horiz = code == RAWKEY_NM_WHEEL_LEFT || code == RAWKEY_NM_WHEEL_RIGHT ||
                    (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT));
        long step;
        if (!inside(obj, im->MouseX, im->MouseY))
            return 0;
        if (im->Qualifier & IEQUALIFIER_CONTROL) {
            if (!horiz)
                set_zoom(cl, obj, d, zk_iv_step(d->shown, back ? 1 : -1),
                         im->MouseX - _mleft(obj), im->MouseY - _mtop(obj), 1);
            track(cl, obj, d, im->MouseX, im->MouseY);
            return MUI_EventHandlerRC_Eat;
        }
        step = (horiz ? d->vw : d->vh) / 8;
        if (step < 16)
            step = 16;
        if (back)
            step = -step;
        if (horiz)
            scroll_to(cl, obj, d, d->sx + step, d->sy);
        else
            scroll_to(cl, obj, d, d->sx, d->sy + step);
        track(cl, obj, d, im->MouseX, im->MouseY);
        return MUI_EventHandlerRC_Eat;
    }
    if (im->Qualifier & (IEQUALIFIER_CONTROL | IEQUALIFIER_LALT | IEQUALIFIER_RALT |
                         IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))
        return 0;
    /* Zoom keys while the view has the keyboard, or no object has. */
    GetAttr(MUIA_Window_ActiveObject, _win(obj), &a);
    if (a && (Object *)a != obj)
        return 0;
    c = key_char(im);
    if (c == '0')
        set_zoom(cl, obj, d, 0, d->vw / 2, d->vh / 2, 1);
    else if (c == '1')
        set_zoom(cl, obj, d, 100, d->vw / 2, d->vh / 2, 1);
    else if (c == '+' || c == '=')
        set_zoom(cl, obj, d, zk_iv_step(d->shown, 1), d->vw / 2, d->vh / 2, 1);
    else if (c == '-')
        set_zoom(cl, obj, d, zk_iv_step(d->shown, -1), d->vw / 2, d->vh / 2, 1);
    else
        return 0;
    return MUI_EventHandlerRC_Eat;
}

static IPTR handle(struct IClass *cl, Object *obj, struct iv_data *d, struct IntuiMessage *im)
{
    switch (im->Class) {
    case IDCMP_MOUSEBUTTONS:
        if (im->Code == SELECTDOWN || im->Code == MIDDLEDOWN) {
            IPTR a = 0;
            if (!inside(obj, im->MouseX, im->MouseY))
                return 0;
            GetAttr(MUIA_Window_ActiveObject, _win(obj), &a);
            if (a && (Object *)a != obj)
                SetAttrs(_win(obj), MUIA_Window_ActiveObject, (IPTR)obj, TAG_DONE);
            if (im->Code == MIDDLEDOWN || d->space)
                start_pan(obj, d, im, im->Code == MIDDLEDOWN ? 2 : 1);
            else if (call_input(obj, d, ZK_IV_PRESS, im))
                d->press = 1;
            else
                start_pan(obj, d, im, 1);
            return MUI_EventHandlerRC_Eat;
        }
        if ((im->Code == SELECTUP && d->pan == 1) || (im->Code == MIDDLEUP && d->pan == 2)) {
            d->pan = 0;
            return MUI_EventHandlerRC_Eat;
        }
        if (im->Code == SELECTUP && d->press) {
            d->press = 0;
            call_input(obj, d, ZK_IV_RELEASE, im);
            return MUI_EventHandlerRC_Eat;
        }
        if (im->Code == MENUDOWN && d->press) {
            d->press = 0;
            call_input(obj, d, ZK_IV_CANCEL, im);
            return MUI_EventHandlerRC_Eat;
        }
        return 0;
    case IDCMP_MOUSEMOVE:
        if (d->pan) {
            scroll_to(cl, obj, d, d->sx0 - (im->MouseX - d->mx0), d->sy0 - (im->MouseY - d->my0));
            track(cl, obj, d, im->MouseX, im->MouseY);
            return MUI_EventHandlerRC_Eat;
        }
        track(cl, obj, d, im->MouseX, im->MouseY);
        if (d->press) {
            call_input(obj, d, ZK_IV_MOVE, im);
            return MUI_EventHandlerRC_Eat;
        }
        if (inside(obj, im->MouseX, im->MouseY) && !d->space)
            call_input(obj, d, ZK_IV_MOVE, im);
        return 0;
    case IDCMP_RAWKEY:
        return handle_key(cl, obj, d, im);
    }
    return 0;
}

/* ---- class ---- */

static void set_bg(struct iv_data *d, ULONG rgb)
{
    d->bg[0] = 255;
    d->bg[1] = (rgb >> 16) & 0xFF;
    d->bg[2] = (rgb >> 8) & 0xFF;
    d->bg[3] = rgb & 0xFF;
}

static void set_tags(struct iv_data *d, struct TagItem *tags, int *redraw)
{
    struct TagItem *t, *ts = tags;
    while ((t = NextTagItem(&ts))) {
        switch (t->ti_Tag) {
        case MUIA_ZkImageView_Background:
            set_bg(d, (ULONG)t->ti_Data);
            *redraw = 1;
            break;
        case MUIA_ZkImageView_DrawHook:
            d->draw_hook = (struct Hook *)t->ti_Data;
            *redraw = 1;
            break;
        case MUIA_ZkImageView_InputHook:
            d->input_hook = (struct Hook *)t->ti_Data;
            break;
        }
    }
}

BOOPSI_DISPATCHER(IPTR, iv_dispatcher, cl, obj, msg)
{
    struct iv_data *d;
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *o = (Object *)DoSuperMethodA(cl, obj, msg);
        struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList;
        int redraw = 0;
        if (!o)
            return 0;
        d = INST_DATA(cl, o);
        set_bg(d, 0x00202020);
        d->px = d->py = -1;
        d->shown = 100;
        d->zoom = (LONG)GetTagData(MUIA_ZkImageView_Zoom, 0, tags);
        if (d->zoom)
            d->zoom = zk_iv_clamp(d->zoom);
        set_tags(d, tags, &redraw);
        return (IPTR)o;
    }
    case OM_DISPOSE:
        d = INST_DATA(cl, obj);
        free_view(d);
        FreeVec(d->scaled);
        d->scaled = NULL;
        break;
    case OM_SET: {
        struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *z;
        int redraw = 0;
        d = INST_DATA(cl, obj);
        set_tags(d, tags, &redraw);
        z = FindTagItem(MUIA_ZkImageView_Zoom, tags);
        if (z)
            set_zoom(cl, obj, d, (LONG)z->ti_Data, d->vw / 2, d->vh / 2, 0);
        else if (redraw && d->shown_ok)
            redraw_all(obj, d);
        break;
    }
    case OM_GET: {
        struct opGet *g = (struct opGet *)msg;
        d = INST_DATA(cl, obj);
        switch (g->opg_AttrID) {
        case MUIA_ZkImageView_Zoom:
            *g->opg_Storage = (IPTR)d->zoom;
            return TRUE;
        case MUIA_ZkImageView_Shown:
            *g->opg_Storage = (IPTR)d->shown;
            return TRUE;
        case MUIA_ZkImageView_PointerX:
            *g->opg_Storage = (IPTR)(SIPTR)d->px;
            return TRUE;
        case MUIA_ZkImageView_PointerY:
            *g->opg_Storage = (IPTR)(SIPTR)d->py;
            return TRUE;
        }
        break;
    }
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        mm->MinWidth += 32;
        mm->MinHeight += 32;
        mm->DefWidth += 640;
        mm->DefHeight += 400;
        mm->MaxWidth = MUI_MAXMAX;
        mm->MaxHeight = MUI_MAXMAX;
        return r;
    }
    case MUIM_Setup:
        if (!DoSuperMethodA(cl, obj, msg))
            return FALSE;
        d = INST_DATA(cl, obj);
        d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
        d->ehn.ehn_Priority = 0;
        d->ehn.ehn_Flags = MUI_EHF_GUIMODE;
        d->ehn.ehn_Object = obj;
        d->ehn.ehn_Class = cl;
        DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&d->ehn);
        d->added = 1;
        return TRUE;
    case MUIM_Cleanup:
        d = INST_DATA(cl, obj);
        if (d->added)
            DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn);
        d->added = 0;
        d->pan = d->press = d->space = 0;
        free_view(d);
        break;
    case MUIM_Show: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        d = INST_DATA(cl, obj);
        d->shown_ok = 1;
        ensure_view(cl, obj, d);
        return r;
    }
    case MUIM_Hide:
        d = INST_DATA(cl, obj);
        d->shown_ok = 0;
        d->pan = 0;
        break;
    case MUIM_Draw: {
        struct MUIP_Draw *dm = (struct MUIP_Draw *)msg;
        IPTR r = DoSuperMethodA(cl, obj, msg);
        d = INST_DATA(cl, obj);
        if (!(dm->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)))
            return r;
        if (!ensure_view(cl, obj, d)) {
            /* No memory for the buffers: the background only. */
            SetAPen(_rp(obj), _pens(obj)[MPEN_SHADOW]);
            RectFill(_rp(obj), _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj));
            return r;
        }
        if (!d->bm_ok) {
            render(obj, d, 0, 0, d->vw, d->vh);
            d->bm_ok = 1;
            d->dirty = 0;
        } else if ((dm->flags & MADF_DRAWUPDATE) && d->dirty) {
            render(obj, d, d->dr.MinX, d->dr.MinY, d->dr.MaxX + 1, d->dr.MaxY + 1);
            d->dirty = 0;
        } else {
            BltBitMapRastPort(d->bm, 0, 0, _rp(obj), _mleft(obj), _mtop(obj), d->vw, d->vh,
                              0xC0);
        }
        return r;
    }
    case MUIM_HandleEvent: {
        struct MUIP_HandleEvent *he = (struct MUIP_HandleEvent *)msg;
        if (!he->imsg)
            return 0;
        return handle(cl, obj, INST_DATA(cl, obj), he->imsg);
    }
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

/* ---- public calls ---- */

int zk_imageview_init(void)
{
    if (!iv_class)
        iv_class = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(struct iv_data),
                                         (APTR)iv_dispatcher);
    return iv_class != NULL;
}

void zk_imageview_exit(void)
{
    if (iv_class)
        MUI_DeleteCustomClass(iv_class);
    iv_class = NULL;
}

Object *zk_imageview_new(const struct TagItem *extra)
{
    if (!iv_class)
        return NULL;
    return NewObject(iv_class->mcc_Class, NULL,
                     MUIA_FillArea, FALSE,
                     MUIA_Frame, MUIV_Frame_None,
                     extra ? TAG_MORE : TAG_DONE, (IPTR)extra);
}

void zk_imageview_set_image(Object *view, UBYTE *pixels, LONG w, LONG h)
{
    struct iv_data *d;
    if (!view || !iv_class)
        return;
    d = INST_DATA(iv_class->mcc_Class, view);
    if (!pixels || w <= 0 || h <= 0) {
        pixels = NULL;
        w = h = 0;
    }
    d->pix = pixels;
    d->pw = w;
    d->ph = h;
    FreeVec(d->scaled);
    d->scaled = NULL;
    d->scaled_zoom = 0;
    d->sx = d->sy = 0;
    if (d->vw && d->vh) {
        relayout(iv_class->mcc_Class, view, d);
        redraw_all(view, d);
    }
}

void zk_imageview_changed(Object *view, LONG x, LONG y, LONG w, LONG h)
{
    struct iv_data *d;
    LONG v0, v1, u0, u1;
    if (!view || !iv_class)
        return;
    d = INST_DATA(iv_class->mcc_Class, view);
    if (!d->pix || !d->vw || !d->vh)
        return;
    if (w <= 0 || h <= 0) {
        if (d->scaled)
            box_rows(d, 0, 0, d->sw, d->sh);
        redraw_all(view, d);
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > d->pw)
        w = d->pw - x;
    if (y + h > d->ph)
        h = d->ph - y;
    if (w <= 0 || h <= 0)
        return;
    /* The view pixels that show the rectangle, one more on each side for
     * the rounding of both scalings. */
    v0 = zk_iv_to_view(x, d->ox, d->shown) - 1;
    v1 = zk_iv_to_view(x + w, d->ox, d->shown) + 1;
    u0 = zk_iv_to_view(y, d->oy, d->shown) - 1;
    u1 = zk_iv_to_view(y + h, d->oy, d->shown) + 1;
    if (d->scaled) {
        LONG a = v0 - d->ox, b = v1 - d->ox, c = u0 - d->oy, e = u1 - d->oy;
        if (a < 0)
            a = 0;
        if (c < 0)
            c = 0;
        if (b > d->sw)
            b = d->sw;
        if (e > d->sh)
            e = d->sh;
        if (a < b && c < e)
            box_rows(d, a, c, b, e);
    }
    add_dirty(d, v0, u0, v1, u1);
    MUI_Redraw(view, MADF_DRAWUPDATE);
}

ULONG zk_imageview_pixel(Object *view, LONG x, LONG y)
{
    struct iv_data *d;
    const UBYTE *p;
    if (!view || !iv_class)
        return 0;
    d = INST_DATA(iv_class->mcc_Class, view);
    if (!d->pix || x < 0 || y < 0 || x >= d->pw || y >= d->ph)
        return 0;
    p = d->pix + ((size_t)y * d->pw + x) * 4;
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}
