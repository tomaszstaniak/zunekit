/* SPDX-License-Identifier: MIT */
/* zk_table: see zk_table.h. A subclass of NList that adds row drag-out and
 * Amiga+A; the rest is NList as shipped.
 *
 * Why NList and not Zune's List: Zune List repaints its whole row area,
 * background first, on every MUIM_List_Insert (list.c, UPDATEMODE_ALL), which
 * shows as the list going empty for each batch of appended rows; NList
 * appends without that (measured on ABIv11 2026.09 and AROS One 1.3, 20,000
 * rows in batches of 256: 6-15 empty frames with List, none with NList). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <mui/NList_mcc.h>
#include <mui/NListview_mcc.h>
#include <devices/inputevent.h>
#include <clib/alib_protos.h>
#include <aros/asmcall.h>
#include "zk_table.h"

#define RAWKEY_A   0x20
#define DRAG_START 6               /* pixels moved before a press is a drag */

struct zk_data {
    struct zk_table_cfg cfg;
    struct Hook display;
    struct MUI_EventHandlerNode ehn;
    int added;
    int armed, dragging;
    WORD x, y;
    LONG entry;
};

static struct MUI_CustomClass *zk_class;

/* ---- display ---- */

AROS_UFH3S(void, zk_display,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, obj, A2),
    AROS_UFHA(struct NList_DisplayMessage *, m, A1))
{
    AROS_USERFUNC_INIT
    struct zk_data *d = hook->h_Data;
    (void)obj;
    d->cfg.columns(d->cfg.ctx, m->entry, m->strings);
    AROS_USERFUNC_EXIT
}

/* ---- the drawer under the pointer ---- */

/* The window under the pointer belongs to another program and can close at
 * any moment, so nothing of it is used after the locks are let go: its
 * owner's name and its title are copied while they are held, and the drawer
 * is checked on disk afterwards. intuition frees a closing window while it
 * holds the screen's LayerInfo lock (closewindow.c, LOCK_REFRESH), and the
 * window's port goes after that, so holding the lock from WhichLayer to the
 * last read keeps both. The port and the title are read under Forbid as
 * well: Zune takes a window's shared port away under Forbid before closing
 * it, and SetWindowTitles takes no lock at all. Wanderer titles a drawer
 * window with the drawer's path (iconwindow.c); workbench.library has no
 * call that says which drawer is open where (WBCTRLA_GetOpenDrawerList is not
 * implemented on AROS). */
int zk_wanderer_drawer_at(struct Window *from, WORD mx, WORD my, char *out, size_t n,
                          char *why, size_t whyn)
{
    struct Screen *scr = from->WScreen;
    struct Layer *layer;
    char owner[64] = "", title[512] = "";
    int found = 0, have_title = 0, ok = 0;
    BPTR l;

    LockLayerInfo(&scr->LayerInfo);
    layer = WhichLayer(&scr->LayerInfo, from->LeftEdge + mx, from->TopEdge + my);
    Forbid();
    if (layer && layer->Window) {
        struct Window *w = layer->Window;
        struct MsgPort *port = w->UserPort;
        struct Task *t = port ? port->mp_SigTask : NULL;
        if (t && t->tc_Node.ln_Name) {
            snprintf(owner, sizeof(owner), "%s", t->tc_Node.ln_Name);
            found = 1;
        }
        if (w->Title) {
            snprintf(title, sizeof(title), "%s", (char *)w->Title);
            have_title = 1;
        }
    }
    Permit();
    UnlockLayerInfo(&scr->LayerInfo);

    if (!found) {
        snprintf(why, whyn, "not dropped on a window");
        return 0;
    }
    if (!strstr(owner, "Wanderer")) {
        snprintf(why, whyn, "dropped on a window of \"%s\"", owner);
        return 0;
    }
    if (!have_title || !strchr(title, ':')) {
        snprintf(why, whyn, "dropped on Wanderer, but not on a drawer");
        return 0;
    }
    snprintf(out, n, "%s", title);
    l = Lock((STRPTR)out, SHARED_LOCK);
    if (l) {
        struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
        ok = fib && Examine(l, fib) && fib->fib_DirEntryType > 0;
        FreeDosObject(DOS_FIB, fib);
        UnLock(l);
    }
    if (!ok)
        snprintf(why, whyn, "\"%s\" is not a drawer", out);
    return ok;
}

/* ---- events ---- */

/* NList treats a move with the button held as range selection, so a drag of
 * rows is told apart here, ahead of NList's own handler: a press on a row
 * that is already selected arms it; moving DRAG_START pixels starts the
 * drag (the selection stays); a release outside the window looks for the
 * drawer there. A press without a move acts as a plain click on that row.
 * Everything else goes to NList as before. */

static void set_events(Object *obj, struct zk_data *d, ULONG events)
{
    if (d->added)
        DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn);
    d->ehn.ehn_Events = events;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&d->ehn);
    d->added = 1;
}

static Object *active_object(Object *win)
{
    IPTR o = 0;
    GetAttr(MUIA_Window_ActiveObject, win, &o);
    return (Object *)o;
}

static IPTR handle(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *msg)
{
    struct zk_data *d = INST_DATA(cl, obj);
    struct IntuiMessage *im = msg->imsg;
    if (!im)
        return 0;
    if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN) {
        struct MUI_NList_TestPos_Result pos;
        LONG state = 0;
        if (im->MouseX < _mleft(obj) || im->MouseX > _mright(obj) || im->MouseY < _mtop(obj) ||
            im->MouseY > _mbottom(obj))
            return 0;
        /* A click in the table moves the keyboard to it. Zune keeps a
         * string field as the window's active object after the click, and
         * Amiga+A would then still be taken for that field's. */
        {
            Object *a = active_object(_win(obj));
            if (a != obj && a != _parent(obj))
                SetAttrs(_win(obj), MUIA_Window_ActiveObject, (IPTR)_parent(obj), TAG_DONE);
        }
        memset(&pos, 0, sizeof(pos));
        pos.entry = -1;
        DoMethod(obj, MUIM_NList_TestPos, im->MouseX, im->MouseY, (IPTR)&pos);
        if (pos.entry < 0)
            return 0;
        DoMethod(obj, MUIM_NList_Select, pos.entry, MUIV_NList_Select_Ask, (IPTR)&state);
        if (!state || (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ||
            !d->cfg.drag_ended)
            return 0;                       /* NList's own click */
        d->armed = 1;
        d->dragging = 0;
        d->x = im->MouseX;
        d->y = im->MouseY;
        d->entry = pos.entry;
        set_events(obj, d, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY);
        return MUI_EventHandlerRC_Eat;
    }
    /* Amiga+A selects every row. Only when no other object has the keyboard:
     * in a string field the key belongs to the text. */
    if (im->Class == IDCMP_RAWKEY && im->Code == RAWKEY_A &&
        (im->Qualifier & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))) {
        Object *a = active_object(_win(obj));
        if (a && a != obj && a != _parent(obj))
            return 0;
        DoMethod(obj, MUIM_NList_Select, MUIV_NList_Select_All, MUIV_NList_Select_On, NULL);
        if (d->cfg.selected_all)
            d->cfg.selected_all(d->cfg.ctx);
        return MUI_EventHandlerRC_Eat;
    }
    if (!d->armed)
        return 0;
    if (im->Class == IDCMP_MOUSEMOVE) {
        if (!d->dragging && (abs(im->MouseX - d->x) >= DRAG_START ||
                             abs(im->MouseY - d->y) >= DRAG_START)) {
            d->dragging = 1;
            if (d->cfg.drag_started)
                d->cfg.drag_started(d->cfg.ctx);
        }
        return MUI_EventHandlerRC_Eat;
    }
    if (im->Class == IDCMP_MOUSEBUTTONS && (im->Code == SELECTUP || im->Code == MENUDOWN)) {
        struct Window *w = im->IDCMPWindow;
        int outside = im->MouseX < 0 || im->MouseY < 0 || im->MouseX >= w->Width ||
                      im->MouseY >= w->Height;
        char target[512], why[160];
        int found = 0;
        d->armed = 0;
        set_events(obj, d, IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY);
        if (!d->dragging) {
            /* A plain click on a selected row: that row alone. */
            DoMethod(obj, MUIM_NList_Select, MUIV_NList_Select_All, MUIV_NList_Select_Off, NULL);
            DoMethod(obj, MUIM_NList_Select, d->entry, MUIV_NList_Select_On, NULL);
            SetAttrs(obj, MUIA_NList_Active, d->entry, TAG_DONE);
            return MUI_EventHandlerRC_Eat;
        }
        d->dragging = 0;
        snprintf(why, sizeof(why), "released inside the window");
        if (im->Code == MENUDOWN)
            snprintf(why, sizeof(why), "the drag was cancelled");
        else if (outside)
            found = zk_wanderer_drawer_at(w, im->MouseX, im->MouseY, target, sizeof(target),
                                          why, sizeof(why));
        d->cfg.drag_ended(d->cfg.ctx, found ? target : NULL, why);
        return MUI_EventHandlerRC_Eat;
    }
    return 0;
}

BOOPSI_DISPATCHER(IPTR, zk_dispatcher, cl, obj, msg)
{
    struct zk_data *d;
    switch (msg->MethodID) {
    case MUIM_Setup: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        if (!r)
            return 0;
        d = INST_DATA(cl, obj);
        d->added = 0;
        d->armed = d->dragging = 0;
        /* Ahead of NList's own handler (priority 0). */
        d->ehn.ehn_Priority = 1;
        d->ehn.ehn_Flags = 0;
        d->ehn.ehn_Object = obj;
        d->ehn.ehn_Class = cl;
        set_events(obj, d, IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY);
        return r;
    }
    case MUIM_Cleanup:
        d = INST_DATA(cl, obj);
        if (d->added)
            DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn);
        d->added = 0;
        break;
    case MUIM_HandleEvent: {
        IPTR r = handle(cl, obj, (struct MUIP_HandleEvent *)msg);
        if (r)
            return r;
        break;
    }
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

/* ---- public calls ---- */

int zk_table_init(void)
{
    if (!zk_class) {
        /* The first open of NList.mcc starts its "clipboard server", a process
         * that lives until the class is expunged and, created without
         * NP_CurrentDir/NP_HomeDir, keeps copies of our current and home
         * directory locks: the program's drawer then stays "in use" after the
         * program has quit (AROS One 1.3). Give it neither while the class is
         * opened. The library is then found through the system's paths only,
         * not in PROGDIR:. */
        struct Process *me = (struct Process *)FindTask(NULL);
        BPTR home = me->pr_HomeDir, cd = CurrentDir(BNULL);
        me->pr_HomeDir = BNULL;
        zk_class = MUI_CreateCustomClass(NULL, MUIC_NList, NULL, sizeof(struct zk_data),
                                         zk_dispatcher);
        me->pr_HomeDir = home;
        CurrentDir(cd);
    }
    return zk_class != NULL;
}

void zk_table_exit(void)
{
    if (zk_class)
        MUI_DeleteCustomClass(zk_class);
    zk_class = NULL;
}

Object *zk_table_new(const struct zk_table_cfg *cfg, Object **table,
                     const struct TagItem *extra)
{
    struct zk_data *d;
    Object *t, *lv;
    *table = NULL;
    if (!zk_class || !cfg || !cfg->columns)
        return NULL;
    t = NewObject(zk_class->mcc_Class, NULL,
                  InputListFrame,
                  MUIA_NList_Format, (IPTR)(cfg->format ? cfg->format : ""),
                  MUIA_NList_Title, TRUE,
                  MUIA_NList_MultiSelect, MUIV_NList_MultiSelect_Default,
                  TAG_MORE, (IPTR)extra);
    if (!t)
        return NULL;
    d = INST_DATA(zk_class->mcc_Class, t);
    d->cfg = *cfg;
    d->display.h_Entry = (HOOKFUNC)AROS_ASMSYMNAME(zk_display);
    d->display.h_Data = d;
    SetAttrs(t, MUIA_NList_DisplayHook2, (IPTR)&d->display, TAG_DONE);
    lv = NListviewObject,
        MUIA_CycleChain, 1,
        MUIA_NListview_NList, (IPTR)t,
    End;
    if (!lv) {
        MUI_DisposeObject(t);
        return NULL;
    }
    *table = t;
    return lv;
}

void zk_table_append(Object *table, APTR *rows, ULONG n)
{
    if (!n)
        return;
    SetAttrs(table, MUIA_NList_Quiet, TRUE, TAG_DONE);
    DoMethod(table, MUIM_NList_Insert, (IPTR)rows, n, MUIV_NList_Insert_Bottom, 0);
    SetAttrs(table, MUIA_NList_Quiet, FALSE, TAG_DONE);
}

void zk_table_clear(Object *table)
{
    DoMethod(table, MUIM_NList_Clear);
}

ULONG zk_table_count(Object *table)
{
    IPTR n = 0;
    GetAttr(MUIA_NList_Entries, table, &n);
    return (ULONG)n;
}

APTR zk_table_row(Object *table, LONG pos)
{
    APTR e = NULL;
    DoMethod(table, MUIM_NList_GetEntry, pos, (IPTR)&e);
    return e;
}

void zk_table_select(Object *table, LONG pos, int on)
{
    DoMethod(table, MUIM_NList_Select, pos < 0 ? MUIV_NList_Select_All : pos,
             on ? MUIV_NList_Select_On : MUIV_NList_Select_Off, NULL);
}

int zk_table_is_selected(Object *table, LONG pos)
{
    LONG state = 0;
    DoMethod(table, MUIM_NList_Select, pos, MUIV_NList_Select_Ask, (IPTR)&state);
    return state != 0;
}

LONG zk_table_next_selected(Object *table, LONG pos)
{
    LONG p = pos < 0 ? MUIV_NList_NextSelected_Start : pos;
    DoMethod(table, MUIM_NList_NextSelected, (IPTR)&p);
    return p == MUIV_NList_NextSelected_End ? -1 : p;
}

ULONG zk_table_selected_count(Object *table)
{
    ULONG n = 0;
    LONG p = -1;
    while ((p = zk_table_next_selected(table, p)) >= 0)
        n++;
    return n;
}

LONG zk_table_active(Object *table)
{
    IPTR a = (IPTR)MUIV_NList_Active_Off;
    GetAttr(MUIA_NList_Active, table, &a);
    return (LONG)a;
}

void zk_table_set_active(Object *table, LONG pos)
{
    SetAttrs(table, MUIA_NList_Active, pos < 0 ? MUIV_NList_Active_Off : pos, TAG_DONE);
}

void zk_table_quiet(Object *table, int on)
{
    SetAttrs(table, MUIA_NList_Quiet, on ? TRUE : FALSE, TAG_DONE);
}
