/* SPDX-License-Identifier: MIT */
/* zk_list (NList with UTF-8 texts) with a multilingual set of rows:
 *   zk-listdemo [LOG file] [FONTS drawer] [AUTO]
 * Two columns (format "BAR,"), titles in UTF-8, a display hook that splits
 * an entry at its TAB, NList's ESC b and ESC u in rows. FONTS names a
 * drawer with DejaVuSans.ttf and NotoEmoji.ttf to use before the system's
 * faces. With AUTO the checks run and the window closes; each says PASS or
 * FAIL, and the last line counts them:
 *   - the class is made, the list says it draws UTF-8 (MUIA_ZkList_Status);
 *   - every entry is there, and each visible row has ink in both columns
 *     (pixels read back from the window: the cell object drew);
 *   - the active row, selected, still has ink (its pen);
 *   - a row replaced (MUIM_NList_ReplaceSingle) is drawn with its new text,
 *     wider than before;
 *   - the entries are the UTF-8 bytes given (MUIM_NList_GetEntry).
 * Without AUTO the window stays open to be looked at. */
#define MUIMASTER_YES_INLINE_STDARG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <libraries/mui.h>
#include <mui/NList_mcc.h>
#include <mui/NListview_mcc.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/cybergraphics.h>
#include <clib/alib_protos.h>
#include <aros/asmcall.h>
#include "zk_list.h"

struct Library *CyberGfxBase;
static FILE *lg;
static int passes, fails;
#define L(...) do { if (lg) { fprintf(lg, __VA_ARGS__); fputc('\n', lg); fflush(lg); } printf(__VA_ARGS__); putchar('\n'); fflush(stdout); } while (0)
#define CHECK(c, ...) do { if (c) { passes++; L("PASS " __VA_ARGS__); } else { fails++; L("FAIL " __VA_ARGS__); } } while (0)

static const char *const rows[] = {
    "Zażółć gęślą jaźń\t13 listopada",
    "\033bΕλληνικά: αβγ δέκα\tσήμερα",
    "Русский: съешь же ещё этих булок\tсегодня",
    "Composed é, decomposed e\xcc\x81, 𝄞, 👩\xe2\x80\x8d👧\t2026-11-13",
    "Maths: ∀x ∈ ℝ: x² ≥ 0, π ≈ 3.14159\tnow",
    "\033uUnderlined\033n then plain\tyesterday",
    "ASCII only row\t12:00",
    "Ünïcödé façade, naïve café\tdemain",
};
#define NROWS (sizeof rows / sizeof rows[0])

static const char *const replaced = "Nowy tytuł: żółw, λόγος, ёлка, 🐢\tteraz";

AROS_UFH3(IPTR, disp_fn, AROS_UFHA(struct Hook *, h, A0), AROS_UFHA(char **, col, A2), AROS_UFHA(char *, e, A1))
{
    AROS_USERFUNC_INIT
    static char a[300], b[100];
    (void)h;
    if (!e) { col[0] = (char *)"Tytuł"; col[1] = (char *)"Kiedy · Πότε"; return 0; }
    {
        const char *tab = strchr(e, '\t');
        snprintf(a, sizeof a, "%.*s", tab ? (int)(tab - e) : (int)strlen(e), e);
        snprintf(b, sizeof b, "%s", tab ? tab + 1 : "");
    }
    col[0] = a; col[1] = b;
    return 0;
    AROS_USERFUNC_EXIT
}
static struct Hook disp_hook = { {0}, (HOOKFUNC)disp_fn, 0, 0 };

static Object *app, *win, *list;

/* Pixels in x0..x1 of row band y0..y1 that differ from the band's first
   pixel's colour (the row's background where the text does not start). */
static long ink(LONG x0, LONG x1, LONG y0, LONG y1)
{
    struct RastPort *rp = _rp(list);
    ULONG bg;
    long n = 0;
    LONG x, y;
    if (!rp || !CyberGfxBase || x1 <= x0 || y1 <= y0) return -1;
    bg = ReadRGBPixel(rp, x1, y0 + (y1 - y0) / 2);
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (ReadRGBPixel(rp, x, y) != bg) n++;
    return n;
}

/* The pixel band of entry ent and the columns' split: 1 when visible. */
static int band(LONG ent, LONG *y0, LONG *y1, LONG *split)
{
    struct MUI_NList_TestPos_Result r;
    LONG x = _mleft(list) + 4, y, first = -1, last = -1;
    *split = _mleft(list) + _mwidth(list) / 2;
    for (y = _mtop(list); y < _mtop(list) + _mheight(list); y++) {
        memset(&r, 0, sizeof r);
        DoMethod(list, MUIM_NList_TestPos, x, y, (IPTR)&r);
        if (r.entry == ent) { if (first < 0) first = y; last = y; }
        if (r.entry == ent && r.column == 1 && r.xoffset >= 0) { /* not used */ }
    }
    if (first < 0) return 0;
    /* the column split: the first x of column 1 on that band */
    for (x = _mleft(list); x < _mleft(list) + _mwidth(list); x++) {
        memset(&r, 0, sizeof r);
        DoMethod(list, MUIM_NList_TestPos, x, (first + last) / 2, (IPTR)&r);
        if (r.column == 1) { *split = x; break; }
    }
    *y0 = first; *y1 = last;
    return 1;
}

static void row_ink(LONG ent, long *c0, long *c1)
{
    LONG y0, y1, split;
    *c0 = *c1 = -1;
    if (!band(ent, &y0, &y1, &split)) return;
    *c0 = ink(_mleft(list) + 2, split - 6, y0, y1);
    *c1 = ink(split + 2, _mleft(list) + _mwidth(list) - 4, y0, y1);
}

static void settle(void)
{
    ULONG sigs = 0;
    int i;
    for (i = 0; i < 20; i++) { DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs); Delay(2); }
}

static void checks(void)
{
    IPTR st = 99, n = 0, act = 0;
    ULONG i;
    long a, b, before;
    char *e = NULL;
    settle();
    GetAttr(MUIA_ZkList_Status, list, &st);
    CHECK(st == ZKLIST_DRAWN, "status %lu (0: UTF-8 drawn)", (unsigned long)st);
    GetAttr(MUIA_NList_Entries, list, &n);
    CHECK(n == NROWS, "entries %lu of %lu", (unsigned long)n, (unsigned long)NROWS);
    for (i = 0; i < NROWS; i++) {
        row_ink((LONG)i, &a, &b);
        if (a < 0) { L("NOTE row %lu not visible", (unsigned long)i); continue; }
        CHECK(a > 20 && b > 20, "row %lu ink %ld | %ld", (unsigned long)i, a, b);
    }
    for (i = 0; i < NROWS; i++) {
        e = NULL;
        DoMethod(list, MUIM_NList_GetEntry, i, (IPTR)&e);
        CHECK(e && !strcmp(e, rows[i]), "entry %lu holds its UTF-8 bytes", (unsigned long)i);
    }
    set(list, MUIA_NList_Active, 2);
    settle();
    GetAttr(MUIA_NList_Active, list, &act);
    row_ink(2, &a, &b);
    CHECK(act == 2 && a > 20 && b > 20, "active row 2 ink %ld | %ld", a, b);
    row_ink(6, &before, &b);
    DoMethod(list, MUIM_NList_ReplaceSingle, (IPTR)replaced, 6, NOWRAP, ALIGN_LEFT);
    settle();
    row_ink(6, &a, &b);
    e = NULL;
    DoMethod(list, MUIM_NList_GetEntry, 6, (IPTR)&e);
    CHECK(e && !strcmp(e, replaced) && a > before, "row 6 replaced: ink %ld, was %ld", a, before);
}

int main(int argc, char **argv)
{
    ULONG sigs = 0;
    int done = 0, i, autorun = 0;
    const char *logname = NULL, *fonts = NULL;
    static char p1[300], p2[300];
    static CONST_STRPTR prop[2], fall[3];
    static zt_fontset set;
    struct Hook *dh = &disp_hook;

    for (i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "LOG") && i + 1 < argc) logname = argv[++i];
        else if (!strcasecmp(argv[i], "FONTS") && i + 1 < argc) fonts = argv[++i];
        else if (!strcasecmp(argv[i], "AUTO")) autorun = 1;
    }
    if (logname) lg = fopen(logname, "w");
    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 0);
    if (fonts) {
        snprintf(p1, sizeof p1, "%s", fonts); AddPart((STRPTR)p1, (CONST_STRPTR)"DejaVuSans.ttf", sizeof p1);
        snprintf(p2, sizeof p2, "%s", fonts); AddPart((STRPTR)p2, (CONST_STRPTR)"NotoEmoji.ttf", sizeof p2);
        prop[0] = (CONST_STRPTR)p1; prop[1] = NULL;
        fall[0] = (CONST_STRPTR)p2; fall[1] = (CONST_STRPTR)p1; fall[2] = NULL;
        set.proportional = prop; set.monospace = NULL; set.fallbacks = fall;
    }
    CHECK(zk_list_init(), "class made");
    if (!zk_list_class()) { L("RESULT %d passed, %d failed", passes, fails); return 20; }

    app = ApplicationObject, MUIA_Application_Title, (IPTR)"zk-listdemo",
        SubWindow, (IPTR)(win = WindowObject, MUIA_Window_Title, (IPTR)"zk_list UTF-8", MUIA_Window_ID, 0,
            MUIA_Window_Width, MUIV_Window_Width_Visible(50), MUIA_Window_Height, MUIV_Window_Height_Visible(45),
            WindowContents, (IPTR)(VGroup,
                Child, (IPTR)(NListviewObject,
                    MUIA_NListview_NList, (IPTR)(list = ZkListObject
                        MUIA_ZkList_UTF8, TRUE,
                        MUIA_ZkList_FontSet, (IPTR)(fonts ? &set : NULL),
                        MUIA_NList_Format, (IPTR)"BAR,",
                        MUIA_NList_Title, TRUE,
                        MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
                        MUIA_NList_DestructHook, MUIV_NList_DestructHook_String,
                        MUIA_NList_DisplayHook, (IPTR)dh,
                        ZkListEnd),
                    End),
                End),
            End),
        End;
    if (!app || !list) { L("FAIL no application"); L("RESULT %d passed, %d failed", passes, fails + 1); return 21; }
    for (i = 0; i < (int)NROWS; i++) DoMethod(list, MUIM_NList_InsertSingle, (IPTR)rows[i], MUIV_NList_Insert_Bottom);
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2, MUIM_Application_ReturnID,
             MUIV_Application_ReturnID_Quit);
    set(win, MUIA_Window_Open, TRUE);
    if (autorun) { checks(); done = 1; }
    while (!done) {
        ULONG id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);
        if (id == MUIV_Application_ReturnID_Quit) done = 1;
        if (!done && sigs) sigs = Wait(sigs | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C) done = 1;
    }
    MUI_DisposeObject(app);
    zk_list_exit();
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    L("RESULT %d passed, %d failed", passes, fails);
    if (lg) fclose(lg);
    return fails ? 5 : 0;
}
