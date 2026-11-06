/* SPDX-License-Identifier: MIT */
/* Manual and scripted check of zk_dragout: a window with a picture (a
 * 320 x 200 test pattern in a zk_imageview). Pressing on it and moving six
 * pixels starts a drag; the thumbnail follows the pointer until the left
 * button is let go (the right button cancels). Dropped on a Wanderer drawer
 * window, a PNG of the pattern (or, with TYPE TEXT, a short text file) is
 * written there; dropped on another program's AppWindow, it is written to
 * TEMP first and sent to that window.
 *
 *   zk-dragout-test [LOG file] [TYPE PNG|TEXT] [TEMP dir|NONE] [X n] [Y n]
 *
 * The status line and the LOG file show every result, the path written and
 * the sentence given for the user. TEMP defaults to T:; NONE refuses
 * AppWindow drops. The window opens at X, Y (default 40, 40). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include <aros/asmcall.h>
#include "zk_imageview.h"
#include "zk_dragout.h"

#define PW 320
#define PH 200

static Object *status, *win;
static BPTR logfh;
static UBYTE pix[PW * PH * 4];
static int as_text, armed;
static LONG ax, ay;
static const char *temp_dir = "T:";

static void note(const char *fmt, ...)
{
    static char b[400];
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

static void pattern(void)
{
    LONG x, y;
    for (y = 0; y < PH; y++)
        for (x = 0; x < PW; x++) {
            UBYTE *p = pix + (y * PW + x) * 4;
            int ring = ((x - PW / 2) * (x - PW / 2) + (y - PH / 2) * (y - PH / 2)) / 400 % 2;
            p[0] = 255;
            p[1] = (UBYTE)(x * 255 / PW);
            p[2] = ring ? 200 : 60;
            p[3] = (UBYTE)(y * 255 / PH);
        }
}

/* ---- a PNG without compression (stored deflate blocks) ---- */

static ULONG crc_tab[256];

static ULONG crc(ULONG c, const UBYTE *b, size_t n)
{
    size_t i;
    if (!crc_tab[1]) {
        ULONG k, v;
        int j;
        for (k = 0; k < 256; k++) {
            v = k;
            for (j = 0; j < 8; j++)
                v = (v & 1) ? 0xEDB88320UL ^ (v >> 1) : v >> 1;
            crc_tab[k] = v;
        }
    }
    for (i = 0; i < n; i++)
        c = crc_tab[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static void be32(UBYTE *p, ULONG v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static int chunk(BPTR fh, const char *type, const UBYTE *data, ULONG n)
{
    UBYTE head[8], tail[4];
    ULONG c;
    be32(head, n);
    memcpy(head + 4, type, 4);
    c = crc(0xFFFFFFFFUL, head + 4, 4);
    c = crc(c, data, n) ^ 0xFFFFFFFFUL;
    be32(tail, c);
    return Write(fh, head, 8) == 8 && (!n || Write(fh, (APTR)data, n) == (LONG)n) &&
           Write(fh, tail, 4) == 4;
}

static int write_png(const char *path, char *why, size_t whyn)
{
    static const UBYTE sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    ULONG raw = (PW * 4 + 1) * PH, blocks = (raw + 65534) / 65535;
    ULONG zn = 2 + raw + 5 * blocks + 4, i, a = 1, b = 0, pos = 0, done = 0;
    UBYTE ihdr[13] = { 0 }, *z = AllocVec(zn, MEMF_ANY), *r;
    BPTR fh;
    int ok;
    if (!z) {
        snprintf(why, whyn, "no memory for the PNG");
        return 0;
    }
    z[pos++] = 0x78;
    z[pos++] = 0x01;
    while (done < raw) {
        ULONG n = raw - done > 65535 ? 65535 : raw - done;
        z[pos++] = done + n == raw ? 1 : 0;
        z[pos++] = n & 0xFF;
        z[pos++] = n >> 8;
        z[pos++] = ~n & 0xFF;
        z[pos++] = (~n >> 8) & 0xFF;
        for (i = 0; i < n; i++) {
            ULONG o = done + i, row = o / (PW * 4 + 1), col = o % (PW * 4 + 1);
            UBYTE v;
            if (col == 0)
                v = 0;                        /* filter: none */
            else {
                r = pix + (row * PW + (col - 1) / 4) * 4;
                v = r[((col - 1) % 4 + 1) % 4];  /* A,R,G,B -> R,G,B,A */
            }
            z[pos++] = v;
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
        done += n;
    }
    be32(z + pos, (b << 16) | a);
    pos += 4;
    be32(ihdr, PW);
    be32(ihdr + 4, PH);
    ihdr[8] = 8;
    ihdr[9] = 6;
    fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (!fh) {
        Fault(IoErr(), NULL, (STRPTR)why, whyn);
        FreeVec(z);
        return 0;
    }
    ok = Write(fh, (APTR)sig, 8) == 8 && chunk(fh, "IHDR", ihdr, 13) &&
         chunk(fh, "IDAT", z, pos) && chunk(fh, "IEND", NULL, 0);
    if (!ok)
        Fault(IoErr(), NULL, (STRPTR)why, whyn);
    Close(fh);
    FreeVec(z);
    if (!ok)
        DeleteFile((CONST_STRPTR)path);
    return ok;
}

static int write_text(const char *path, char *why, size_t whyn)
{
    static const char text[] = "Written by zk-dragout-test.\n";
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    int ok;
    if (!fh) {
        Fault(IoErr(), NULL, (STRPTR)why, whyn);
        return 0;
    }
    ok = Write(fh, (APTR)text, sizeof(text) - 1) == (LONG)sizeof(text) - 1;
    if (!ok)
        Fault(IoErr(), NULL, (STRPTR)why, whyn);
    Close(fh);
    if (!ok)
        DeleteFile((CONST_STRPTR)path);
    return ok;
}

static int write_file(void *ctx, const char *path, char *why, size_t whyn)
{
    (void)ctx;
    if (logfh) {
        FPuts(logfh, (CONST_STRPTR)"writing ");
        FPuts(logfh, (CONST_STRPTR)path);
        FPuts(logfh, (CONST_STRPTR)"\n");
        Flush(logfh);
    }
    return as_text ? write_text(path, why, whyn) : write_png(path, why, whyn);
}

static const char *result_name(enum zk_dragout_result r)
{
    switch (r) {
    case ZK_DRAGOUT_DRAWER:    return "DRAWER";
    case ZK_DRAGOUT_APPWINDOW: return "APPWINDOW";
    case ZK_DRAGOUT_DESKTOP:   return "DESKTOP";
    case ZK_DRAGOUT_NOWHERE:   return "NOWHERE";
    case ZK_DRAGOUT_CANCELLED: return "CANCELLED";
    case ZK_DRAGOUT_FAILED:    return "FAILED";
    }
    return "?";
}

static void drag(void)
{
    struct zk_dragout_cfg cfg;
    IPTR w = 0;
    char path[512], why[300];
    enum zk_dragout_result r;
    GetAttr(MUIA_Window_Window, win, &w);
    if (!w)
        return;
    memset(&cfg, 0, sizeof(cfg));
    cfg.thumb = pix;
    cfg.thumb_w = PW;
    cfg.thumb_h = PH;
    cfg.file_name = as_text ? "dragout-test.txt" : "dragout-test.png";
    cfg.write_file = write_file;
    cfg.temp_dir = temp_dir;
    note("drag started");
    r = zk_dragout_run((struct Window *)w, &cfg, path, sizeof(path), why, sizeof(why));
    note("result %s path \"%s\" why \"%s\"", result_name(r), path, why);
}

AROS_UFH3S(IPTR, input_func,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, obj, A2),
    AROS_UFHA(struct zk_imageview_input_msg *, m, A1))
{
    AROS_USERFUNC_INIT
    (void)hook;
    (void)obj;
    switch (m->event) {
    case ZK_IV_PRESS:
        armed = 1;
        ax = m->x;
        ay = m->y;
        return 1;
    case ZK_IV_MOVE:
        if (!armed)
            return 0;
        if (abs(m->x - ax) >= 6 || abs(m->y - ay) >= 6) {
            /* The release that ends the drag arrives afterwards; it finds
             * the press disarmed. */
            armed = 0;
            drag();
        }
        return 1;
    case ZK_IV_RELEASE:
    case ZK_IV_CANCEL:
        armed = 0;
        return 1;
    }
    return 0;
    AROS_USERFUNC_EXIT
}

int main(int argc, char **argv)
{
    static struct Hook input_hook;
    struct TagItem tags[3];
    LONG x = 40, y = 40;
    Object *app, *view;
    ULONG sigs = 0;
    int k;

    for (k = 1; k + 1 < argc; k += 2) {
        if (!strcmp(argv[k], "LOG"))
            logfh = Open((CONST_STRPTR)argv[k + 1], MODE_NEWFILE);
        else if (!strcmp(argv[k], "TYPE"))
            as_text = !strcmp(argv[k + 1], "TEXT");
        else if (!strcmp(argv[k], "TEMP"))
            temp_dir = strcmp(argv[k + 1], "NONE") ? argv[k + 1] : NULL;
        else if (!strcmp(argv[k], "X"))
            x = atol(argv[k + 1]);
        else if (!strcmp(argv[k], "Y"))
            y = atol(argv[k + 1]);
    }
    pattern();
    if (!zk_imageview_init()) {
        PutStr((CONST_STRPTR)"could not create the class\n");
        return 20;
    }
    input_hook.h_Entry = (HOOKFUNC)(void (*)(void))AROS_ASMSYMNAME(input_func);
    tags[0].ti_Tag = MUIA_ZkImageView_InputHook;
    tags[0].ti_Data = (IPTR)&input_hook;
    tags[1].ti_Tag = MUIA_ZkImageView_Zoom;
    tags[1].ti_Data = 100;
    tags[2].ti_Tag = TAG_DONE;
    tags[2].ti_Data = 0;
    view = zk_imageview_new(tags);
    app = view ? ApplicationObject,
        MUIA_Application_Title, (IPTR)"ZKDragoutTest",
        SubWindow, (IPTR)(win = WindowObject,
            MUIA_Window_Title, (IPTR)"zk_dragout test",
            MUIA_Window_LeftEdge, x,
            MUIA_Window_TopEdge, y,
            WindowContents, (IPTR)(VGroup,
                Child, (IPTR)(TextObject,
                    MUIA_Text_Contents, (IPTR)"Drag the picture into a drawer window.",
                End),
                Child, (IPTR)view,
                Child, (IPTR)(status = TextObject, TextFrame,
                    MUIA_Text_Contents, (IPTR)"",
                End),
            End),
        End),
    End : NULL;
    if (!app) {
        PutStr((CONST_STRPTR)"could not create the window\n");
        if (view)
            MUI_DisposeObject(view);
        zk_imageview_exit();
        return 20;
    }
    zk_imageview_set_image(view, pix, PW, PH);
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2,
             MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    SetAttrs(win, MUIA_Window_Open, TRUE, TAG_DONE);
    note("open, %s files, temp %s", as_text ? "text" : "PNG", temp_dir ? temp_dir : "none");
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
    if (logfh)
        Close(logfh);
    return 0;
}
