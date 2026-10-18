/* SPDX-License-Identifier: MIT */
/* Manual and scripted check of zk_table: a window with a table of N rows
 * (argument, default 2000) filled in batches of 256, and a line that shows
 * what the table reported: selection, Amiga+A, drag start, drag end with the
 * drawer found or the reason none was. With LOG <file> the same lines are
 * appended to that file, for tests driven from outside. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <libraries/mui.h>
#include <mui/NList_mcc.h>
#include <clib/alib_protos.h>
#include "zk_table.h"

static Object *app, *status, *table;
static BPTR logfh;
static char c0[64], c1[16], c2[24];

static void note(const char *fmt, ...)
{
    static char b[300];
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

static void columns(void *ctx, APTR e, char **col)
{
    ULONG i = (ULONG)(IPTR)e;
    (void)ctx;
    if (!e) {
        col[0] = "Name";
        col[1] = "Size";
        col[2] = "Date";
        return;
    }
    snprintf(c0, sizeof(c0), "Caf\xe9 %s%05lu.txt", (i % 5) ? "docs/" : "", (unsigned long)i);
    snprintf(c1, sizeof(c1), "%lu", (unsigned long)(i * 37 % 100000));
    snprintf(c2, sizeof(c2), "%02lu-Oct-26 12:00", (unsigned long)(i % 28 + 1));
    col[0] = c0;
    col[1] = c1;
    col[2] = c2;
}

static void drag_started(void *ctx)
{
    (void)ctx;
    note("drag started, %lu rows selected", (unsigned long)zk_table_selected_count(table));
}

static void drag_ended(void *ctx, const char *target, const char *why)
{
    (void)ctx;
    if (target)
        note("drag ended on drawer \"%s\"", target);
    else
        note("drag ended, no drawer: %s", why);
}

static void selected_all(void *ctx)
{
    (void)ctx;
    note("Amiga+A: %lu of %lu rows selected", (unsigned long)zk_table_selected_count(table),
         (unsigned long)zk_table_count(table));
}

int main(int argc, char **argv)
{
    struct zk_table_cfg cfg = { "BAR,P=\33r BAR,", columns, NULL, drag_started, drag_ended,
                                selected_all };
    Object *win, *lv;
    ULONG rows = 2000, i, sigs = 0;
    APTR buf[256];
    int k;

    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "LOG") && k + 1 < argc)
            logfh = Open((STRPTR)argv[++k], MODE_NEWFILE);
        else
            rows = strtoul(argv[k], NULL, 10);
    }
    if (!zk_table_init()) {
        PutStr("NList.mcc is not available\n");
        return 20;
    }
    lv = zk_table_new(&cfg, &table, NULL);
    app = lv ? ApplicationObject,
        MUIA_Application_Title, (IPTR)"ZKTableTest",
        SubWindow, (IPTR)(win = WindowObject,
            MUIA_Window_Title, (IPTR)"zk_table test",
            MUIA_Window_Width, 520,
            MUIA_Window_Height, 360,
            WindowContents, (IPTR)(VGroup,
                Child, (IPTR)lv,
                Child, (IPTR)(status = TextObject, TextFrame, MUIA_Text_SetMin, FALSE, End),
            End),
        End),
    End : NULL;
    if (!app) {
        PutStr("could not create the window\n");
        zk_table_exit();
        return 20;
    }
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2,
             MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    DoMethod(table, MUIM_Notify, MUIA_NList_SelectChange, TRUE, (IPTR)app, 2,
             MUIM_Application_ReturnID, 1);
    SetAttrs(win, MUIA_Window_Open, TRUE, TAG_DONE);
    for (i = 0; i < rows; i += 256) {
        ULONG n = rows - i < 256 ? rows - i : 256, j;
        for (j = 0; j < n; j++)
            buf[j] = (APTR)(IPTR)(i + j + 1);
        zk_table_append(table, buf, n);
    }
    note("%lu rows", (unsigned long)zk_table_count(table));
    for (;;) {
        IPTR id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);
        if (id == (IPTR)MUIV_Application_ReturnID_Quit)
            break;
        if (id == 1)
            note("%lu selected", (unsigned long)zk_table_selected_count(table));
        if (sigs) {
            sigs = Wait(sigs | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C)
                break;
        }
    }
    MUI_DisposeObject(app);
    zk_table_exit();
    if (logfh)
        Close(logfh);
    return 0;
}
