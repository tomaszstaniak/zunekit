/* SPDX-License-Identifier: MIT */
/* Linktext in UTF-8 mode with the review's text set (2026-11-07):
 *   zk-utf8demo [LOG file]
 * One view, MUIA_Linktext_UTF8 on: Polish, Greek, Cyrillic, maths, a
 * composed and a decomposed letter, a character outside the BMP, a joined
 * emoji, a table and code. Copy puts the selection on the clipboard as
 * UTF-8 (a drag, then Amiga+C); "Check" reads the clipboard's UTF8
 * chunk back and compares it with the source bytes it should hold. The log
 * says which fonts opened and how many characters came from fallbacks. */
#define MUIMASTER_YES_INLINE_STDARG
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <libraries/mui.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/iffparse.h>
#include <clib/alib_protos.h>
#include "zk_linktext.h"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif

struct Library *IFFParseBase;
static FILE *lg;
#define L(...) do { if (lg) { fprintf(lg, __VA_ARGS__); fputc('\n', lg); fflush(lg); } printf(__VA_ARGS__); putchar('\n'); fflush(stdout); } while (0)

enum { ID_CHECK = 100 };

static const char text[] =
    "Zażółć gęślą jaźń. ZAŻÓŁĆ GĘŚLĄ JAŹŃ.\n"
    "\n"
    "Ελληνικά: αβγ δέκα. Русский: Жизнь и съешь же ещё этих мягких булок.\n"
    "\n"
    "Maths: ∀x ∈ ℝ: x² ≥ 0, ∑ 1/n², π ≈ 3.14159.\n"
    "\n"
    "Composed é, decomposed e\xcc\x81; a G clef 𝄞 outside the BMP; a family 👩\xe2\x80\x8d👧.\n"
    "\n"
    "Plik\tSłowa\tUwagi\n"
    "main.c\t142\tGłówna pętla: zażółć gęślą jaźń.\n"
    "ψ.txt\t7\tΚείμενο και кириллица.\n"
    "\n"
    "while ((ch = fgetc(fp)) != EOF) /* liczy słowa */\n"
    "    count++;\n";

static Object *app, *win, *view;
static struct MUI_LinktextBlock blocks[3];
static struct MUI_LinktextSpan spans[2];

/* The text of the clipboard's UTF8 chunk into buf; its length or -1. */
static long clip_utf8(char *buf, long max)
{
    struct IFFHandle *iff;
    long got = -1;
    if (!(iff = AllocIFF())) return -1;
    if ((iff->iff_Stream = (IPTR)OpenClipboard(0))) {
        InitIFFasClip(iff);
        if (!OpenIFF(iff, IFFF_READ)) {
            if (!StopChunk(iff, MAKE_ID('F','T','X','T'), MAKE_ID('U','T','F','8')) && !ParseIFF(iff, IFFPARSE_SCAN))
                got = ReadChunkBytes(iff, buf, max);
            CloseIFF(iff);
        }
        CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
    }
    FreeIFF(iff);
    return got;
}

static void check(void)
{
    static char buf[4096];
    long n = clip_utf8(buf, sizeof buf - 1);
    if (n < 0) { L("CHECK clipboard has no UTF8 chunk"); return; }
    buf[n] = 0;
    L("CHECK clipboard UTF8 %ld bytes; %s the text's own bytes", n,
      strstr(text, buf) && n > 0 ? "equal to" : "NOT equal to");
}

int main(int argc, char **argv)
{
    ULONG sigs = 0;
    int done = 0, i;
    const char *logname = NULL;
    Object *root, *body, *bar;
    struct TagItem extra[3];
    const char *tbl, *code;

    for (i = 1; i < argc; i++)
        if (!strcasecmp(argv[i], "LOG") && i + 1 < argc) logname = argv[++i];
    if (logname) lg = fopen(logname, "w");
    if (!(IFFParseBase = OpenLibrary((CONST_STRPTR)"iffparse.library", 39))) { L("no iffparse"); return 20; }
    if (!zk_linktext_init()) { L("no class"); return 20; }

    tbl = strstr(text, "Plik\t");
    code = strstr(text, "while");
    blocks[0].Start = (ULONG)(tbl - text);
    blocks[0].Length = (ULONG)(strstr(tbl, "\n\n") - tbl);
    blocks[0].Flags = MUIV_Linktext_Block_Table | MUIV_Linktext_Block_Head | MUIV_Linktext_Block_Fill | MUIV_Linktext_Block_Pad;
    blocks[0].PadX = 8; blocks[0].PadY = 2;
    blocks[0].Background = MUIV_Linktext_RGB(0xffffff);
    blocks[0].Head = MUIV_Linktext_RGB(0xeef1f5);
    blocks[0].Rule = MUIV_Linktext_RGB(0xc8ced6);
    blocks[0].Align = MUIV_Linktext_Align_Right << 2;
    blocks[1].Start = (ULONG)(code - text);
    blocks[1].Length = (ULONG)(strlen(code) - 1);
    blocks[1].Flags = MUIV_Linktext_Block_Fill;
    blocks[1].Indent = 2; blocks[1].RightIndent = 2;
    blocks[1].Background = MUIV_Linktext_RGB(0xeef0f3);
    spans[0].Start = (ULONG)(tbl - text);
    spans[0].Length = (ULONG)(strchr(tbl, '\n') - tbl);
    spans[0].Flags = MUIV_Linktext_Span_Bold;
    spans[1].Start = (ULONG)(code - text);
    spans[1].Length = (ULONG)(strlen(code) - 1);
    spans[1].Flags = MUIV_Linktext_Span_Fixed;

    extra[0].ti_Tag = MUIA_Linktext_UTF8; extra[0].ti_Data = TRUE;
    extra[1].ti_Tag = MUIA_Linktext_Lines; extra[1].ti_Data = 16;
    extra[2].ti_Tag = TAG_DONE; extra[2].ti_Data = 0;

    app = ApplicationObject, MUIA_Application_Title, (IPTR)"zk-utf8demo",
        SubWindow, (IPTR)(win = WindowObject, MUIA_Window_Title, (IPTR)"Linktext UTF-8", MUIA_Window_ID, 0,
            MUIA_Window_Width, MUIV_Window_Width_Visible(55), MUIA_Window_Height, MUIV_Window_Height_Visible(60),
            WindowContents, (IPTR)(root = VGroup, End), End),
        End;
    if (!app) { L("no application"); return 21; }
    body = zk_linktext_new(&view, 1, extra);
    bar = SimpleButton((IPTR)"Check");
    if (!body || !bar) { L("no objects"); MUI_DisposeObject(app); return 22; }
    DoMethod(root, OM_ADDMEMBER, (IPTR)body);
    DoMethod(root, OM_ADDMEMBER, (IPTR)bar);
    DoMethod(bar, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, ID_CHECK);
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2, MUIM_Application_ReturnID,
             MUIV_Application_ReturnID_Quit);
    L("SETDOCUMENT status=%lu", (unsigned long)DoMethod(view, MUIM_Linktext_SetDocument, (IPTR)text, (ULONG)~0UL,
                                                         (IPTR)spans, 2, (IPTR)blocks, 2));
    set(win, MUIA_Window_Open, TRUE);
    {
        IPTR u = 0, lc = 0;
        get(view, MUIA_Linktext_UTF8, &u);
        get(view, MUIA_Linktext_LineCount, &lc);
        L("UTF8=%lu lines=%lu", (unsigned long)u, (unsigned long)lc);
    }
    while (!done) {
        ULONG id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);
        switch (id) {
        case MUIV_Application_ReturnID_Quit: done = 1; break;
        case ID_CHECK: check(); break;
        }
        if (!done && sigs) sigs = Wait(sigs | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C) done = 1;
    }
    MUI_DisposeObject(app);
    zk_linktext_exit();
    CloseLibrary(IFFParseBase);
    L("END");
    if (lg) fclose(lg);
    return 0;
}
