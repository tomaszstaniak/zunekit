/* SPDX-License-Identifier: MIT */
/* Manual and scripted check of zk_imageview: a picture loaded through
 * datatypes (or, without PIC, a 1920 x 1080 test pattern) on a dark
 * background, with a red demonstration rectangle drawn by the DrawHook and
 * a yellow rubber band that the InputHook draws while the left button is
 * held. Space+drag or the middle button pans, Ctrl+wheel zooms, 0, 1, +
 * and - set the zoom.
 *
 *   zk-imageview-test [PIC file] [LOG file] [ZOOM n] [X n] [Y n] [W n] [H n]
 *
 * The status line and the LOG file show the pixel under the pointer, every
 * zoom shown and every rubber band. The window opens at X, Y (default
 * 40, 40), W x H inner size (default 800 x 500). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/datatypes.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <datatypes/pictureclass.h>
#include <graphics/rpattr.h>
#include <clib/alib_protos.h>
#include <aros/asmcall.h>
#include "zk_imageview.h"

static Object *status, *view;
static BPTR logfh;
static UBYTE *pix;
static LONG pw, ph;
static int band_on, band_drag;
static LONG bx0, by0, bx1, by1;

static void note(const char *fmt, ...)
{
    static char b[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    SetAttrs(status, MUIA_Text_Contents, (IPTR)b, TAG_DONE);
    if (logfh) {
        FPuts(logfh, (CONST_STRPTR)b);
        FPuts(logfh, (CONST_STRPTR)"\n");
        Flush(logfh);
    }
}

/* A picture through datatypes as A,R,G,B; 0 on failure. */
static int load(const char *name)
{
    struct BitMapHeader *bmh = NULL;
    Object *o = NewDTObject((APTR)name, DTA_GroupID, GID_PICTURE, PDTA_DestMode, PMODE_V43,
                            PDTA_Remap, FALSE, TAG_DONE);
    if (!o)
        return 0;
    GetDTAttrs(o, PDTA_BitMapHeader, (IPTR)&bmh, TAG_DONE);
    if (bmh && bmh->bmh_Width && bmh->bmh_Height) {
        pw = bmh->bmh_Width;
        ph = bmh->bmh_Height;
        pix = AllocVec((size_t)pw * ph * 4, MEMF_ANY);
        if (pix) {
            DoMethod(o, DTM_PROCLAYOUT, (IPTR)NULL, TRUE);
            DoMethod(o, PDTM_READPIXELARRAY, (IPTR)pix, PBPAFMT_ARGB, pw * 4, 0, 0, pw, ph);
        }
    }
    DisposeDTObject(o);
    return pix != NULL;
}

/* A test pattern: gradients, a grid every 64 pixels, a white border. */
static int pattern(void)
{
    LONG x, y;
    pw = 1920;
    ph = 1080;
    pix = AllocVec((size_t)pw * ph * 4, MEMF_ANY);
    if (!pix)
        return 0;
    for (y = 0; y < ph; y++)
        for (x = 0; x < pw; x++) {
            UBYTE *p = pix + ((size_t)y * pw + x) * 4;
            int grid = (x % 64 == 0) || (y % 64 == 0);
            int edge = x == 0 || y == 0 || x == pw - 1 || y == ph - 1;
            p[0] = 255;
            p[1] = edge ? 255 : grid ? 40 : (UBYTE)(x * 255 / pw);
            p[2] = edge ? 255 : grid ? 40 : (UBYTE)(y * 255 / ph);
            p[3] = edge ? 255 : grid ? 40 : (UBYTE)(255 - x * 255 / pw);
        }
    return 1;
}

static LONG to_view(LONG p, LONG origin, LONG shown)
{
    return origin + p * shown / 100;
}

static void frame(struct RastPort *rp, LONG l, LONG t, LONG r, LONG b)
{
    RectFill(rp, l, t, r, t + 1);
    RectFill(rp, l, b - 1, r, b);
    RectFill(rp, l, t, l + 1, b);
    RectFill(rp, r - 1, t, r, b);
}

AROS_UFH3S(IPTR, draw_func,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, obj, A2),
    AROS_UFHA(struct zk_imageview_draw_msg *, m, A1))
{
    AROS_USERFUNC_INIT
    struct RastPort *rp = m->rp;
    (void)hook;
    (void)obj;
    SetRPAttrs(rp, RPTAG_PenMode, FALSE, RPTAG_FgColor, 0xFFE03030, TAG_DONE);
    frame(rp, to_view(200, m->left, m->shown), to_view(150, m->top, m->shown),
          to_view(700, m->left, m->shown), to_view(450, m->top, m->shown));
    if (band_on) {
        LONG l = bx0 < bx1 ? bx0 : bx1, r = bx0 < bx1 ? bx1 : bx0;
        LONG t = by0 < by1 ? by0 : by1, b = by0 < by1 ? by1 : by0;
        SetRPAttrs(rp, RPTAG_FgColor, 0xFFF0D020, TAG_DONE);
        frame(rp, to_view(l, m->left, m->shown), to_view(t, m->top, m->shown),
              to_view(r, m->left, m->shown), to_view(b, m->top, m->shown));
    }
    SetRPAttrs(rp, RPTAG_PenMode, TRUE, TAG_DONE);
    return 0;
    AROS_USERFUNC_EXIT
}

static void band_changed(LONG ox0, LONG oy0, LONG ox1, LONG oy1)
{
    LONG l = ox0, t = oy0, r = ox1, b = oy1, v;
    LONG xs[2] = { bx0, bx1 }, ys[2] = { by0, by1 };
    int i;
    for (i = 0; i < 2; i++) {
        v = xs[i];
        if (v < l) l = v;
        if (v > r) r = v;
        v = ys[i];
        if (v < t) t = v;
        if (v > b) b = v;
    }
    if (l > r) { v = l; l = r; r = v; }
    if (t > b) { v = t; t = b; b = v; }
    /* The frame's two view pixels on each side, in picture pixels. */
    {
        IPTR shown = 100;
        LONG m;
        GetAttr(MUIA_ZkImageView_Shown, view, &shown);
        m = 300 / (LONG)(shown ? shown : 100) + 2;
        zk_imageview_changed(view, l - m, t - m, r - l + 2 * m + 1, b - t + 2 * m + 1);
    }
}

AROS_UFH3S(IPTR, input_func,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, obj, A2),
    AROS_UFHA(struct zk_imageview_input_msg *, m, A1))
{
    AROS_USERFUNC_INIT
    LONG ox0 = bx0, oy0 = by0, ox1 = bx1, oy1 = by1;
    (void)hook;
    (void)obj;
    switch (m->event) {
    case ZK_IV_PRESS:
        band_on = band_drag = 1;
        bx0 = bx1 = m->x;
        by0 = by1 = m->y;
        band_changed(ox0, oy0, ox1, oy1);
        return 1;
    case ZK_IV_MOVE:
        if (!band_drag)
            return 0;
        bx1 = m->x;
        by1 = m->y;
        band_changed(ox0, oy0, ox1, oy1);
        return 1;
    case ZK_IV_RELEASE:
        band_drag = 0;
        note("band %ld,%ld - %ld,%ld", (long)bx0, (long)by0, (long)bx1, (long)by1);
        return 1;
    case ZK_IV_CANCEL:
        band_drag = band_on = 0;
        band_changed(ox0, oy0, ox1, oy1);
        note("band cancelled");
        return 1;
    }
    return 0;
    AROS_USERFUNC_EXIT
}

AROS_UFH3S(void, pointer_func,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, obj, A2),
    AROS_UFHA(IPTR *, arg, A1))
{
    AROS_USERFUNC_INIT
    IPTR x = 0, y = 0;
    (void)hook;
    (void)arg;
    GetAttr(MUIA_ZkImageView_PointerX, obj, &x);
    GetAttr(MUIA_ZkImageView_PointerY, obj, &y);
    if ((LONG)x < 0 || (LONG)y < 0)
        note("pointer outside");
    else
        note("pointer %ld,%ld #%08lx", (long)(LONG)x, (long)(LONG)y,
             (unsigned long)zk_imageview_pixel(obj, (LONG)x, (LONG)y));
    AROS_USERFUNC_EXIT
}

AROS_UFH3S(void, shown_func,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, obj, A2),
    AROS_UFHA(IPTR *, arg, A1))
{
    AROS_USERFUNC_INIT
    IPTR z = 0;
    (void)hook;
    GetAttr(MUIA_ZkImageView_Zoom, obj, &z);
    note("shown %ld %% (zoom %ld)", (long)arg[0], (long)z);
    AROS_USERFUNC_EXIT
}

int main(int argc, char **argv)
{
    static struct Hook draw_hook, input_hook, pointer_hook, shown_hook;
    struct TagItem tags[5];
    const char *picname = NULL;
    LONG x = 40, y = 40, w = 800, h = 500, zoom = 0;
    Object *app, *win;
    ULONG sigs = 0;
    int k;

    for (k = 1; k + 1 < argc; k += 2) {
        if (!strcmp(argv[k], "LOG"))
            logfh = Open((CONST_STRPTR)argv[k + 1], MODE_NEWFILE);
        else if (!strcmp(argv[k], "PIC"))
            picname = argv[k + 1];
        else if (!strcmp(argv[k], "ZOOM"))
            zoom = atol(argv[k + 1]);
        else if (!strcmp(argv[k], "X"))
            x = atol(argv[k + 1]);
        else if (!strcmp(argv[k], "Y"))
            y = atol(argv[k + 1]);
        else if (!strcmp(argv[k], "W"))
            w = atol(argv[k + 1]);
        else if (!strcmp(argv[k], "H"))
            h = atol(argv[k + 1]);
    }
    if (picname ? !load(picname) : !pattern()) {
        PutStr((CONST_STRPTR)"could not load the picture\n");
        return 20;
    }
    if (!zk_imageview_init()) {
        PutStr((CONST_STRPTR)"could not create the class\n");
        FreeVec(pix);
        return 20;
    }
    draw_hook.h_Entry = (HOOKFUNC)(void (*)(void))AROS_ASMSYMNAME(draw_func);
    input_hook.h_Entry = (HOOKFUNC)(void (*)(void))AROS_ASMSYMNAME(input_func);
    pointer_hook.h_Entry = (HOOKFUNC)(void (*)(void))AROS_ASMSYMNAME(pointer_func);
    shown_hook.h_Entry = (HOOKFUNC)(void (*)(void))AROS_ASMSYMNAME(shown_func);
    tags[0].ti_Tag = MUIA_ZkImageView_DrawHook;
    tags[0].ti_Data = (IPTR)&draw_hook;
    tags[1].ti_Tag = MUIA_ZkImageView_InputHook;
    tags[1].ti_Data = (IPTR)&input_hook;
    tags[2].ti_Tag = MUIA_ZkImageView_Zoom;
    tags[2].ti_Data = zoom;
    tags[3].ti_Tag = MUIA_CycleChain;
    tags[3].ti_Data = 1;
    tags[4].ti_Tag = TAG_DONE;
    tags[4].ti_Data = 0;
    view = zk_imageview_new(tags);
    app = view ? ApplicationObject,
        MUIA_Application_Title, (IPTR)"ZKImageViewTest",
        SubWindow, (IPTR)(win = WindowObject,
            MUIA_Window_Title, (IPTR)"zk_imageview test",
            MUIA_Window_LeftEdge, x,
            MUIA_Window_TopEdge, y,
            MUIA_Window_Width, w,
            MUIA_Window_Height, h,
            WindowContents, (IPTR)(VGroup,
                MUIA_Group_Spacing, 0,
                Child, (IPTR)view,
                Child, (IPTR)(status = TextObject, TextFrame,
                    MUIA_Text_Contents, (IPTR)"Move the pointer over the picture.",
                End),
            End),
        End),
    End : NULL;
    if (!app) {
        PutStr((CONST_STRPTR)"could not create the window\n");
        if (view)
            MUI_DisposeObject(view);
        zk_imageview_exit();
        FreeVec(pix);
        return 20;
    }
    zk_imageview_set_image(view, pix, pw, ph);
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2,
             MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    DoMethod(view, MUIM_Notify, MUIA_ZkImageView_PointerX, MUIV_EveryTime, (IPTR)view, 3,
             MUIM_CallHook, (IPTR)&pointer_hook, MUIV_TriggerValue);
    DoMethod(view, MUIM_Notify, MUIA_ZkImageView_PointerY, MUIV_EveryTime, (IPTR)view, 3,
             MUIM_CallHook, (IPTR)&pointer_hook, MUIV_TriggerValue);
    DoMethod(view, MUIM_Notify, MUIA_ZkImageView_Shown, MUIV_EveryTime, (IPTR)view, 3,
             MUIM_CallHook, (IPTR)&shown_hook, MUIV_TriggerValue);
    SetAttrs(win, MUIA_Window_Open, TRUE, TAG_DONE);
    {
        IPTR s = 0;
        GetAttr(MUIA_ZkImageView_Shown, view, &s);
        note("open %ldx%ld shown %ld %%", (long)pw, (long)ph, (long)s);
    }
    for (;;) {
        IPTR id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);
        if (id == (IPTR)MUIV_Application_ReturnID_Quit)
            break;
        if (sigs) {
            sigs = Wait(sigs | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C)
                break;
        }
    }
    MUI_DisposeObject(app);
    zk_imageview_exit();
    FreeVec(pix);
    if (logfh)
        Close(logfh);
    return 0;
}
