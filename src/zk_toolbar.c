/* SPDX-License-Identifier: MIT */
/* zk_toolbar: see zk_toolbar.h.
 *
 * The row is a horizontal Group; each button is a small Area subclass that
 * uses Area's own input modes, so pressing, the pressed look and the
 * disabled look are Zune's: an action is MUIV_InputMode_RelVerify, a tool
 * MUIV_InputMode_Immediate (a press on the active tool does nothing), a
 * toggle MUIV_InputMode_Toggle. With MUIV_Frame_None and no background of
 * its own a button is flat on the window; while Selected, Area draws the
 * preferences' selected background (MUII_SelectedBack). MUI_Redraw ghosts
 * a disabled button after its MUIM_Draw.
 *
 * Icons: svgicon.library renders the SVG file to straight A,R,G,B; only the
 * alpha is used, as coverage, and drawn in the MUI text pen. On screens
 * deeper than 8 bits the coverage is blended with WritePixelArrayAlpha; on
 * palette screens pixels of at least half coverage are set in the pen. The
 * library is optional and opened by zk_toolbar_init(); its three calls are
 * made through the library's vectors here (offsets from svgicon.conf), so
 * that no generated headers are needed.
 *
 * Keys: the row has one raw key handler. Zune gives a key to the window's
 * active object first, so a string field keeps its keys; the handler also
 * ignores keys while the active object is a text input class, and keys
 * with Ctrl, Alt or Amiga (menu shortcuts). */
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/cybergraphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/mui.h>
#include <cybergraphx/cybergraphics.h>
#include <devices/inputevent.h>
#include <graphics/gfx.h>
#include <clib/alib_protos.h>
#include <aros/asmcall.h>
#include <aros/libcall.h>
#include "zk_toolbar.h"

/* Private: creation data of a button, and the private method. */
#define ZKA_Btn_Item   (TAG_USER | 0x5A4B0301)
#define ZKA_Btn_Size   (TAG_USER | 0x5A4B0302)
#define ZKA_Tb_Buttons (TAG_USER | 0x5A4B0303)
#define ZKA_Tb_Count   (TAG_USER | 0x5A4B0304)
#define ZKM_Tb_Event   (MUIB_ZkToolbar | 0x80)

#define SVG_MAX_BYTES (256 * 1024)

struct zkp_event {
    STACKED ULONG MethodID;
    STACKED IPTR id;
};

static struct Library *tb_svg;

#define SVG_Load(data, len, flags, handle) \
    AROS_LC4(LONG, SVGI_Load, \
             AROS_LCA(CONST_APTR, (data), A0), \
             AROS_LCA(ULONG, (len), D0), \
             AROS_LCA(ULONG, (flags), D1), \
             AROS_LCA(APTR *, (handle), A1), \
             struct Library *, tb_svg, 5, SvgIcon)
#define SVG_RenderARGB(handle, w, h, buf, bpr) \
    AROS_LC5(LONG, SVGI_RenderARGB, \
             AROS_LCA(APTR, (handle), A0), \
             AROS_LCA(ULONG, (w), D0), \
             AROS_LCA(ULONG, (h), D1), \
             AROS_LCA(APTR, (buf), A1), \
             AROS_LCA(ULONG, (bpr), D2), \
             struct Library *, tb_svg, 6, SvgIcon)
#define SVG_Free(handle) \
    AROS_LC1NR(VOID, SVGI_Free, \
               AROS_LCA(APTR, (handle), A0), \
               struct Library *, tb_svg, 7, SvgIcon)

/* ---- a button ---- */

struct tbb_data {
    Object *bar;
    ULONG id;
    UBYTE kind;
    char key;
    char label[32];
    char help[160];
    APTR svg;              /* loaded document, NULL: label */
    LONG want;             /* MUIA_ZkToolbar_IconSize, 0: font */
    LONG size;             /* size of argb */
    UBYTE *argb;           /* rendered, size * size * 4 */
    UBYTE *tint;           /* coverage in the text pen */
    int deep;              /* screen deeper than 8 bits */
};

static struct MUI_CustomClass *tbb_class, *tb_class;

static APTR load_svg(const char *path)
{
    BPTR fh;
    UBYTE *buf;
    LONG n;
    APTR doc = NULL;
    if (!tb_svg || !path || !*path)
        return NULL;
    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh)
        return NULL;
    buf = AllocVec(SVG_MAX_BYTES + 1, MEMF_ANY);
    n = buf ? Read(fh, buf, SVG_MAX_BYTES + 1) : -1;
    Close(fh);
    if (n > 0 && n <= SVG_MAX_BYTES) {
        if (SVG_Load(buf, (ULONG)n, 0, &doc) != 0)
            doc = NULL;
    }
    FreeVec(buf);
    return doc;
}

static LONG icon_size(Object *obj, struct tbb_data *d)
{
    LONG s = d->want;
    if (s <= 0) {
        s = (_font(obj)->tf_YSize * 4 + 2) / 3;
        if (s < 12)
            s = 12;
    }
    return s > 128 ? 128 : s;
}

static void free_images(struct tbb_data *d)
{
    FreeVec(d->argb);
    FreeVec(d->tint);
    d->argb = d->tint = NULL;
    d->size = 0;
}

/* Renders the icon at the size for the current font, once per size. */
static void render(Object *obj, struct tbb_data *d)
{
    LONG s = icon_size(obj, d);
    if (!d->svg || (d->argb && d->size == s))
        return;
    free_images(d);
    d->argb = AllocVec(s * s * 4, MEMF_CLEAR);
    d->tint = AllocVec(s * s * 4, MEMF_CLEAR);
    if (!d->argb || !d->tint ||
        SVG_RenderARGB(d->svg, (ULONG)s, (ULONG)s, d->argb, (ULONG)s * 4) != 0) {
        free_images(d);
        return;
    }
    d->size = s;
}

/* The coverage in the text pen of this setup, as A,R,G,B. */
static void tint(Object *obj, struct tbb_data *d)
{
    ULONG rgb[3];
    LONG i;
    struct Screen *scr = _screen(obj);
    d->deep = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH) > 8;
    if (!d->argb)
        return;
    GetRGB32(scr->ViewPort.ColorMap, _pens(obj)[MPEN_TEXT], 1, rgb);
    for (i = 0; i < d->size * d->size; i++) {
        d->tint[i * 4] = d->argb[i * 4];
        d->tint[i * 4 + 1] = rgb[0] >> 24;
        d->tint[i * 4 + 2] = rgb[1] >> 24;
        d->tint[i * 4 + 3] = rgb[2] >> 24;
    }
}

static int pad(LONG size) { return size / 4 + 2; }

static LONG label_w(Object *obj, struct tbb_data *d)
{
    struct RastPort rp;
    InitRastPort(&rp);
    SetFont(&rp, _font(obj));
    return TextLength(&rp, (STRPTR)d->label, strlen(d->label));
}

static void draw_button(Object *obj, struct tbb_data *d)
{
    struct RastPort *rp = _rp(obj);
    LONG x, y, i, j;
    if (d->argb) {
        x = _mleft(obj) + (_mwidth(obj) - d->size) / 2;
        y = _mtop(obj) + (_mheight(obj) - d->size) / 2;
        if (d->deep) {
            WritePixelArrayAlpha(d->tint, 0, 0, d->size * 4, rp, x, y, d->size, d->size,
                                 0xFFFFFFFF);
            return;
        }
        /* Palette screen: runs of pixels with at least half coverage. */
        SetAPen(rp, _pens(obj)[MPEN_TEXT]);
        for (j = 0; j < d->size; j++) {
            const UBYTE *row = d->argb + j * d->size * 4;
            for (i = 0; i < d->size; i++) {
                LONG k = i;
                while (k < d->size && row[k * 4] >= 128)
                    k++;
                if (k > i)
                    RectFill(rp, x + i, y + j, x + k - 1, y + j);
                i = k;
            }
        }
        return;
    }
    if (d->label[0]) {
        struct TextFont *f = _font(obj);
        LONG w = label_w(obj, d);
        SetFont(rp, f);
        SetAPen(rp, _pens(obj)[MPEN_TEXT]);
        SetDrMd(rp, JAM1);
        Move(rp, _mleft(obj) + (_mwidth(obj) - w) / 2,
             _mtop(obj) + (_mheight(obj) - f->tf_YSize) / 2 + f->tf_Baseline);
        Text(rp, (STRPTR)d->label, strlen(d->label));
    }
}

BOOPSI_DISPATCHER(IPTR, tbb_dispatcher, cl, obj, msg)
{
    struct tbb_data *d;
    switch (msg->MethodID) {
    case OM_NEW: {
        struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList;
        const struct zk_toolbar_item *it =
            (const struct zk_toolbar_item *)GetTagData(ZKA_Btn_Item, 0, tags);
        Object *o;
        if (!it)
            return 0;
        o = (Object *)DoSuperMethodA(cl, obj, msg);
        if (!o)
            return 0;
        d = INST_DATA(cl, o);
        d->id = it->id;
        d->kind = it->kind;
        d->key = it->key;
        d->want = (LONG)GetTagData(ZKA_Btn_Size, 0, tags);
        snprintf(d->label, sizeof(d->label), "%s", it->label ? it->label : "?");
        if (it->help) {
            snprintf(d->help, sizeof(d->help), "%s", it->help);
            SetAttrs(o, MUIA_ShortHelp, (IPTR)d->help, TAG_DONE);
        }
        d->svg = load_svg(it->svg);
        return (IPTR)o;
    }
    case OM_DISPOSE:
        d = INST_DATA(cl, obj);
        free_images(d);
        if (d->svg)
            SVG_Free(d->svg);
        d->svg = NULL;
        break;
    case MUIM_Setup:
        if (!DoSuperMethodA(cl, obj, msg))
            return FALSE;
        d = INST_DATA(cl, obj);
        render(obj, d);
        tint(obj, d);
        return TRUE;
    case MUIM_AskMinMax: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        struct MUI_MinMax *mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        LONG s, w, h;
        d = INST_DATA(cl, obj);
        s = icon_size(obj, d);
        h = s + 2 * pad(s);
        w = d->argb ? h : label_w(obj, d) + 2 * pad(s);
        if (w < h)
            w = h;
        mm->MinWidth += w;
        mm->DefWidth += w;
        mm->MaxWidth += w;
        mm->MinHeight += h;
        mm->DefHeight += h;
        mm->MaxHeight += h;
        return r;
    }
    case MUIM_Draw: {
        IPTR r = DoSuperMethodA(cl, obj, msg);
        if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))
            draw_button(obj, INST_DATA(cl, obj));
        return r;
    }
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

/* ---- the row ---- */

struct tb_data {
    Object **btn;
    ULONG n;
    ULONG tool;
    ULONG pressed;
    struct MUI_EventHandlerNode ehn;
    int added;
};

static struct tbb_data *bdata(Object *b)
{
    return INST_DATA(tbb_class->mcc_Class, b);
}

static Object *find(struct tb_data *d, ULONG id)
{
    ULONG i;
    for (i = 0; i < d->n; i++)
        if (bdata(d->btn[i])->id == id)
            return d->btn[i];
    return NULL;
}

/* Makes tool id the active one; 1 when that changed the active tool. With
 * notify the change is notified here; OM_SET leaves that to Notify. */
static int activate(struct IClass *cl, Object *obj, struct tb_data *d, ULONG id, int notify)
{
    ULONG i;
    Object *b = find(d, id);
    if (!b || bdata(b)->kind != ZK_TB_TOOL)
        return 0;
    for (i = 0; i < d->n; i++)
        if (bdata(d->btn[i])->kind == ZK_TB_TOOL)
            nnset(d->btn[i], MUIA_Selected, bdata(d->btn[i])->id == id);
    if (d->tool == id)
        return 0;
    d->tool = id;
    if (notify)
        SetSuperAttrs(cl, obj, MUIA_ZkToolbar_Tool, id, TAG_DONE);
    return 1;
}

static void pressed(struct IClass *cl, Object *obj, struct tb_data *d, ULONG id)
{
    d->pressed = id;
    SetSuperAttrs(cl, obj, MUIA_ZkToolbar_Pressed, id, TAG_DONE);
}

/* An object that takes typed text: its keys are not shortcuts. Zune gives
 * the active object the key first, and a string field eats it; this also
 * covers fields that leave some keys unused. */
static int is_text_input(Object *o)
{
    struct IClass *c;
    for (c = OCLASS(o); c; c = c->cl_Super) {
        const char *id = (const char *)c->cl_ID;
        if (id && (strstr(id, "String") || strstr(id, "Textinput") ||
                   strstr(id, "TextEditor") || strstr(id, "Texteditor")))
            return 1;
    }
    return 0;
}

static char key_char(struct IntuiMessage *im)
{
    struct InputEvent ie;
    char buf[4];
    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = im->Code;
    ie.ie_Qualifier = im->Qualifier;
    /* Zune hands over a copy whose IAddress points at a ULONG; dead keys
     * are not needed for shortcut letters. */
    ie.ie_EventAddress = NULL;
    if (MapRawKey(&ie, (STRPTR)buf, sizeof(buf), NULL) != 1)
        return 0;
    return buf[0];
}

static char fold(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static IPTR handle_key(struct IClass *cl, Object *obj, struct tb_data *d, struct IntuiMessage *im)
{
    IPTR a = 0, dis = 0, sel = 0;
    ULONG i;
    char c;
    if (im->Class != IDCMP_RAWKEY || (im->Code & IECODE_UP_PREFIX) ||
        (im->Qualifier & (IEQUALIFIER_REPEAT | IEQUALIFIER_CONTROL | IEQUALIFIER_LALT |
                          IEQUALIFIER_RALT | IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)))
        return 0;
    GetAttr(MUIA_Window_ActiveObject, _win(obj), &a);
    if (a && is_text_input((Object *)a))
        return 0;
    c = fold(key_char(im));
    if (!c)
        return 0;
    for (i = 0; i < d->n; i++) {
        Object *b = d->btn[i];
        struct tbb_data *bd = bdata(b);
        if (!bd->key || fold(bd->key) != c)
            continue;
        GetAttr(MUIA_Disabled, b, &dis);
        if (dis)
            return 0;
        switch (bd->kind) {
        case ZK_TB_TOOL:
            activate(cl, obj, d, bd->id, 1);
            break;
        case ZK_TB_ACTION:
            pressed(cl, obj, d, bd->id);
            break;
        case ZK_TB_TOGGLE:
            GetAttr(MUIA_Selected, b, &sel);
            nnset(b, MUIA_Selected, !sel);
            pressed(cl, obj, d, bd->id);
            break;
        }
        return MUI_EventHandlerRC_Eat;
    }
    return 0;
}

BOOPSI_DISPATCHER(IPTR, tb_dispatcher, cl, obj, msg)
{
    struct tb_data *d;
    switch (msg->MethodID) {
    case OM_NEW: {
        struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList;
        Object **btn = (Object **)GetTagData(ZKA_Tb_Buttons, 0, tags);
        ULONG n = (ULONG)GetTagData(ZKA_Tb_Count, 0, tags), i;
        Object *o = (Object *)DoSuperMethodA(cl, obj, msg);
        if (!o)
            return 0;
        d = INST_DATA(cl, o);
        if (n) {
            d->btn = AllocVec(n * sizeof(Object *), MEMF_ANY);
            if (!d->btn) {
                CoerceMethod(cl, o, OM_DISPOSE);
                return 0;
            }
            CopyMem(btn, d->btn, n * sizeof(Object *));
        }
        d->n = n;
        for (i = 0; i < n; i++) {
            struct tbb_data *bd = bdata(d->btn[i]);
            bd->bar = o;
            if (bd->kind == ZK_TB_TOOL && !d->tool) {
                d->tool = bd->id;
                nnset(d->btn[i], MUIA_Selected, TRUE);
            }
            /* A verified release of an action; a tool that becomes
             * selected; every change of a toggle. */
            if (bd->kind == ZK_TB_ACTION)
                DoMethod(d->btn[i], MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)o, 2, ZKM_Tb_Event,
                         bd->id);
            else if (bd->kind == ZK_TB_TOOL)
                DoMethod(d->btn[i], MUIM_Notify, MUIA_Selected, TRUE, (IPTR)o, 2, ZKM_Tb_Event,
                         bd->id);
            else
                DoMethod(d->btn[i], MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (IPTR)o, 2,
                         ZKM_Tb_Event, bd->id);
        }
        return (IPTR)o;
    }
    case OM_DISPOSE:
        d = INST_DATA(cl, obj);
        FreeVec(d->btn);
        d->btn = NULL;
        d->n = 0;
        break;
    case OM_SET: {
        struct TagItem *t = FindTagItem(MUIA_ZkToolbar_Tool, ((struct opSet *)msg)->ops_AttrList);
        d = INST_DATA(cl, obj);
        /* An unknown or unchanged tool is not passed on to Notify, so
         * notifications report real changes only. */
        if (t && !activate(cl, obj, d, (ULONG)t->ti_Data, 0))
            t->ti_Tag = TAG_IGNORE;
        break;
    }
    case OM_GET: {
        struct opGet *g = (struct opGet *)msg;
        d = INST_DATA(cl, obj);
        if (g->opg_AttrID == MUIA_ZkToolbar_Tool) {
            *g->opg_Storage = d->tool;
            return TRUE;
        }
        if (g->opg_AttrID == MUIA_ZkToolbar_Pressed) {
            *g->opg_Storage = d->pressed;
            return TRUE;
        }
        break;
    }
    case ZKM_Tb_Event: {
        ULONG id = (ULONG)((struct zkp_event *)msg)->id;
        Object *b;
        d = INST_DATA(cl, obj);
        b = find(d, id);
        if (!b)
            return 0;
        if (bdata(b)->kind == ZK_TB_TOOL)
            activate(cl, obj, d, id, 1);
        else
            pressed(cl, obj, d, id);
        return 0;
    }
    case MUIM_Setup:
        if (!DoSuperMethodA(cl, obj, msg))
            return FALSE;
        d = INST_DATA(cl, obj);
        d->ehn.ehn_Events = IDCMP_RAWKEY;
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
        break;
    case MUIM_HandleEvent: {
        struct MUIP_HandleEvent *he = (struct MUIP_HandleEvent *)msg;
        if (!he->imsg)
            return 0;
        return handle_key(cl, obj, INST_DATA(cl, obj), he->imsg);
    }
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

/* ---- public calls ---- */

int zk_toolbar_init(void)
{
    if (!tbb_class)
        tbb_class = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(struct tbb_data),
                                          (APTR)tbb_dispatcher);
    if (tbb_class && !tb_class)
        tb_class = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Group, NULL, sizeof(struct tb_data),
                                         (APTR)tb_dispatcher);
    if (tb_class && !tb_svg)
        tb_svg = OpenLibrary((CONST_STRPTR)"svgicon.library", 0);
    return tb_class != NULL;
}

void zk_toolbar_exit(void)
{
    if (tb_class)
        MUI_DeleteCustomClass(tb_class);
    if (tbb_class)
        MUI_DeleteCustomClass(tbb_class);
    tb_class = tbb_class = NULL;
    if (tb_svg)
        CloseLibrary(tb_svg);
    tb_svg = NULL;
}

static Object *make_button(const struct zk_toolbar_item *it, LONG size)
{
    LONG mode = it->kind == ZK_TB_ACTION ? MUIV_InputMode_RelVerify
              : it->kind == ZK_TB_TOOL   ? MUIV_InputMode_Immediate
                                         : MUIV_InputMode_Toggle;
    return NewObject(tbb_class->mcc_Class, NULL,
                     ZKA_Btn_Item, (IPTR)it,
                     ZKA_Btn_Size, size,
                     MUIA_Frame, MUIV_Frame_None,
                     MUIA_InputMode, mode,
                     MUIA_ShowSelState, TRUE,
                     MUIA_Weight, 0,
                     TAG_DONE);
}

Object *zk_toolbar_new(const struct zk_toolbar_item *items, ULONG count,
                       const struct TagItem *extra)
{
    struct TagItem *tags;
    Object **btn, **kids, *bar = NULL;
    ULONG i, nb = 0, nk = 0, t = 0;
    LONG size = (LONG)GetTagData(MUIA_ZkToolbar_IconSize, 0, (struct TagItem *)extra);
    if (!tb_class || !items)
        return NULL;
    tags = AllocVec((count + 8) * sizeof(struct TagItem), MEMF_CLEAR);
    btn = AllocVec((count + 1) * sizeof(Object *), MEMF_CLEAR);
    kids = AllocVec((count + 1) * sizeof(Object *), MEMF_CLEAR);
    if (!tags || !btn || !kids)
        goto done;
    for (i = 0; i < count; i++) {
        const struct zk_toolbar_item *it = &items[i];
        Object *o;
        switch (it->kind) {
        case ZK_TB_SEPARATOR:
            o = RectangleObject,
                MUIA_Rectangle_VBar, TRUE,
                MUIA_FixWidth, 9,
                MUIA_Weight, 0,
            End;
            break;
        case ZK_TB_SPACE:
            o = HSpace(0);
            break;
        case ZK_TB_ACTION:
        case ZK_TB_TOOL:
        case ZK_TB_TOGGLE:
            if (!it->id)
                goto fail;
            o = make_button(it, size);
            if (o)
                btn[nb++] = o;
            break;
        default:
            goto fail;
        }
        if (!o)
            goto fail;
        kids[nk++] = o;
    }
    tags[t].ti_Tag = MUIA_Group_Horiz;
    tags[t++].ti_Data = TRUE;
    tags[t].ti_Tag = MUIA_Group_HorizSpacing;
    tags[t++].ti_Data = 2;
    tags[t].ti_Tag = ZKA_Tb_Buttons;
    tags[t++].ti_Data = (IPTR)btn;
    tags[t].ti_Tag = ZKA_Tb_Count;
    tags[t++].ti_Data = nb;
    for (i = 0; i < nk; i++) {
        tags[t].ti_Tag = MUIA_Group_Child;
        tags[t++].ti_Data = (IPTR)kids[i];
    }
    tags[t].ti_Tag = extra ? TAG_MORE : TAG_DONE;
    tags[t].ti_Data = (IPTR)extra;
    /* Group disposes the children it was given when it fails. */
    bar = NewObjectA(tb_class->mcc_Class, NULL, tags);
    nk = 0;
    goto done;
fail:
    bar = NULL;
done:
    if (kids)
        for (i = 0; i < nk; i++)
            MUI_DisposeObject(kids[i]);
    FreeVec(tags);
    FreeVec(btn);
    FreeVec(kids);
    return bar;
}

int zk_toolbar_add(Object *toolbar, Object *obj)
{
    IPTR parent = 0;
    if (!toolbar || !obj)
        return 0;
    DoMethod(toolbar, MUIM_Group_InitChange);
    DoMethod(toolbar, OM_ADDMEMBER, (IPTR)obj);
    DoMethod(toolbar, MUIM_Group_ExitChange);
    GetAttr(MUIA_Parent, obj, &parent);
    return parent == (IPTR)toolbar;
}

void zk_toolbar_set_disabled(Object *toolbar, ULONG id, BOOL disabled)
{
    Object *b;
    if (!toolbar || !tb_class)
        return;
    b = find(INST_DATA(tb_class->mcc_Class, toolbar), id);
    if (b)
        SetAttrs(b, MUIA_Disabled, disabled ? TRUE : FALSE, TAG_DONE);
}

void zk_toolbar_set_toggle(Object *toolbar, ULONG id, BOOL on)
{
    Object *b;
    if (!toolbar || !tb_class)
        return;
    b = find(INST_DATA(tb_class->mcc_Class, toolbar), id);
    if (b && bdata(b)->kind == ZK_TB_TOGGLE)
        nnset(b, MUIA_Selected, on ? TRUE : FALSE);
}

BOOL zk_toolbar_get_toggle(Object *toolbar, ULONG id)
{
    Object *b;
    IPTR sel = 0;
    if (!toolbar || !tb_class)
        return FALSE;
    b = find(INST_DATA(tb_class->mcc_Class, toolbar), id);
    if (b && bdata(b)->kind == ZK_TB_TOGGLE)
        GetAttr(MUIA_Selected, b, &sel);
    return sel ? TRUE : FALSE;
}
