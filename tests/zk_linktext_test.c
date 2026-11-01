/* SPDX-License-Identifier: MIT */
/* Demonstration and test of zk_linktext.
 *   zk-linktext-test [LOG file]
 * Every action goes to the log, the target as it arrived and its length.
 * The buttons do what a test needs: new content, spans that must be refused,
 * failed allocations (a LINKTEXT_TEST build), the position of every link
 * fragment for aimed clicks, a window close from inside the hook. The class
 * opens nothing; this program only logs. */
#define MUIMASTER_YES_INLINE_STDARG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libraries/mui.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>
#include "zk_linktext.h"

#ifdef LINKTEXT_TEST
extern long linktext_fail_after;
#endif

static FILE *lg;
#define L(...) do { if (lg) { fprintf(lg, __VA_ARGS__); fputc('\n', lg); fflush(lg); } printf(__VA_ARGS__); putchar('\n'); fflush(stdout); } while (0)

enum { ID_CONTENT = 100, ID_BAD, ID_NOMEM, ID_BOXES, ID_CLOSEHOOK, ID_REPLACEHOOK, ID_SCROLLEND, ID_FONT };

static Object *app, *win, *view;
static int close_on_action, replace_on_action;

static char long_url[1601];

#define MAXS 16
static char text[4096];
static struct MUI_LinktextSpan sp[MAXS];
static ULONG ns;

static void add(const char *t) { strcat(text, t); }
static void link_(const char *label, const char *target, IPTR id)
{
    sp[ns].Start = strlen(text);
    sp[ns].Length = strlen(label);
    sp[ns].Flags = 0;
    sp[ns].Target = (CONST_STRPTR)target;
    sp[ns].ID = id;
    ns++;
    add(label);
}
static void bold(const char *t)
{
    sp[ns].Start = strlen(text);
    sp[ns].Length = strlen(t);
    sp[ns].Flags = MUIV_Linktext_Span_Bold;
    sp[ns].Target = NULL;
    sp[ns].ID = 0;
    ns++;
    add(t);
}

/* Punctuation ends and a query, escapes and an address too long for any
   fixed buffer: all explicit targets, none guessed from text. */
static void content_a(void)
{
    text[0] = 0; ns = 0;
    bold("Links inside text.");
    add(" The first address wraps over the lines of this paragraph: ");
    link_("https://example.org/a/very/long/address/that/has/to/wrap/over/several/lines/of/the/window?x=1&y=%2F", "https://example.org/a/very/long/address/that/has/to/wrap/over/several/lines/of/the/window?x=1&y=%2F", 1);
    add(" and text goes on after it, then a ");
    link_("Project website", "https://example.org/project", 2);
    add(" with a short label, the same ");
    link_("website again", "https://example.org/project", 3);
    add(" and ");
    link_("a long one", long_url, 4);
    add(".\n\nEndings stay: ");
    link_("bang", "https://example.org/download?token=abc!", 5);
    add(", ");
    link_("semicolon", "https://example.org/x;", 6);
    add(", ");
    link_("question", "https://example.org/x?", 7);
    add(", ");
    link_("apostrophe", "https://example.org/x'", 8);
    add(", ");
    link_("period", "https://example.org/x.", 9);
    add(" and the end of the text.");
}

static void content_b(void)
{
    text[0] = 0; ns = 0;
    add("Other content. Only one ");
    link_("link", "https://example.org/other", 20);
    add(" here, and no selection left from before.");
}

static ULONG show(void)
{
    ULONG r = DoMethod(view, MUIM_Linktext_SetContent, (IPTR)text, (ULONG)~0UL, (IPTR)sp, ns);
    L("SETCONTENT status=%lu spans=%lu", (unsigned long)r, (unsigned long)ns);
    return r;
}

static void boxes(void)
{
    ULONG l, f;
    IPTR lines = 0, first = 0;
    get(view, MUIA_Linktext_LineCount, &lines);
    get(view, MUIA_Linktext_First, &first);
    {
        struct Window *w = NULL;
        get(win, MUIA_Window_Window, &w);
        if (w) L("WINDOW left=%d top=%d width=%d height=%d", w->LeftEdge, w->TopEdge, w->Width, w->Height);
    }
    L("LINES count=%lu first=%lu", (unsigned long)lines, (unsigned long)first);
    for (l = 0; l < ns + 2; l++)
        for (f = 0; f < 64; f++) {
            struct IBox b;
            memset(&b, 0, sizeof b);
            if (DoMethod(view, MUIM_Linktext_LinkBox, l, f, (IPTR)&b))
                L("BOX link=%lu frag=%lu x=%d y=%d w=%d h=%d", (unsigned long)l, (unsigned long)f, b.Left, b.Top, b.Width, b.Height);
            else if (!b.Width) break;
            else L("BOX link=%lu frag=%lu hidden y=%d", (unsigned long)l, (unsigned long)f, b.Top);
        }
}

static void refused(void)
{
    struct MUI_LinktextSpan b[2];
    ULONG r;
    memset(b, 0, sizeof b);
    b[0].Start = 5; b[0].Length = 9999999; b[0].Target = (CONST_STRPTR)"https://x";
    r = DoMethod(view, MUIM_Linktext_SetContent, (IPTR)"short", (ULONG)~0UL, (IPTR)b, 1);
    L("BAD range -> %lu", (unsigned long)r);
    b[0].Start = 0; b[0].Length = 3; b[1].Start = 2; b[1].Length = 2; b[1].Target = (CONST_STRPTR)"https://y";
    r = DoMethod(view, MUIM_Linktext_SetContent, (IPTR)"short", (ULONG)~0UL, (IPTR)b, 2);
    L("BAD overlap -> %lu", (unsigned long)r);
    b[0].Start = 0; b[0].Length = 2; b[0].Target = (CONST_STRPTR)"";
    r = DoMethod(view, MUIM_Linktext_SetContent, (IPTR)"short", (ULONG)~0UL, (IPTR)b, 1);
    L("BAD empty target -> %lu", (unsigned long)r);
    b[0].Start = 0xFFFFFFF0; b[0].Length = 0x20; b[0].Target = (CONST_STRPTR)"https://z";
    r = DoMethod(view, MUIM_Linktext_SetContent, (IPTR)"short", (ULONG)~0UL, (IPTR)b, 1);
    L("BAD wrap-around -> %lu", (unsigned long)r);
}

static void nomem(void)
{
#ifdef LINKTEXT_TEST
    int k;
    for (k = 0; k < 4; k++) {
        ULONG r;
        linktext_fail_after = k;
        r = DoMethod(view, MUIM_Linktext_SetContent, (IPTR)"replacement that must not appear", (ULONG)~0UL, NULL, 0);
        linktext_fail_after = -1;
        L("NOMEM fail-after=%d -> %lu", k, (unsigned long)r);
        if (r == MUIV_Linktext_Status_OK) { show(); break; }
    }
#else
    L("NOMEM needs a LINKTEXT_TEST build");
#endif
}

/* Target is usable to the end of the hook even when the content goes. */
static IPTR action(struct Hook *h, Object *o, struct MUI_LinktextAction *a)
{
    unsigned long sum = 0;
    ULONG i;
    (void)h; (void)o;
    if (replace_on_action) {
        replace_on_action = 0;
        content_b();
        show();
    }
    for (i = 0; i < a->TargetLength; i++) sum = sum * 31 + (unsigned char)a->Target[i];
    L("ACTION %s link=%lu id=%lu len=%lu sum=%08lx target=%s", a->Action == MUIV_Linktext_Action_Open ? "OPEN" : "COPY",
      (unsigned long)a->Link, (unsigned long)a->ID, (unsigned long)a->TargetLength, sum & 0xffffffffUL, (const char *)a->Target);
    if (close_on_action) {
        close_on_action = 0;
        set(win, MUIA_Window_Open, FALSE);
        L("CLOSED from hook");
        DoMethod(app, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    }
    return a->Action == MUIV_Linktext_Action_Copy ? FALSE : TRUE;   /* the class copies */
}

static struct Hook hook;

static Object *btn(const char *t, int id)
{
    Object *b = SimpleButton(t);
    if (b) DoMethod(b, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, id);
    return b;
}

int main(int argc, char **argv)
{
    Object *root, *g1, *g2, *body;
    ULONG sigs = 0;
    int done = 0, i;
    struct TagItem extra[] = { { MUIA_Linktext_ActionHook, (IPTR)&hook }, { MUIA_Linktext_Lines, 10 }, { TAG_DONE, 0 } };

    if (argc > 2) lg = fopen(argv[2], "w");
    else if (argc > 1) lg = fopen(argv[1], "w");
    strcpy(long_url, "https://example.org/");
    for (i = strlen(long_url); i < 1500; i++) long_url[i] = "abcdefghij"[i % 10];
    long_url[1500] = '!';
    long_url[1501] = 0;

    hook.h_Entry = (HOOKFUNC)HookEntry;
    hook.h_SubEntry = (HOOKFUNC)action;

    if (!zk_linktext_init()) { L("no class"); return 20; }
    /* Buttons need the application for their notifications, so the tree is
       added to the window afterwards. */
    app = ApplicationObject, MUIA_Application_Title, (IPTR)"zk-linktext-test",
        SubWindow, (IPTR)(win = WindowObject, MUIA_Window_Title, (IPTR)"zk_linktext", MUIA_Window_ID, 0,
            MUIA_Window_Width, MUIV_Window_Width_Visible(46), MUIA_Window_Height, MUIV_Window_Height_Visible(52),
            WindowContents, (IPTR)(root = VGroup, End), End),
        End;
    if (!app) { L("no application"); return 21; }
    body = zk_linktext_new(&view, 1, extra);
    g1 = HGroup, Child, (IPTR)btn("Content", ID_CONTENT), Child, (IPTR)btn("Other", ID_REPLACEHOOK),
                 Child, (IPTR)btn("Bad", ID_BAD), Child, (IPTR)btn("NoMem", ID_NOMEM), End;
    g2 = HGroup, Child, (IPTR)btn("Boxes", ID_BOXES), Child, (IPTR)btn("CloseHook", ID_CLOSEHOOK),
                 Child, (IPTR)btn("End", ID_SCROLLEND), Child, (IPTR)btn("Font", ID_FONT), End;
    if (!body || !g1 || !g2) { L("no objects"); MUI_DisposeObject(app); return 22; }
    DoMethod(root, OM_ADDMEMBER, (IPTR)body);
    DoMethod(root, OM_ADDMEMBER, (IPTR)g1);
    DoMethod(root, OM_ADDMEMBER, (IPTR)g2);
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

    content_a();
    show();
    set(win, MUIA_Window_Open, TRUE);
    boxes();
    while (!done) {
        ULONG id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);
        switch (id) {
        case MUIV_Application_ReturnID_Quit: done = 1; break;
        case ID_CONTENT: content_a(); show(); boxes(); break;
        case ID_REPLACEHOOK: replace_on_action = 1; L("next action replaces the content from the hook"); break;
        case ID_BAD: refused(); boxes(); break;
        case ID_NOMEM: nomem(); boxes(); break;
        case ID_BOXES: boxes(); break;
        case ID_CLOSEHOOK: close_on_action = 1; L("next action closes the window from the hook"); break;
        case ID_SCROLLEND: set(view, MUIA_Linktext_First, 100000); boxes(); break;
        case ID_FONT: {
            /* The way a font comes to a window: closed and set up again. */
            static const IPTR fonts[] = { MUIV_Font_Fixed, MUIV_Font_Big, MUIV_Font_Tiny, MUIV_Font_List };
            static int f;
            set(win, MUIA_Window_Open, FALSE);
            set(view, MUIA_Font, fonts[f]);
            f = (f + 1) % 4;
            set(win, MUIA_Window_Open, TRUE);
            boxes();
            break;
        }
        }
        if (!done && sigs) sigs = Wait(sigs | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C) done = 1;
    }
    MUI_DisposeObject(app);
    zk_linktext_exit();
    L("END");
    if (lg) fclose(lg);
    return 0;
}
