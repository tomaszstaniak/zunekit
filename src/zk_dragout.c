/* SPDX-License-Identifier: MIT */
/* zk_dragout: see zk_dragout.h. The name rules are in zk_dragout_core.c.
 *
 * The drag runs on the calling task: every tick (Delay(1)) it reads the
 * pointer from the screen and the buttons from input.device's qualifier
 * (PeekQualifier), and moves a borderless thumbnail window there. No
 * IDCMP is used, so it works over any program's windows. At the release the
 * thumbnail is closed first, so that WhichLayer finds the window under it.
 *
 * Wanderer has no call that says which drawer a window shows, so a drawer
 * window is recognised by its owner (the task's name contains "Wanderer")
 * and its title, which is the drawer's path (iconwindow.c); its desktop is
 * its backdrop window. Every other program's window is offered the file
 * through workbench.library's SendAppWindowMessage, which delivers it only
 * when the window is a registered AppWindow and says so. */
#include <stdio.h>
#include <string.h>
#include <exec/devices.h>
#include <exec/io.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <graphics/layers.h>
#include <intuition/screens.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/cybergraphics.h>
#include <proto/workbench.h>
/* input.device's base is the opened device; it is private to this file. */
#define InputBase zk_dragout_input
static struct Device *zk_dragout_input;
#include <proto/input.h>
#include "zk_dragout.h"
#include "zk_dragout_core.h"

#define THUMB_MAX 160

/* What was under a screen point. */
struct probe {
    int window, own, backdrop, has_owner, has_title;
    char owner[64];
    char title[512];
    struct Window *win;
    WORD left, top;
};

/* The window under the pointer belongs to another program and can close at
 * any moment, so nothing of it is used after the locks are let go: its
 * owner's name, title, flags and position are copied while they are held.
 * intuition frees a closing window while it holds the screen's LayerInfo
 * lock (closewindow.c, LOCK_REFRESH), and the window's port goes after
 * that, so holding the lock from WhichLayer to the last read keeps both.
 * The port and the title are read under Forbid as well: Zune takes a
 * window's shared port away under Forbid before closing it, and
 * SetWindowTitles takes no lock at all. The window pointer itself is kept
 * only to be compared (SendAppWindowMessage looks it up in its list). */
static void probe_at(struct Window *from, WORD x, WORD y, struct probe *p)
{
    struct Screen *scr = from->WScreen;
    struct Layer *layer;
    struct Task *me = FindTask(NULL);
    memset(p, 0, sizeof(*p));
    LockLayerInfo(&scr->LayerInfo);
    layer = WhichLayer(&scr->LayerInfo, x, y);
    Forbid();
    if (layer && layer->Window) {
        struct Window *w = layer->Window;
        struct MsgPort *port = w->UserPort;
        struct Task *t = port ? port->mp_SigTask : NULL;
        p->window = 1;
        p->win = w;
        p->left = w->LeftEdge;
        p->top = w->TopEdge;
        p->backdrop = (w->Flags & WFLG_BACKDROP) != 0;
        p->own = w == from || t == me;
        if (t && t->tc_Node.ln_Name) {
            snprintf(p->owner, sizeof(p->owner), "%s", t->tc_Node.ln_Name);
            p->has_owner = 1;
        }
        if (w->Title) {
            snprintf(p->title, sizeof(p->title), "%s", (char *)w->Title);
            p->has_title = 1;
        }
    }
    Permit();
    UnlockLayerInfo(&scr->LayerInfo);
}

static int is_dir(const char *path)
{
    BPTR l = Lock((CONST_STRPTR)path, SHARED_LOCK);
    int ok = 0;
    if (l) {
        struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
        ok = fib && Examine(l, fib) && fib->fib_DirEntryType > 0;
        FreeDosObject(DOS_FIB, fib);
        UnLock(l);
    }
    return ok;
}

static int exists(void *ctx, const char *path)
{
    BPTR l = Lock((CONST_STRPTR)path, SHARED_LOCK);
    (void)ctx;
    if (!l)
        return IoErr() != ERROR_OBJECT_NOT_FOUND;
    UnLock(l);
    return 1;
}

/* The drawer of a probed Wanderer window: 1 and the path, or 0 and why. */
static int drawer_of(const struct probe *p, char *out, size_t n, char *why, size_t whyn)
{
    if (!p->window || !p->has_owner) {
        snprintf(why, whyn, "not dropped on a window");
        return 0;
    }
    if (!zk_do_is_wanderer(p->owner)) {
        snprintf(why, whyn, "dropped on a window of \"%s\"", p->owner);
        return 0;
    }
    if (!p->has_title || !zk_do_is_drawer_title(p->title)) {
        snprintf(why, whyn, "dropped on Wanderer, but not on a drawer");
        return 0;
    }
    snprintf(out, n, "%s", p->title);
    if (!is_dir(out)) {
        snprintf(why, whyn, "\"%s\" is not a drawer", out);
        return 0;
    }
    return 1;
}

int zk_dragout_drawer_at(struct Window *from, WORD x, WORD y, char *out, size_t n,
                         char *why, size_t whyn)
{
    struct probe p;
    probe_at(from, x, y, &p);
    return drawer_of(&p, out, n, why, whyn);
}

/* ---- the thumbnail ---- */

struct thumb {
    struct Window *win;
    long w, h;                  /* window size */
};

/* The thumbnail's top-left corner for the pointer at x, y: centred on the
 * pointer, kept on the screen. */
static WORD place(WORD x, long size, WORD screen)
{
    long p = x - size / 2;
    if (p > screen - size)
        p = screen - size;
    return (WORD)(p < 0 ? 0 : p);
}

static void open_thumb(struct Screen *scr, const struct zk_dragout_cfg *cfg, WORD x, WORD y,
                       struct thumb *t)
{
    long tw = 64, th = 48, i, j;
    UBYTE *buf = NULL;
    struct DrawInfo *dri;
    struct RastPort *rp;
    WORD left, top;
    memset(t, 0, sizeof(*t));
    if (cfg->thumb && cfg->thumb_w > 0 && cfg->thumb_h > 0) {
        zk_do_fit_thumb(cfg->thumb_w, cfg->thumb_h, THUMB_MAX, &tw, &th);
        buf = AllocVec(tw * th * 4, MEMF_ANY);
        if (buf)
            for (j = 0; j < th; j++) {
                const UBYTE *row = cfg->thumb + (size_t)(j * cfg->thumb_h / th) * cfg->thumb_w * 4;
                for (i = 0; i < tw; i++)
                    memcpy(buf + (j * tw + i) * 4, row + (size_t)(i * cfg->thumb_w / tw) * 4, 4);
            }
    }
    t->w = tw + 2;
    t->h = th + 2;
    left = place(x, t->w, scr->Width);
    top = place(y, t->h, scr->Height);
    t->win = OpenWindowTags(NULL,
                            WA_CustomScreen, (IPTR)scr,
                            WA_Left, left,
                            WA_Top, top,
                            WA_Width, t->w,
                            WA_Height, t->h,
                            WA_Borderless, TRUE,
                            WA_Activate, FALSE,
                            WA_RMBTrap, TRUE,
                            WA_SmartRefresh, TRUE,
                            WA_NoCareRefresh, TRUE,
                            TAG_DONE);
    if (t->win) {
        rp = t->win->RPort;
        dri = GetScreenDrawInfo(scr);
        SetAPen(rp, dri ? dri->dri_Pens[SHINEPEN] : 2);
        RectFill(rp, 1, 1, t->w - 2, t->h - 2);
        if (buf)
            WritePixelArray(buf, 0, 0, tw * 4, rp, 1, 1, tw, th, RECTFMT_ARGB);
        SetAPen(rp, dri ? dri->dri_Pens[SHADOWPEN] : 1);
        Move(rp, 0, 0);
        Draw(rp, t->w - 1, 0);
        Draw(rp, t->w - 1, t->h - 1);
        Draw(rp, 0, t->h - 1);
        Draw(rp, 0, 0);
        if (dri)
            FreeScreenDrawInfo(scr, dri);
    }
    FreeVec(buf);
}

/* ---- writing ---- */

static enum zk_dragout_result write_into(const char *dir, const struct zk_dragout_cfg *cfg,
                                         char *path, size_t pathn, char *why, size_t whyn)
{
    char reason[200] = "";
    if (!zk_do_unique(dir, cfg->file_name, exists, NULL, path, pathn)) {
        path[0] = 0;
        snprintf(why, whyn, "There is no free name for \"%s\" in %s.", cfg->file_name, dir);
        return ZK_DRAGOUT_FAILED;
    }
    if (!cfg->write_file(cfg->ctx, path, reason, sizeof(reason))) {
        snprintf(why, whyn, "Could not write %s: %s", path, reason[0] ? reason : "no reason given.");
        path[0] = 0;
        return ZK_DRAGOUT_FAILED;
    }
    return ZK_DRAGOUT_DRAWER;
}

static enum zk_dragout_result to_appwindow(const struct probe *p, WORD x, WORD y,
                                           const struct zk_dragout_cfg *cfg, char *path,
                                           size_t pathn, char *why, size_t whyn)
{
    enum zk_dragout_result r;
    char *files[1];
    ULONG secs = 0, micros = 0;
    if (!cfg->temp_dir) {
        snprintf(why, whyn, "Dropped on a window of \"%s\"; this program does not send files "
                 "to other programs.", p->owner);
        return ZK_DRAGOUT_NOWHERE;
    }
    r = write_into(cfg->temp_dir, cfg, path, pathn, why, whyn);
    if (r != ZK_DRAGOUT_DRAWER)
        return r;
    files[0] = path;
    CurrentTime(&secs, &micros);
    if (!SendAppWindowMessage(p->win, 1, files, 0, x - p->left, y - p->top, secs, micros)) {
        DeleteFile((CONST_STRPTR)path);
        path[0] = 0;
        snprintf(why, whyn, "The window of \"%s\" does not take dropped files.", p->owner);
        return ZK_DRAGOUT_NOWHERE;
    }
    snprintf(why, whyn, "Sent %s to \"%s\".", path, p->owner);
    return ZK_DRAGOUT_APPWINDOW;
}

static enum zk_dragout_result resolve(struct Window *from, WORD x, WORD y,
                                      const struct zk_dragout_cfg *cfg, char *path, size_t pathn,
                                      char *why, size_t whyn)
{
    struct probe p;
    char dir[512], reason[160];
    enum zk_dragout_result r;
    probe_at(from, x, y, &p);
    if (!p.window) {
        snprintf(why, whyn, "Not dropped on a window.");
        return ZK_DRAGOUT_NOWHERE;
    }
    if (p.own) {
        snprintf(why, whyn, "Dropped on this program's own window.");
        return ZK_DRAGOUT_NOWHERE;
    }
    if (p.has_owner && zk_do_is_wanderer(p.owner)) {
        if (p.backdrop) {
            snprintf(why, whyn, "Dropped on the Wanderer desktop; nothing was saved.");
            return ZK_DRAGOUT_DESKTOP;
        }
        if (!drawer_of(&p, dir, sizeof(dir), reason, sizeof(reason))) {
            snprintf(why, whyn, "Not saved: %s.", reason);
            return ZK_DRAGOUT_NOWHERE;
        }
        r = write_into(dir, cfg, path, pathn, why, whyn);
        if (r == ZK_DRAGOUT_DRAWER)
            snprintf(why, whyn, "Saved as %s.", path);
        return r;
    }
    if (!p.has_owner)
        snprintf(p.owner, sizeof(p.owner), "%.60s", p.has_title ? p.title : "another program");
    return to_appwindow(&p, x, y, cfg, path, pathn, why, whyn);
}

enum zk_dragout_result zk_dragout_run(struct Window *from, const struct zk_dragout_cfg *cfg,
                                      char *path, size_t pathn, char *why, size_t whyn)
{
    struct Screen *scr;
    struct MsgPort *port = NULL;
    struct IORequest *io = NULL;
    struct thumb t;
    WORD x, y, lx, ly;
    int cancelled = 0;

    if (pathn)
        path[0] = 0;
    if (!from || !cfg || !cfg->file_name || !*cfg->file_name || !cfg->write_file) {
        snprintf(why, whyn, "Nothing to drag.");
        return ZK_DRAGOUT_FAILED;
    }
    scr = from->WScreen;
    port = CreateMsgPort();
    io = port ? CreateIORequest(port, sizeof(struct IOStdReq)) : NULL;
    if (!io || OpenDevice((CONST_STRPTR)"input.device", 0, io, 0) != 0) {
        if (io)
            DeleteIORequest(io);
        if (port)
            DeleteMsgPort(port);
        snprintf(why, whyn, "The drag could not start: input.device did not open.");
        return ZK_DRAGOUT_NOWHERE;
    }
    zk_dragout_input = io->io_Device;

    x = lx = scr->MouseX;
    y = ly = scr->MouseY;
    open_thumb(scr, cfg, x, y, &t);
    for (;;) {
        UWORD q = PeekQualifier();
        x = scr->MouseX;
        y = scr->MouseY;
        if (q & IEQUALIFIER_RBUTTON) {
            cancelled = 1;
            break;
        }
        if (!(q & IEQUALIFIER_LEFTBUTTON))
            break;
        if (t.win && (x != lx || y != ly)) {
            ChangeWindowBox(t.win, place(x, t.w, scr->Width), place(y, t.h, scr->Height), t.w,
                            t.h);
            lx = x;
            ly = y;
        }
        Delay(1);
    }
    if (t.win)
        CloseWindow(t.win);
    zk_dragout_input = NULL;
    CloseDevice(io);
    DeleteIORequest(io);
    DeleteMsgPort(port);

    if (cancelled) {
        snprintf(why, whyn, "The drag was cancelled.");
        return ZK_DRAGOUT_CANCELLED;
    }
    return resolve(from, x, y, cfg, path, pathn, why, whyn);
}
