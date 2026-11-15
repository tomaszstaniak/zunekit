/* SPDX-License-Identifier: MIT */
/* zk_glarea: see zk_glarea.h.
 *
 * The context: glACreateContext() with GLA_Window and the area's rectangle
 * (GLA_Left, GLA_Top, GLA_Width, GLA_Height), made in MUIM_Show because the
 * rectangle is known only then, destroyed in MUIM_Hide. Zune hides and
 * shows an object whose size changes, so a new size gets a new context.
 *
 * The choice: the context is opened first and GL_RENDERER read; for
 * ZK_GLAREA_AUTO a software rasteriser closes it again. The texts of that
 * context stay readable afterwards (MUIA_ZkGLArea_GLInfo). */
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <proto/processor.h>
#include <libraries/mui.h>
#include <cybergraphx/cybergraphics.h>
#include <resources/processor.h>
#include <clib/alib_protos.h>
#include <aros/asmcall.h>
#include <GL/gla.h>
#include "zk_glarea.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

APTR ProcessorBase;

struct ga_data {
    LONG request, mode, software;
    char reason[96], glinfo[200];
    struct Hook *draw_hook;
    int shown, in_hook;
    GLAContext ctx;
    GLuint tex;
    LONG tw, th;                    /* size the texture was made for */
};

static struct MUI_CustomClass *ga_class;

ULONG zk_glarea_cpu_cores(void)
{
    ULONG n = 0;
    struct TagItem tags[] = { { GCIT_NumberOfProcessors, (IPTR)&n }, { TAG_DONE, 0 } };

    if (!ProcessorBase)
        ProcessorBase = OpenResource((CONST_STRPTR)"processor.resource");
    if (ProcessorBase)
        GetCPUInfo(tags);
    return n ? n : 1;
}

static int ga_software(const char *renderer)
{
    static const char *const names[] = { "softpipe", "llvmpipe", "swrast", "lavapipe", "Software", NULL };
    int i;

    for (i = 0; names[i]; i++)
        if (strstr(renderer, names[i]))
            return 1;
    return 0;
}

static void ga_close(struct ga_data *d)
{
    if (d->ctx) {
        glAMakeCurrent(d->ctx);
        if (d->tex)
            glDeleteTextures(1, &d->tex);
        glAMakeCurrent(NULL);
        glADestroyContext(d->ctx);
    }
    d->ctx = NULL;
    d->tex = 0;
    d->tw = d->th = 0;
}

/* 1 when a context is open and current; its texts are in d->glinfo. */
static int ga_open(struct ga_data *d, Object *obj)
{
    struct TagItem tags[] = {
        { GLA_Window, (IPTR)_window(obj) },
        { GLA_Left, _mleft(obj) }, { GLA_Top, _mtop(obj) },
        { GLA_Width, _mwidth(obj) }, { GLA_Height, _mheight(obj) },
        { GLA_DoubleBuf, GL_TRUE }, { GLA_RGBMode, GL_TRUE },
        { GLA_NoStencil, GL_TRUE }, { GLA_NoAccum, GL_TRUE },
        { TAG_DONE, 0 } };
    const char *vendor, *renderer, *version;

    d->ctx = glACreateContext(tags);
    if (!d->ctx)
        return 0;
    glAMakeCurrent(d->ctx);
    vendor = (const char *)glGetString(GL_VENDOR);
    renderer = (const char *)glGetString(GL_RENDERER);
    version = (const char *)glGetString(GL_VERSION);
    snprintf(d->glinfo, sizeof(d->glinfo), "%s | %s | %s", vendor ? vendor : "?",
             renderer ? renderer : "?", version ? version : "?");
    d->software = ga_software(renderer ? renderer : "");
    return 1;
}

static void ga_choose(struct ga_data *d, Object *obj)
{
    ga_close(d);
    d->mode = ZK_GLAREA_USES_CPU;
    d->glinfo[0] = 0;
    d->software = -1;
    if (d->request == ZK_GLAREA_CPU)
        strcpy(d->reason, "requested");
    else if (!ga_open(d, obj))
        strcpy(d->reason, "no OpenGL context");
    else if (d->request == ZK_GLAREA_AUTO && d->software) {
        strcpy(d->reason, "OpenGL here is a software rasteriser");
        ga_close(d);
    } else {
        d->mode = ZK_GLAREA_USES_GL;
        strcpy(d->reason, d->request == ZK_GLAREA_GPU ? "requested" : "hardware OpenGL found");
    }
    /* Notify listeners also when the value is the same as before. */
    SetAttrs(obj, MUIA_NoNotify, TRUE, MUIA_ZkGLArea_Mode, -1, TAG_DONE);
    SetAttrs(obj, MUIA_ZkGLArea_Mode, d->mode, TAG_DONE);
}

static void ga_draw(struct ga_data *d, Object *obj)
{
    struct zk_glarea_draw_msg m;

    if (!d->draw_hook)
        return;
    m.mode = d->mode;
    m.width = _mwidth(obj);
    m.height = _mheight(obj);
    m.rp = _rp(obj);
    m.left = _mleft(obj);
    m.top = _mtop(obj);
    if (d->mode == ZK_GLAREA_USES_GL) {
        glAMakeCurrent(d->ctx);
        glViewport(0, 0, m.width, m.height);
    }
    d->in_hook = 1;
    CallHookPkt(d->draw_hook, obj, &m);
    d->in_hook = 0;
    if (d->mode == ZK_GLAREA_USES_GL) {
        glFinish();
        glASwapBuffers(d->ctx);
    }
}

void zk_glarea_present(Object *area, const UBYTE *rgba, LONG w, LONG h, LONG stride)
{
    struct ga_data *d = INST_DATA(ga_class->mcc_Class, area);
    LONG aw, ah;
    float x1, y0;

    if (!d->in_hook || !rgba || w <= 0 || h <= 0)
        return;
    aw = _mwidth(area);
    ah = _mheight(area);
    if (d->mode == ZK_GLAREA_USES_CPU) {
        WritePixelArray((APTR)rgba, 0, 0, stride, _rp(area), _mleft(area), _mtop(area),
                        w < aw ? w : aw, h < ah ? h : ah, RECTFMT_RGBA);
        return;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);
    if (!d->tex) {
        glGenTextures(1, &d->tex);
        glBindTexture(GL_TEXTURE_2D, d->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else
        glBindTexture(GL_TEXTURE_2D, d->tex);
    if (d->tw != w || d->th != h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        d->tw = w;
        d->th = h;
    } else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    /* The picture's corner at the area's top left, one texel per pixel. */
    x1 = -1.0f + 2.0f * (float)w / (float)aw;
    y0 = 1.0f - 2.0f * (float)h / (float)ah;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(-1.0f, y0);
    glTexCoord2f(1, 1); glVertex2f(x1, y0);
    glTexCoord2f(1, 0); glVertex2f(x1, 1.0f);
    glTexCoord2f(0, 0); glVertex2f(-1.0f, 1.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void zk_glarea_redraw(Object *area)
{
    MUI_Redraw(area, MADF_DRAWUPDATE);
}

BOOPSI_DISPATCHER(IPTR, ga_dispatch, cl, obj, msg)
{
    struct ga_data *d;
    struct TagItem *tag, *tags;

    switch (msg->MethodID) {
    case OM_NEW:
        obj = (Object *)DoSuperNewTags(cl, obj, NULL, MUIA_FillArea, FALSE,
                                       TAG_MORE, (IPTR)((struct opSet *)msg)->ops_AttrList);
        if (obj) {
            d = INST_DATA(cl, obj);
            tags = ((struct opSet *)msg)->ops_AttrList;
            d->request = GetTagData(MUIA_ZkGLArea_Request, ZK_GLAREA_AUTO, tags);
            d->draw_hook = (struct Hook *)GetTagData(MUIA_ZkGLArea_DrawHook, 0, tags);
            d->software = -1;
            strcpy(d->reason, "not shown yet");
        }
        return (IPTR)obj;

    case OM_SET:
        d = INST_DATA(cl, obj);
        tags = ((struct opSet *)msg)->ops_AttrList;
        while ((tag = NextTagItem(&tags))) {
            if (tag->ti_Tag == MUIA_ZkGLArea_DrawHook)
                d->draw_hook = (struct Hook *)tag->ti_Data;
            else if (tag->ti_Tag == MUIA_ZkGLArea_Request) {
                d->request = tag->ti_Data;
                if (d->shown) {
                    ga_choose(d, obj);
                    MUI_Redraw(obj, MADF_DRAWOBJECT);
                }
            }
        }
        break;

    case OM_GET:
        d = INST_DATA(cl, obj);
        switch (((struct opGet *)msg)->opg_AttrID) {
        case MUIA_ZkGLArea_Request:  *((struct opGet *)msg)->opg_Storage = d->request; return TRUE;
        case MUIA_ZkGLArea_Mode:     *((struct opGet *)msg)->opg_Storage = d->mode; return TRUE;
        case MUIA_ZkGLArea_Reason:   *((struct opGet *)msg)->opg_Storage = (IPTR)d->reason; return TRUE;
        case MUIA_ZkGLArea_GLInfo:   *((struct opGet *)msg)->opg_Storage = (IPTR)d->glinfo; return TRUE;
        case MUIA_ZkGLArea_Software: *((struct opGet *)msg)->opg_Storage = (IPTR)d->software; return TRUE;
        }
        break;

    case MUIM_AskMinMax: {
        struct MUI_MinMax *mm;

        DoSuperMethodA(cl, obj, msg);
        mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
        mm->MinWidth += 32;
        mm->MinHeight += 32;
        mm->DefWidth += 640;
        mm->DefHeight += 400;
        mm->MaxWidth = MUI_MAXMAX;
        mm->MaxHeight = MUI_MAXMAX;
        return 0;
    }

    case MUIM_Show: {
        IPTR r = DoSuperMethodA(cl, obj, msg);

        d = INST_DATA(cl, obj);
        d->shown = 1;
        ga_choose(d, obj);
        return r;
    }

    case MUIM_Hide:
        d = INST_DATA(cl, obj);
        ga_close(d);
        d->shown = 0;
        break;

    case MUIM_Draw:
        DoSuperMethodA(cl, obj, msg);
        d = INST_DATA(cl, obj);
        if (d->shown && (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)))
            ga_draw(d, obj);
        return 0;
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

int zk_glarea_init(void)
{
    if (!ga_class)
        ga_class = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(struct ga_data), ga_dispatch);
    return ga_class != NULL;
}

void zk_glarea_exit(void)
{
    if (ga_class)
        MUI_DeleteCustomClass(ga_class);
    ga_class = NULL;
}

Object *zk_glarea_new(const struct TagItem *extra)
{
    if (!ga_class)
        return NULL;
    return NewObject(ga_class->mcc_Class, NULL, extra ? TAG_MORE : TAG_DONE, (IPTR)extra, TAG_DONE);
}
