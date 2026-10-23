/* SPDX-License-Identifier: MIT */
/* Manual and scripted check of zk_steps: a window with a slider whose number
 * of steps is chosen with a cycle gadget (2, 3 or 4 named steps, as an
 * archiver's compression levels), a disabled slider, and a line that shows
 * the value the slider reported and the step's name. With LOG <file> the
 * same lines, and a line for every press on a slider (pointer x, ticks, step
 * chosen), are appended to that file, for tests driven from outside.
 *
 * HIDE or DISABLE: the first time the slider reports step 0 (reached by a
 * drag), it is hidden or disabled at once, while the button may still be
 * held; "Restore" shows or enables it again. A drag that survived that would
 * move the knob on pointer motion alone, with no button held. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <libraries/mui.h>
#include <clib/alib_protos.h>
#include "zk_steps.h"

enum { ID_VALUE = 1, ID_COUNT, ID_RESTORE };

static Object *app, *status, *steps, *count_cy;
static int transition;             /* 0 none, 1 hide, 2 disable */
static int armed = 1;
static BPTR logfh;

static const char *names[3][4] = {
    { "Store only", "Normal" },
    { "Fast", "Normal", "Best" },
    { "Store only", "Fast", "Normal", "Best" },
};
static const char *count_labels[] = { "2 steps", "3 steps", "4 steps", NULL };

static void note(const char *fmt, ...)
{
    static char b[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    SetAttrs(status, MUIA_Text_Contents, (IPTR)b, TAG_DONE);
    if (logfh) {
        FPuts(logfh, b);
        FPuts(logfh, "\n");
        Flush(logfh);
    }
}

static void trace(const char *line)
{
    if (logfh) {
        FPuts(logfh, line);
        FPuts(logfh, "\n");
        Flush(logfh);
    }
}

static void show_value(void)
{
    IPTR v = 0, mx = 0, c = 0;
    get(steps, MUIA_Numeric_Value, &v);
    get(steps, MUIA_Numeric_Max, &mx);
    get(count_cy, MUIA_Cycle_Active, &c);
    note("value %lu of 0-%lu: %s", (unsigned long)v, (unsigned long)mx,
         v <= mx && c < 3 ? names[c][v] : "?");
}

int main(int argc, char **argv)
{
    struct TagItem chain[] = { { MUIA_CycleChain, 1 }, { TAG_DONE, 0 } };
    struct TagItem off[] = { { MUIA_Disabled, TRUE }, { TAG_DONE, 0 } };
    Object *win, *dis, *restore;
    ULONG sigs = 0;
    int k;

    for (k = 1; k < argc; k++)
        if (!strcmp(argv[k], "LOG") && k + 1 < argc)
            logfh = Open((STRPTR)argv[++k], MODE_NEWFILE);
        else if (!strcmp(argv[k], "HIDE"))
            transition = 1;
        else if (!strcmp(argv[k], "DISABLE"))
            transition = 2;
    zk_steps_set_trace(trace);
    if (!zk_steps_init()) {
        PutStr("could not create the class\n");
        return 20;
    }
    steps = zk_steps_new(4, 2, chain);
    dis = zk_steps_new(3, 1, off);
    app = steps && dis ? ApplicationObject,
        MUIA_Application_Title, (IPTR)"ZKStepsTest",
        SubWindow, (IPTR)(win = WindowObject,
            MUIA_Window_Title, (IPTR)"zk_steps test",
            WindowContents, (IPTR)(VGroup,
                Child, (IPTR)(count_cy = CycleObject,
                    MUIA_Cycle_Entries, (IPTR)count_labels,
                    MUIA_Cycle_Active, 2,
                    MUIA_CycleChain, 1,
                End),
                Child, (IPTR)(HGroup,
                    Child, (IPTR)Label1("Faster"),
                    Child, (IPTR)steps,
                    Child, (IPTR)Label1("Smaller"),
                End),
                Child, (IPTR)(HGroup,
                    Child, (IPTR)Label1("Disabled"),
                    Child, (IPTR)dis,
                End),
                Child, (IPTR)(restore = SimpleButton("_Restore")),
                Child, (IPTR)(status = TextObject, TextFrame, MUIA_Text_SetMin, FALSE, End),
            End),
        End),
    End : NULL;
    if (!app) {
        PutStr("could not create the window\n");
        zk_steps_exit();
        return 20;
    }
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2,
             MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    DoMethod(steps, MUIM_Notify, MUIA_Numeric_Value, MUIV_EveryTime, (IPTR)app, 2,
             MUIM_Application_ReturnID, ID_VALUE);
    DoMethod(count_cy, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime, (IPTR)app, 2,
             MUIM_Application_ReturnID, ID_COUNT);
    DoMethod(restore, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2,
             MUIM_Application_ReturnID, ID_RESTORE);
    SetAttrs(win, MUIA_Window_Open, TRUE, TAG_DONE);
    show_value();
    for (;;) {
        IPTR id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);
        if (id == (IPTR)MUIV_Application_ReturnID_Quit)
            break;
        if (id == ID_VALUE) {
            IPTR v = 0;
            show_value();
            get(steps, MUIA_Numeric_Value, &v);
            if (transition && armed && v == 0) {
                armed = 0;
                SetAttrs(steps, transition == 1 ? MUIA_ShowMe : MUIA_Disabled,
                         transition == 1 ? FALSE : TRUE, TAG_DONE);
                note(transition == 1 ? "hidden" : "disabled");
            }
        }
        if (id == ID_RESTORE) {
            SetAttrs(steps, MUIA_ShowMe, TRUE, MUIA_Disabled, FALSE, TAG_DONE);
            note("restored");
        }
        if (id == ID_COUNT) {
            IPTR c = 0;
            get(count_cy, MUIA_Cycle_Active, &c);
            /* As a program does when its format changes: new range, the
             * value set without a notification, then shown once. */
            SetAttrs(steps, MUIA_NoNotify, TRUE, MUIA_Numeric_Max, c + 1, TAG_DONE);
            show_value();
        }
        if (sigs) {
            sigs = Wait(sigs | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C)
                break;
        }
    }
    MUI_DisposeObject(app);
    zk_steps_exit();
    if (logfh)
        Close(logfh);
    return 0;
}
