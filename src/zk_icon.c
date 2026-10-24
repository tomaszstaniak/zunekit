/* SPDX-License-Identifier: MIT */
/* zk_icon: see zk_icon.h.
 *
 * Why icon.library and not Dtpic on the icon's PNG: an AROS PNG icon is a PNG
 * file, but Dtpic drew alpha correctly only from August 2026 (dtpic.c,
 * 82330213), after AROS One 1.3. DrawIconStateA composites the icon's ARGB
 * image itself (drawiconstate.c, WritePixelArrayAlpha) once LayoutIconA has
 * prepared it for the screen, on both systems. */
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <workbench/icon.h>
#include <clib/alib_protos.h>
#include <aros/asmcall.h>
#include "zk_icon.h"

#define ZKA_Icon_Object (TAG_USER | 0x5A4B0101)   /* private: the DiskObject */

struct zi_data {
    struct DiskObject *dob;
    int laid;
};

static struct MUI_CustomClass *zi_class;

static int icon_w(struct zi_data *d) { return d->dob ? d->dob->do_Gadget.Width : 0; }
static int icon_h(struct zi_data *d) { return d->dob ? d->dob->do_Gadget.Height : 0; }

BOOPSI_DISPATCHER(IPTR, zi_dispatcher, cl, obj, msg)
{
    struct zi_data *d;
    switch (msg->MethodID) {
    case OM_NEW: {
        Object *o = (Object *)DoSuperMethodA(cl, obj, msg);
        if (o) {
            d = INST_DATA(cl, o);
            d->dob = (struct DiskObject *)GetTagData(ZKA_Icon_Object, 0,
                                                     ((struct opSet *)msg)->ops_AttrList);
        }
        return (IPTR)o;
    }
    case OM_DISPOSE:
        d = INST_DATA(cl, obj);
        if (d->dob)
            FreeDiskObject(d->dob);
        break;
    case MUIM_Setup:
        if (!DoSuperMethodA(cl, obj, msg))
            return FALSE;
        d = INST_DATA(cl, obj);
        if (d->dob)
            d->laid = LayoutIconA(d->dob, _screen(obj), NULL);
        return TRUE;
    case MUIM_Cleanup:
        d = INST_DATA(cl, obj);
        if (d->laid) {
            LayoutIconA(d->dob, NULL, NULL);
            d->laid = 0;
        }
        break;
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        d = INST_DATA(cl, obj);
        mm->MinWidth += icon_w(d);
        mm->DefWidth += icon_w(d);
        mm->MaxWidth += icon_w(d);
        mm->MinHeight += icon_h(d);
        mm->DefHeight += icon_h(d);
        mm->MaxHeight += icon_h(d);
        return r;
    }
    case MUIM_Draw: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        d = INST_DATA(cl, obj);
        if (d->dob && (((struct MUIP_Draw *)msg)->flags & MADF_DRAWOBJECT)) {
            struct TagItem t[] = {
                { ICONDRAWA_Borderless, TRUE },
                { ICONDRAWA_Frameless, TRUE },
                { ICONDRAWA_EraseBackground, FALSE },
                { ICONDRAWA_DrawInfo, (IPTR)_dri(obj) },
                { TAG_DONE, 0 }
            };
            DrawIconStateA(_rp(obj), d->dob, NULL, _mleft(obj), _mtop(obj), IDS_NORMAL, t);
        }
        return r;
    }
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

int zk_icon_init(void)
{
    if (!zi_class)
        zi_class = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct zi_data),
                                         (APTR)zi_dispatcher);
    return zi_class != NULL;
}

void zk_icon_exit(void)
{
    if (zi_class) {
        MUI_DeleteCustomClass(zi_class);
        zi_class = NULL;
    }
}

Object *zk_icon_new(const char *name, const struct TagItem *extra)
{
    struct DiskObject *dob;
    Object *o;
    if (!zi_class)
        return NULL;
    dob = name ? GetDiskObject((STRPTR)name) : NULL;
    o = NewObject(zi_class->mcc_Class, NULL,
                  ZKA_Icon_Object, (IPTR)dob,
                  MUIA_FillArea, TRUE,
                  extra ? TAG_MORE : TAG_DONE, (IPTR)extra);
    if (!o && dob)
        FreeDiskObject(dob);
    return o;
}
