/* SPDX-License-Identifier: MIT */
/* Manual and scripted check of zk_toolbar: a row like a capture tool's
 * editor (Copy and Save, a separator, eight tools, a toggle, the flexible
 * space, then a colour swatch, its value, an image size and a zoom), a
 * text field to check that typing there does not trigger the keys, and
 * buttons that disable Save and select the Rectangle tool from the program.
 *
 *   zk-toolbar-test [LOG file] [ICONS dir] [SIZE n] [X n] [Y n] [W n]
 *
 * ICONS is the drawer with the Tabler SVG files (default PROGDIR:icons);
 * a missing file or svgicon.library shows the label. Every Tool and Pressed
 * notification is shown in the status line and appended to the LOG file.
 * The window opens at X, Y (default 40, 40), W pixels wide (default 900). */
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
#include "zk_toolbar.h"

enum {
    T_COPY = 1, T_SAVE, T_SELECT, T_RECT, T_CIRCLE, T_ARROW, T_LINE, T_TEXT, T_BLUR,
    T_HIGHLIGHT, T_RULER,
    ID_DISABLE = 100, ID_RECT
};

static struct {
    UBYTE kind;
    char key;
    ULONG id;
    const char *file, *label, *help;
} spec[] = {
    { ZK_TB_ACTION, 0, T_COPY, "copy.svg", "Copy", "Copy to the clipboard" },
    { ZK_TB_ACTION, 's', T_SAVE, "device-floppy.svg", "Save", "Save (S)" },
    { ZK_TB_SEPARATOR, 0, 0, NULL, NULL, NULL },
    { ZK_TB_TOOL, 'v', T_SELECT, "pointer.svg", "Sel", "Select (V)" },
    { ZK_TB_TOOL, 'r', T_RECT, "square.svg", "Rect", "Rectangle (R)" },
    { ZK_TB_TOOL, 'o', T_CIRCLE, "circle.svg", "Oval", "Oval (O)" },
    { ZK_TB_TOOL, 'a', T_ARROW, "arrow-up-right.svg", "Arrow", "Arrow (A)" },
    { ZK_TB_TOOL, 'l', T_LINE, "line.svg", "Line", "Line (L)" },
    { ZK_TB_TOOL, 't', T_TEXT, "typography.svg", "Text", "Text (T)" },
    { ZK_TB_TOOL, 'b', T_BLUR, "blur.svg", "Blur", "Blur (B)" },
    { ZK_TB_TOOL, 'h', T_HIGHLIGHT, "highlight.svg", "Mark", "Highlight (H)" },
    { ZK_TB_SEPARATOR, 0, 0, NULL, NULL, NULL },
    { ZK_TB_TOGGLE, 'm', T_RULER, "ruler-measure.svg", "Ruler", "Ruler (M)" },
    { ZK_TB_SPACE, 0, 0, NULL, NULL, NULL },
};
#define NSPEC (sizeof(spec) / sizeof(spec[0]))

static Object *status, *bar;
static BPTR logfh;

static const char *name_of(ULONG id)
{
    ULONG i;
    for (i = 0; i < NSPEC; i++)
        if (spec[i].id == id)
            return spec[i].label;
    return "?";
}

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

AROS_UFH3S(void, tool_func,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, obj, A2),
    AROS_UFHA(IPTR *, arg, A1))
{
    AROS_USERFUNC_INIT
    (void)hook;
    (void)obj;
    note("Tool %lu %s", (unsigned long)arg[0], name_of((ULONG)arg[0]));
    AROS_USERFUNC_EXIT
}

AROS_UFH3S(void, pressed_func,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, obj, A2),
    AROS_UFHA(IPTR *, arg, A1))
{
    AROS_USERFUNC_INIT
    ULONG id = (ULONG)arg[0];
    (void)hook;
    (void)obj;
    if (id == T_RULER)
        note("Pressed %lu %s %s", (unsigned long)id, name_of(id),
             zk_toolbar_get_toggle(bar, id) ? "on" : "off");
    else
        note("Pressed %lu %s", (unsigned long)id, name_of(id));
    AROS_USERFUNC_EXIT
}

int main(int argc, char **argv)
{
    static struct Hook tool_hook, pressed_hook;
    static struct zk_toolbar_item items[NSPEC];
    static char paths[NSPEC][256];
    static ULONG swatch[3] = { 0x3A3A3A3A, 0x7B7B7B7B, 0xD5D5D5D5 };
    const char *icons = "PROGDIR:icons";
    LONG x = 40, y = 40, w = 900, size = 0;
    struct TagItem more[2];
    Object *app, *win, *field, *disable, *rect, *colour, *value, *dims, *zoom;
    ULONG sigs = 0, i;
    int k, failed;

    for (k = 1; k + 1 < argc; k += 2) {
        if (!strcmp(argv[k], "LOG"))
            logfh = Open((CONST_STRPTR)argv[k + 1], MODE_NEWFILE);
        else if (!strcmp(argv[k], "ICONS"))
            icons = argv[k + 1];
        else if (!strcmp(argv[k], "SIZE"))
            size = atol(argv[k + 1]);
        else if (!strcmp(argv[k], "X"))
            x = atol(argv[k + 1]);
        else if (!strcmp(argv[k], "Y"))
            y = atol(argv[k + 1]);
        else if (!strcmp(argv[k], "W"))
            w = atol(argv[k + 1]);
    }
    if (!zk_toolbar_init()) {
        PutStr((CONST_STRPTR)"could not create the class\n");
        return 20;
    }
    for (i = 0; i < NSPEC; i++) {
        items[i].kind = spec[i].kind;
        items[i].key = spec[i].key;
        items[i].id = spec[i].id;
        items[i].label = spec[i].label;
        items[i].help = spec[i].help;
        items[i].svg = NULL;
        if (spec[i].file) {
            snprintf(paths[i], sizeof(paths[i]), "%s", icons);
            AddPart((STRPTR)paths[i], (CONST_STRPTR)spec[i].file, sizeof(paths[i]));
            items[i].svg = paths[i];
        }
    }
    more[0].ti_Tag = MUIA_ZkToolbar_IconSize;
    more[0].ti_Data = size;
    more[1].ti_Tag = TAG_DONE;
    more[1].ti_Data = 0;
    bar = zk_toolbar_new(items, NSPEC, more);
    colour = ColorfieldObject,
        MUIA_Colorfield_RGB, (IPTR)swatch,
        MUIA_FixWidth, 16,
        MUIA_FixHeight, 16,
        MUIA_Frame, MUIV_Frame_None,
    End;
    value = TextObject, MUIA_Text_Contents, (IPTR)"#3A7BD5 / Tab to copy", End;
    dims = TextObject, MUIA_Text_Contents, (IPTR)"1920 \xd7 1080", End;
    zoom = TextObject, MUIA_Text_Contents, (IPTR)"100 %", End;
    failed = !bar || !zk_toolbar_add(bar, colour) || !zk_toolbar_add(bar, value) ||
             !zk_toolbar_add(bar, dims) || !zk_toolbar_add(bar, zoom);
    app = !failed ? ApplicationObject,
        MUIA_Application_Title, (IPTR)"ZKToolbarTest",
        SubWindow, (IPTR)(win = WindowObject,
            MUIA_Window_Title, (IPTR)"zk_toolbar test",
            MUIA_Window_LeftEdge, x,
            MUIA_Window_TopEdge, y,
            MUIA_Window_Width, w,
            WindowContents, (IPTR)(VGroup,
                Child, (IPTR)bar,
                Child, (IPTR)(RectangleObject, MUIA_Rectangle_HBar, TRUE, MUIA_FixHeight, 4, End),
                Child, (IPTR)(HGroup,
                    Child, (IPTR)(TextObject, MUIA_Text_Contents, (IPTR)"Typing here:", End),
                    Child, (IPTR)(field = StringObject, StringFrame, MUIA_CycleChain, 1, End),
                End),
                Child, (IPTR)(HGroup,
                    Child, (IPTR)(disable = MUI_MakeObject(MUIO_Checkmark, (IPTR)"Disable Save")),
                    Child, (IPTR)(TextObject, MUIA_Text_Contents, (IPTR)"Disable Save", End),
                    Child, (IPTR)(rect = SimpleButton("Select _Rectangle")),
                    Child, (IPTR)HSpace(0),
                End),
                Child, (IPTR)(status = TextObject, TextFrame,
                    MUIA_Text_Contents, (IPTR)"Click a button or press its key.",
                End),
            End),
        End),
    End : NULL;
    if (!app) {
        PutStr((CONST_STRPTR)"could not create the window\n");
        if (bar)
            MUI_DisposeObject(bar);
        zk_toolbar_exit();
        return 20;
    }
    (void)field;
    tool_hook.h_Entry = (HOOKFUNC)(void (*)(void))AROS_ASMSYMNAME(tool_func);
    pressed_hook.h_Entry = (HOOKFUNC)(void (*)(void))AROS_ASMSYMNAME(pressed_func);
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2,
             MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    DoMethod(bar, MUIM_Notify, MUIA_ZkToolbar_Tool, MUIV_EveryTime, (IPTR)bar, 3,
             MUIM_CallHook, (IPTR)&tool_hook, MUIV_TriggerValue);
    DoMethod(bar, MUIM_Notify, MUIA_ZkToolbar_Pressed, MUIV_EveryTime, (IPTR)bar, 3,
             MUIM_CallHook, (IPTR)&pressed_hook, MUIV_TriggerValue);
    DoMethod(disable, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, (IPTR)app, 2,
             MUIM_Application_ReturnID, ID_DISABLE);
    DoMethod(rect, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2,
             MUIM_Application_ReturnID, ID_RECT);
    SetAttrs(win, MUIA_Window_Open, TRUE, TAG_DONE);
    {
        struct Library *svg = OpenLibrary((CONST_STRPTR)"svgicon.library", 0);
        note("open, svgicon.library %s", svg ? "present" : "missing: labels");
        if (svg)
            CloseLibrary(svg);
    }
    for (;;) {
        IPTR id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);
        if (id == (IPTR)MUIV_Application_ReturnID_Quit)
            break;
        if (id == ID_DISABLE) {
            IPTR on = 0;
            GetAttr(MUIA_Selected, disable, &on);
            zk_toolbar_set_disabled(bar, T_SAVE, on ? TRUE : FALSE);
            note("Save %s", on ? "disabled" : "enabled");
        }
        if (id == ID_RECT)
            SetAttrs(bar, MUIA_ZkToolbar_Tool, T_RECT, TAG_DONE);
        if (sigs) {
            sigs = Wait(sigs | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C)
                break;
        }
    }
    MUI_DisposeObject(app);
    zk_toolbar_exit();
    if (logfh)
        Close(logfh);
    return 0;
}
