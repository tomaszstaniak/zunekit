/* SPDX-License-Identifier: MIT */
/* zk_edit in a small program (the Unicode editing scenario):
 *   zk-editdemo [FILE path] [LOG file]
 * A text editor, a code editor (Fixed) and a one-line field. "Fill" types
 * the review's text set at the text editor's caret (Polish, Greek,
 * Cyrillic, a decomposed letter, a character outside the BMP, a joined
 * emoji); "Save" writes the text editor's bytes to FILE (RAM:zk-edit.txt);
 * "Reread" reads FILE back into the editor and compares the bytes; "Dump"
 * logs the text with every byte above 0x7e as \xNN; "Clipboard" logs the
 * clipboard's UTF8 and CHRS chunks. The log is the evidence: the screen
 * shows the appearance. */
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
#include "zk_edit.h"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif

struct Library *IFFParseBase;
static FILE *lg;
#define L(...) do { if (lg) { fprintf(lg, __VA_ARGS__); fputc('\n', lg); fflush(lg); } printf(__VA_ARGS__); putchar('\n'); fflush(stdout); } while (0)

enum { ID_FILL = 100, ID_SAVE, ID_REREAD, ID_DUMP, ID_CLIP, ID_ACK, ID_MSG };

static const char set_text[] =
    "Zażółć gęślą jaźń. Ελληνικά: αβγ. Русский: съешь ещё.\n"
    "e\xcc\x81 (decomposed), 𝄞 (outside the BMP), 👩\xe2\x80\x8d👧 (one emoji).\n";
static const char code_text[] = "while ((ch = fgetc(fp)) != EOF) /* liczy słowa */\n    count++;\n";

static Object *app, *win, *edit, *code, *field, *status;
static const char *path = "RAM:zk-edit.txt";

static void dump(const char *tag, const char *s, size_t n)
{
    static char out[8192];
    size_t i, k = 0;
    for (i = 0; i < n && k + 5 < sizeof out; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c < 0x7f && c != '\\') out[k++] = (char)c;
        else k += (size_t)sprintf(out + k, "\\x%02x", c);
    }
    out[k] = 0;
    L("%s %lu bytes: %s%s", tag, (unsigned long)n, out, i < n ? " ..." : "");
}

static const char *contents(Object *o, size_t *n)
{
    IPTR s = 0;
    get(o, MUIA_ZkEdit_Contents, &s);
    *n = s ? strlen((const char *)s) : 0;
    return s ? (const char *)s : "";
}

static void save(void)
{
    size_t n;
    const char *s = contents(edit, &n);
    FILE *f = fopen(path, "wb");
    if (!f) { L("SAVE cannot open %s", path); return; }
    if (fwrite(s, 1, n, f) != n) L("SAVE short write");
    fclose(f);
    L("SAVE %s %lu bytes", path, (unsigned long)n);
}

static void reread(void)
{
    size_t n, m;
    const char *s = contents(edit, &n);
    char *before, *buf;
    FILE *f = fopen(path, "rb");
    long r;
    if (!f) { L("REREAD cannot open %s", path); return; }
    fseek(f, 0, SEEK_END);
    m = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc(m + 1);
    before = (char *)malloc(n + 1);
    if (!buf || !before) { L("REREAD no memory"); fclose(f); free(buf); free(before); return; }
    m = fread(buf, 1, m, f);
    buf[m] = 0;
    fclose(f);
    memcpy(before, s, n);
    L("REREAD file %lu bytes, editor %lu bytes: %s", (unsigned long)m, (unsigned long)n,
      m == n && !memcmp(buf, before, n) ? "identical" : "DIFFERENT");
    r = (long)DoMethod(edit, MUIM_ZkEdit_SetText, (IPTR)buf, (IPTR)m);
    s = contents(edit, &n);
    L("REREAD SetText=%ld; editor now %lu bytes: %s the file", r, (unsigned long)n,
      m == n && !memcmp(buf, s, n) ? "identical to" : "DIFFERENT from");
    free(buf);
    free(before);
}

static void clip(void)
{
    struct IFFHandle *iff;
    static const LONG stops[] = { MAKE_ID('F','T','X','T'), MAKE_ID('U','T','F','8'), MAKE_ID('F','T','X','T'), MAKE_ID('C','H','R','S') };
    static char buf[4096];
    int seen = 0;
    if (!(iff = AllocIFF())) return;
    if ((iff->iff_Stream = (IPTR)OpenClipboard(0))) {
        InitIFFasClip(iff);
        if (!OpenIFF(iff, IFFF_READ)) {
            if (!StopChunks(iff, (LONG *)stops, 2))
                while (!ParseIFF(iff, IFFPARSE_SCAN)) {
                    struct ContextNode *cn = CurrentChunk(iff);
                    LONG got = ReadChunkBytes(iff, buf, sizeof buf);
                    if (got < 0) got = 0;
                    dump(cn && cn->cn_ID == MAKE_ID('U','T','F','8') ? "CLIP UTF8" : "CLIP CHRS", buf, (size_t)got);
                    seen++;
                }
            CloseIFF(iff);
        }
        CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
    }
    FreeIFF(iff);
    if (!seen) L("CLIP no text chunks");
}

int main(int argc, char **argv)
{
    ULONG sigs = 0;
    int done = 0, i;
    const char *logname = NULL;
    Object *root, *body, *codebody, *fieldbody, *b_fill, *b_save, *b_reread, *b_dump, *b_clip;
    struct TagItem e1[2], e2[4], e3[3];
    char why[200];

    for (i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "LOG") && i + 1 < argc) logname = argv[++i];
        else if (!strcasecmp(argv[i], "FILE") && i + 1 < argc) path = argv[++i];
    }
    if (logname) lg = fopen(logname, "w");
    if (!(IFFParseBase = OpenLibrary((CONST_STRPTR)"iffparse.library", 39))) { L("no iffparse"); return 20; }
    if (!zk_edit_fonts(NULL, why, sizeof why)) { L("FONTS %s", why); CloseLibrary(IFFParseBase); return 20; }
    if (!zk_edit_init()) { L("no class"); return 20; }

    e1[0].ti_Tag = MUIA_ZkEdit_Lines; e1[0].ti_Data = 8;
    e1[1].ti_Tag = TAG_DONE;
    e2[0].ti_Tag = MUIA_ZkEdit_Fixed; e2[0].ti_Data = TRUE;
    e2[1].ti_Tag = MUIA_ZkEdit_Lines; e2[1].ti_Data = 3;
    e2[2].ti_Tag = MUIA_ZkEdit_Contents; e2[2].ti_Data = (IPTR)code_text;
    e2[3].ti_Tag = TAG_DONE;
    e3[0].ti_Tag = MUIA_ZkEdit_SingleLine; e3[0].ti_Data = TRUE;
    e3[1].ti_Tag = MUIA_ZkEdit_Contents; e3[1].ti_Data = (IPTR)"Szukaj: ąę";
    e3[2].ti_Tag = TAG_DONE;

    app = ApplicationObject, MUIA_Application_Title, (IPTR)"zk-editdemo",
        SubWindow, (IPTR)(win = WindowObject, MUIA_Window_Title, (IPTR)"zk_edit", MUIA_Window_ID, 0,
            MUIA_Window_Width, MUIV_Window_Width_Visible(50), MUIA_Window_Height, MUIV_Window_Height_Visible(60),
            WindowContents, (IPTR)(root = VGroup, End), End),
        End;
    if (!app) { L("no application"); return 21; }
    body = zk_edit_new(&edit, 1, e1);
    codebody = zk_edit_new(&code, 1, e2);
    fieldbody = zk_edit_new(&field, 0, e3);
    status = TextObject, TextFrame, MUIA_Text_Contents, (IPTR)"", End;
    b_fill = SimpleButton((IPTR)"Fill");
    b_save = SimpleButton((IPTR)"Save");
    b_reread = SimpleButton((IPTR)"Reread");
    b_dump = SimpleButton((IPTR)"Dump");
    b_clip = SimpleButton((IPTR)"Clipboard");
    if (!body || !codebody || !fieldbody || !status || !b_fill || !b_save || !b_reread || !b_dump || !b_clip) {
        L("no objects"); MUI_DisposeObject(app); return 22;
    }
    DoMethod(root, OM_ADDMEMBER, (IPTR)body);
    DoMethod(root, OM_ADDMEMBER, (IPTR)codebody);
    DoMethod(root, OM_ADDMEMBER, (IPTR)fieldbody);
    DoMethod(root, OM_ADDMEMBER, (IPTR)status);
    DoMethod(root, OM_ADDMEMBER, (IPTR)HGroup, Child, (IPTR)b_fill, Child, (IPTR)b_save, Child, (IPTR)b_reread,
             Child, (IPTR)b_dump, Child, (IPTR)b_clip, End);
    DoMethod(b_fill, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, ID_FILL);
    DoMethod(b_save, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, ID_SAVE);
    DoMethod(b_reread, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, ID_REREAD);
    DoMethod(b_dump, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, ID_DUMP);
    DoMethod(b_clip, MUIM_Notify, MUIA_Pressed, FALSE, (IPTR)app, 2, MUIM_Application_ReturnID, ID_CLIP);
    DoMethod(field, MUIM_Notify, MUIA_ZkEdit_Acknowledge, MUIV_EveryTime, (IPTR)app, 2, MUIM_Application_ReturnID, ID_ACK);
    DoMethod(edit, MUIM_Notify, MUIA_ZkEdit_Message, MUIV_EveryTime, (IPTR)app, 2, MUIM_Application_ReturnID, ID_MSG);
    DoMethod(edit, MUIM_Notify, MUIA_ZkEdit_Message, MUIV_EveryTime, (IPTR)status, 3, MUIM_Set, MUIA_Text_Contents, MUIV_TriggerValue);
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, (IPTR)app, 2, MUIM_Application_ReturnID,
             MUIV_Application_ReturnID_Quit);
    set(win, MUIA_Window_Open, TRUE);
    {
        IPTR open = 0;
        get(win, MUIA_Window_Open, &open);
        L("OPEN window=%lu", (unsigned long)open);
        if (!open) done = 1;
    }
    set(win, MUIA_Window_ActiveObject, (IPTR)edit);
    while (!done) {
        ULONG id = DoMethod(app, MUIM_Application_NewInput, (IPTR)&sigs);
        size_t n;
        const char *s;
        switch (id) {
        case MUIV_Application_ReturnID_Quit: done = 1; break;
        case ID_FILL:
            L("FILL Insert=%ld", (long)DoMethod(edit, MUIM_ZkEdit_Insert, (IPTR)set_text, (IPTR)(sizeof set_text - 1)));
            set(win, MUIA_Window_ActiveObject, (IPTR)edit);
            break;
        case ID_SAVE: save(); break;
        case ID_REREAD: reread(); break;
        case ID_DUMP:
            s = contents(edit, &n); dump("TEXT", s, n);
            s = contents(code, &n); dump("CODE", s, n);
            s = contents(field, &n); dump("FIELD", s, n);
            break;
        case ID_CLIP: clip(); break;
        case ID_ACK: s = contents(field, &n); dump("ACK", s, n); break;
        case ID_MSG: { IPTR m = 0; get(edit, MUIA_ZkEdit_Message, &m); if (m && *(char *)m) L("MESSAGE %s", (char *)m); break; }
        }
        if (!done && sigs) sigs = Wait(sigs | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C) done = 1;
    }
    MUI_DisposeObject(app);
    zk_edit_exit();
    CloseLibrary(IFFParseBase);
    L("END");
    if (lg) fclose(lg);
    return 0;
}
