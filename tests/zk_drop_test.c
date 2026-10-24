/* SPDX-License-Identifier: MIT */
/* Manual and scripted check of zk_icon and zk_drop: a window with an icon
 * (ICON <name>, default the test's own "PROGDIR:zk-drop-test") next to a
 * title, and a drop area. Names dropped on the window are shown in the drop
 * area's second line; "Busy" swaps both lines, as a program does while it
 * cannot take a drop. With LOG <file> every drop's names are appended to
 * that file, for tests driven from outside. */
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/workbench.h>
#include <libraries/mui.h>
#include <workbench/startup.h>
#include <clib/alib_protos.h>
#include <aros/asmcall.h>
#include "zk_icon.h"
#include "zk_drop.h"

enum { ID_BUSY = 1 };

static Object *drop;
static BPTR logfh;

AROS_UFH3S(void, drop_func,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, obj, A2),
    AROS_UFHA(struct AppMessage **, pmsg, A1))
{
    AROS_USERFUNC_INIT
    struct AppMessage *am = *pmsg;
    char line[200] = "", name[256];
    LONG i;
    (void)hook;
    (void)obj;
    for (i = 0; i < am->am_NumArgs; i++) {
        struct WBArg *a = &am->am_ArgList[i];
        if (!NameFromLock(a->wa_Lock, name, sizeof(name)))
            name[0] = 0;
        if (a->wa_Name && *a->wa_Name)
            AddPart(name, a->wa_Name, sizeof(name));
        if (logfh) {
            FPuts(logfh, "drop ");
            FPuts(logfh, name);
            FPuts(logfh, "\n");
            Flush(logfh);
        }
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s", i ? ", " : "",
                 FilePart(name));
    }
    zk_drop_set(drop, "Dropped", line);
    AROS_USERFUNC_EXIT
}

int main(int argc, char **argv)
{
    static struct Hook drop_hook;
    struct TagItem back[] = { { MUIA_Background, MUII_TextBack }, { TAG_DONE, 0 } };
    const char *iconname = "PROGDIR:zk-drop-test";
    Object *app, *win, *icon, *busy;
    ULONG sigs = 0;
    int k, busy_on = 0;

    for (k = 1; k < argc; k++)
        if (!strcmp(argv[k], "LOG") && k + 1 < argc)
            logfh = Open((STRPTR)argv[++k], MODE_NEWFILE);
        else if (!strcmp(argv[k], "ICON") && k + 1 < argc)
            iconname = argv[++k];
    if (!zk_icon_init() || !zk_drop_init()) {
        PutStr("could not create the classes\n");
        return 20;
    }
    drop_hook.h_Entry = (HOOKFUNC)AROS_ASMSYMNAME(drop_func);
    icon = zk_icon_new(iconname, NULL);
    drop = zk_drop_new("Drop files here", "Their names are shown in this line.", back);
    app = icon && drop ? ApplicationObject,
        MUIA_Application_Title, (IPTR)"ZKDropTest",
        SubWindow, (IPTR)(win = WindowObject,
            MUIA_Window_Title, (IPTR)"zk_drop test",
            MUIA_Window_AppWindow, TRUE,
            WindowContents, (IPTR)(VGroup,
                Child, (IPTR)(HGroup,
                    Child, (IPTR)icon,
                    Child, (IPTR)(TextObject,
                        MUIA_Font, MUIV_Font_Big,
                        MUIA_Text_Contents, (IPTR)"\33bzk_drop test",
                    End),
                    Child, (IPTR)HSpace(0),
                End),
                Child, (IPTR)drop,
                Child, (IPTR)(busy = SimpleButton("_Busy")),
            End),
        End),
    End : NULL;
    if (!app) {
        PutStr("could not create the window\n");
        zk_drop_exit();
        zk_icon_exit();
        return 20;
    }
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2,
             MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    DoMethod(win, MUIM_Notify, MUIA_AppMessage, MUIV_EveryTime, (IPTR)win, 3,
             MUIM_CallHook, (IPTR)&drop_hook, MUIV_TriggerValue);
    DoMethod(busy, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2,
             MUIM_Application_ReturnID, ID_BUSY);
    SetAttrs(win, MUIA_Window_Open, TRUE, TAG_DONE);
    for (;;) {
        IPTR id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);
        if (id == (IPTR)MUIV_Application_ReturnID_Quit)
            break;
        if (id == ID_BUSY) {
            busy_on = !busy_on;
            if (busy_on)
                zk_drop_set(drop, "Busy", "A drop now would have to wait.");
            else
                zk_drop_set(drop, "Drop files here", "Their names are shown in this line.");
        }
        if (sigs) {
            sigs = Wait(sigs | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C)
                break;
        }
    }
    MUI_DisposeObject(app);
    zk_drop_exit();
    zk_icon_exit();
    if (logfh)
        Close(logfh);
    return 0;
}
