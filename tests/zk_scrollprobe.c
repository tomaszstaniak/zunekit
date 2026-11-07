/* SPDX-License-Identifier: MIT */
/* The scroller of a long Linktext document, as a Zune Scrollbar shows it:
 *   zk-scrollprobe [PARAGRAPHS] [LOG file]
 * A document of PARAGRAPHS paragraphs (4600 by default), an empty one in
 * every four so that lines differ in height; the status line and the log
 * say what the scroller holds (Prop_Entries, Prop_Visible, Prop_First)
 * and what the view shows (First, LineCount) at every change of the
 * scroller and on Query. Top, Mid, End move the view; Short gives a
 * document of 20 paragraphs, Long the long one again. For the 2026-11-07
 * check of a knob that moved the view one line on AROS One 1.3. */
#define MUIMASTER_YES_INLINE_STDARG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <libraries/mui.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "zk_linktext.h"

enum { ID_TOP = 100, ID_MID, ID_END, ID_QUERY, ID_SHORT, ID_LONG, ID_SCROLLED };

static FILE *lg;
static Object *app, *win, *view, *sb, *status;
static char *text;
static long nparas = 4600;

static void report(const char *why)
{
    IPTR e = 0, v = 0, f = 0, lf = 0, lc = 0;
    static char buf[200];
    get(sb, MUIA_Prop_Entries, &e);
    get(sb, MUIA_Prop_Visible, &v);
    get(sb, MUIA_Prop_First, &f);
    get(view, MUIA_Linktext_First, &lf);
    get(view, MUIA_Linktext_LineCount, &lc);
    snprintf(buf, sizeof buf, "%s: prop entries %lu visible %lu first %lu | view first %lu of %lu lines",
             why, (unsigned long)e, (unsigned long)v, (unsigned long)f, (unsigned long)lf, (unsigned long)lc);
    set(status, MUIA_Text_Contents, (IPTR)buf);
    if (lg) { fprintf(lg, "%s\n", buf); fflush(lg); }
    printf("%s\n", buf);
}

static void make(long n)
{
    long i;
    size_t cap = (size_t)n * 90 + 16, len = 0;
    free(text);
    text = malloc(cap);
    if (!text) return;
    for (i = 0; i < n; i++) {
        if (i % 4 == 3) len += (size_t)snprintf(text + len, cap - len, "\n");
        else len += (size_t)snprintf(text + len, cap - len, "Paragraph %ld: some words that make one line of text in the view, not too long.\n", i);
    }
    DoMethod(view, MUIM_Linktext_SetContent, (IPTR)text, (ULONG)~0UL, (IPTR)NULL, 0);
}

static Object *btn(const char *s, ULONG id)
{
    Object *o = SimpleButton((IPTR)s);
    if (o) DoMethod(o, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, id);
    return o;
}

int main(int argc, char **argv)
{
    Object *root, *g1, *body;
    ULONG sigs = 0;
    int done = 0, i;
    const char *logname = NULL;
    struct TagItem extra[4];

    for (i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "LOG") && i + 1 < argc) logname = argv[++i];
        else if (atol(argv[i]) > 0) nparas = atol(argv[i]);
    }
    if (logname) lg = fopen(logname, "w");
    if (!zk_linktext_init()) { printf("no class\n"); return 20; }
    sb = ScrollbarObject, End;
    if (!sb) { printf("no scrollbar\n"); return 21; }
    extra[0].ti_Tag = MUIA_Linktext_Scroller; extra[0].ti_Data = (IPTR)sb;
    extra[1].ti_Tag = MUIA_Linktext_Lines; extra[1].ti_Data = 12;
    extra[2].ti_Tag = MUIA_Linktext_EmptyLine; extra[2].ti_Data = 4;
    extra[3].ti_Tag = TAG_DONE; extra[3].ti_Data = 0;
    app = ApplicationObject, MUIA_Application_Title, (IPTR)"zk-scrollprobe",
        SubWindow, (IPTR)(win = WindowObject, MUIA_Window_Title, (IPTR)"zk_scrollprobe", MUIA_Window_ID, 0,
            MUIA_Window_Width, MUIV_Window_Width_Visible(60), MUIA_Window_Height, MUIV_Window_Height_Visible(60),
            WindowContents, (IPTR)(root = VGroup, End), End),
        End;
    if (!app) { printf("no application\n"); return 22; }
    body = zk_linktext_new(&view, 0, extra);
    status = TextObject, MUIA_Frame, MUIV_Frame_Text, MUIA_Text_Contents, (IPTR)"-", End;
    g1 = HGroup, Child, (IPTR)btn("Top", ID_TOP), Child, (IPTR)btn("Mid", ID_MID), Child, (IPTR)btn("End", ID_END),
                 Child, (IPTR)btn("Query", ID_QUERY), Child, (IPTR)btn("Short", ID_SHORT), Child, (IPTR)btn("Long", ID_LONG), End;
    if (!body || !status || !g1) { printf("no objects\n"); MUI_DisposeObject(app); return 23; }
    DoMethod(root, OM_ADDMEMBER, (IPTR)(HGroup, Child, (IPTR)body, Child, (IPTR)sb, End));
    DoMethod(root, OM_ADDMEMBER, (IPTR)status);
    DoMethod(root, OM_ADDMEMBER, (IPTR)g1);
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    DoMethod(sb, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (IPTR)app, 2, MUIM_Application_ReturnID, ID_SCROLLED);
    make(nparas);
    set(win, MUIA_Window_Open, TRUE);
    report("open");
    while (!done) {
        ULONG id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);
        switch (id) {
        case MUIV_Application_ReturnID_Quit: done = 1; break;
        case ID_TOP: set(view, MUIA_Linktext_First, 0); report("top"); break;
        case ID_MID: { IPTR lc = 0; get(view, MUIA_Linktext_LineCount, &lc); set(view, MUIA_Linktext_First, lc / 2); report("mid"); break; }
        case ID_END: set(view, MUIA_Linktext_First, (ULONG)~0UL); report("end"); break;
        case ID_QUERY: report("query"); break;
        case ID_SHORT: make(20); report("short"); break;
        case ID_LONG: make(nparas); report("long"); break;
        case ID_SCROLLED: report("prop"); break;
        }
        if (!done && sigs) sigs = Wait(sigs | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C) done = 1;
    }
    MUI_DisposeObject(app);
    zk_linktext_exit();
    free(text);
    if (lg) fclose(lg);
    return 0;
}
