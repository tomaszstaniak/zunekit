/* SPDX-License-Identifier: MIT */
/* zk_edit: see zk_edit.h. The class on top of zk_edit_core (document,
 * history, layout) and zk_text (shaping, drawing). */
#define MUIMASTER_YES_INLINE_STDARG
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <devices/inputevent.h>
#include <devices/rawkeycodes.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/keymap.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include <string.h>
#include <stdlib.h>

#define __IFFPARSE_NOLIBBASE__
#define __IFFPARSE_LIBBASE ze_iff_base
static struct Library *ze_iff_base;
#include <proto/iffparse.h>

#define __CYBERGRAPHICS_NOLIBBASE__
#define __CYBERGRAPHICS_LIBBASE ze_cgx_base
static struct Library *ze_cgx_base;
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>

#include "zk_edit.h"
#include "zk_edit_core.h"
#include "zk_text_aros.h"

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif

#define PADX 4
#define PADY 2
#define CLIP_LIMIT (4UL * 1024 * 1024)

struct ZkEditData
{
    ze_doc *doc;
    ze_view view;
    zt_font *font;
    struct TextFont *fixed_tf;
    Object *fixed_obj;
    CONST_STRPTR *fallbacks;
    BOOL fixed, readonly, single, setup, active, dragging;
    LONG lines;
    ULONG maxbytes;
    unsigned long laid_rev;
    LONG laid_w;
    LONG top, hscroll, total, pub_top;
    long goal;                    /* Up/Down column, -1 none */
    ULONG ack;
    struct MUI_EventHandlerNode ehn;
    char message[120];
};

static struct MUI_CustomClass *ZkEditClass;

/* ------------------------------------------------------------- helpers */

static void say(Object *obj, struct ZkEditData *d, const char *m)
{
    strncpy(d->message, m, sizeof d->message - 1);
    d->message[sizeof d->message - 1] = 0;
    set(obj, MUIA_ZkEdit_Message, (IPTR)d->message);
}

static int relayout(struct ZkEditData *d, Object *obj)
{
    LONG w;
    if (!d->font || !d->setup) return 0;
    w = d->single ? 0x3fffffff : _mwidth(obj) - 2 * PADX;
    if (w < 8) w = 8;
    if (d->laid_rev == ze_revision(d->doc) && d->laid_w == w && d->view.nlines) return 1;
    if (!ze_layout(&d->view, d->doc, d->font, w)) return 0;   /* the old lines stay */
    d->laid_rev = ze_revision(d->doc);
    d->laid_w = w;
    return 1;
}

/* Keeps the caret in sight; publishes the geometry for scrollers. */
static void reveal(struct ZkEditData *d, Object *obj)
{
    size_t line;
    long x, y, h = _mheight(obj) - 2 * PADY, w = _mwidth(obj) - 2 * PADX;
    if (!d->setup || !d->font || !relayout(d, obj)) return;
    x = ze_caret_x(&d->view, d->doc, d->font, ze_caret(d->doc), &line);
    y = d->view.nlines ? d->view.line[line].y : 0;
    if (y < d->top) d->top = y;
    if (y + d->view.lineh > d->top + h) d->top = y + d->view.lineh - h;
    if (d->top > d->view.height - h) d->top = d->view.height - h;
    if (d->top < 0) d->top = 0;
    if (d->single) {
        if (x < d->hscroll) d->hscroll = x;
        if (x > d->hscroll + w - 2) d->hscroll = x - w + 2;
        if (d->hscroll < 0) d->hscroll = 0;
    }
    if (d->view.height != d->total) { d->total = d->view.height; SetAttrs(obj, MUIA_ZkEdit_Total, d->total, TAG_DONE); }
    if (d->top != d->pub_top) { d->pub_top = d->top; SetAttrs(obj, MUIA_ZkEdit_Top, d->top, TAG_DONE); }
}

/* After an edit (text 1) or a caret move (0). */
static void changed(Object *obj, struct ZkEditData *d, BOOL text)
{
    if (d->setup) {
        reveal(d, obj);
        MUI_Redraw(obj, MADF_DRAWUPDATE);
    }
    if (text) set(obj, MUIA_ZkEdit_Revision, (IPTR)ze_revision(d->doc));
}

/* The result of a change as words; 1 when it changed the text. */
static int result(Object *obj, struct ZkEditData *d, int r)
{
    if (r == ZE_NOMEM) { say(obj, d, "Not enough memory: the change was not made; the text is as it was"); DisplayBeep(NULL); return 0; }
    if (r == ZE_INVALID) { say(obj, d, "Refused: not valid UTF-8 text"); return 0; }
    if (r == 1 && d->message[0]) say(obj, d, "");
    return r == 1;
}

/* A field takes no line feeds: they become spaces. 0 when out of memory. */
static int insert_text(Object *obj, struct ZkEditData *d, const char *s, size_t n)
{
    int r;
    if (d->readonly) return 0;
    if (d->maxbytes) {
        size_t len, a, b, room;
        ze_text(d->doc, &len);
        ze_selection(d->doc, &a, &b);
        room = len - (b - a) >= d->maxbytes ? 0 : d->maxbytes - (len - (b - a));
        if (n > room) {
            /* what fits, to a cluster boundary (a lone accent or half an
               emoji sequence is not put in) */
            n = zt_valid(s, n) ? zt_cluster_start(s, n, room) : 0;
            if (!n) { say(obj, d, "The field is full"); DisplayBeep(NULL); return 0; }
        }
    }
    if (d->single && (memchr(s, '\n', n) || memchr(s, '\r', n))) {
        char *c = (char *)malloc(n ? n : 1);
        size_t i, k = 0;
        if (!c) return result(obj, d, ZE_NOMEM);
        for (i = 0; i < n; i++) {
            if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n') continue;
            c[k++] = s[i] == '\n' || s[i] == '\r' ? ' ' : s[i];
        }
        r = ze_insert(d->doc, c, k);
        free(c);
    } else r = ze_insert(d->doc, s, n);
    d->goal = -1;
    if (result(obj, d, r)) { changed(obj, d, TRUE); return 1; }
    return 0;
}

/* ----------------------------------------------------------- clipboard */

static int iff_open(void)
{
    if (!ze_iff_base) ze_iff_base = OpenLibrary((CONST_STRPTR)"iffparse.library", 39);
    return ze_iff_base != NULL;
}

/* FTXT with a UTF8 chunk and a CHRS copy in the system's set. */
static int clip_write(const char *s, size_t n)
{
    struct IFFHandle *iff;
    char *local;
    size_t k;
    int ok = 0;
    if (!iff_open()) return 0;
    /* the UTF8 chunk keeps the bytes as they are; the CHRS copy is
       zk_text_aros's (composed, in the system's set) */
    if (!(local = zt_local_copy(s, n, &k))) return 0;
    if ((iff = AllocIFF())) {
        if ((iff->iff_Stream = (IPTR)OpenClipboard(0))) {
            InitIFFasClip(iff);
            if (!OpenIFF(iff, IFFF_WRITE)) {
                ok = !PushChunk(iff, MAKE_ID('F','T','X','T'), MAKE_ID('F','O','R','M'), IFFSIZE_UNKNOWN)
                     && !PushChunk(iff, 0, MAKE_ID('C','H','R','S'), (LONG)k)
                     && WriteChunkBytes(iff, local, (LONG)k) == (LONG)k && !PopChunk(iff)
                     && !PushChunk(iff, 0, MAKE_ID('U','T','F','8'), (LONG)n)
                     && WriteChunkBytes(iff, (APTR)s, (LONG)n) == (LONG)n && !PopChunk(iff)
                     && !PopChunk(iff);
                CloseIFF(iff);
            }
            CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
        }
        FreeIFF(iff);
    }
    free(local);
    return ok;
}

/* The clipboard's text as UTF-8 into a new buffer (*n bytes): its UTF8
   chunk, else its CHRS converted from the system's set. NULL when there
   is none or no memory (*err says which). */
static char *clip_read(size_t *n, const char **err)
{
    struct IFFHandle *iff;
    char *out = NULL;
    *n = 0;
    *err = "the clipboard holds no text";
    if (!iff_open()) { *err = "no iffparse.library"; return NULL; }
    if (!(iff = AllocIFF())) { *err = "not enough memory"; return NULL; }
    if ((iff->iff_Stream = (IPTR)OpenClipboard(0))) {
        InitIFFasClip(iff);
        if (!OpenIFF(iff, IFFF_READ)) {
            static const LONG stops[] = { MAKE_ID('F','T','X','T'), MAKE_ID('U','T','F','8'),
                                          MAKE_ID('F','T','X','T'), MAKE_ID('C','H','R','S') };
            if (!StopChunks(iff, (LONG *)stops, 2)) {
                char *utf = NULL, *chrs = NULL;
                LONG nu = 0, nc = 0;
                while (!ParseIFF(iff, IFFPARSE_SCAN)) {
                    struct ContextNode *cn = CurrentChunk(iff);
                    LONG size = cn ? cn->cn_Size : 0;
                    char **dst = cn && cn->cn_ID == MAKE_ID('U','T','F','8') ? &utf : &chrs;
                    LONG *dn = dst == &utf ? &nu : &nc;
                    if (!cn || size < 0 || (ULONG)size > CLIP_LIMIT || *dst) continue;
                    if (!(*dst = (char *)malloc((size_t)size + 1))) { *err = "not enough memory"; break; }
                    *dn = ReadChunkBytes(iff, *dst, size);
                    if (*dn < 0) *dn = 0;
                }
                if (utf && zt_valid(utf, (size_t)nu)) { out = utf; *n = (size_t)nu; utf = NULL; }
                else if (chrs) {
                    /* every byte of the set up to four bytes of UTF-8 */
                    if ((out = (char *)malloc((size_t)nc * 4 + 1))) {
                        LONG i;
                        for (i = 0; i < nc; i++) *n += zt_from_local((UBYTE)chrs[i], out + *n);
                    } else *err = "not enough memory";
                }
                free(utf);
                free(chrs);
            }
            CloseIFF(iff);
        }
        CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
    } else *err = "the clipboard cannot be opened";
    FreeIFF(iff);
    return out;
}

static IPTR do_copy(Object *obj, struct ZkEditData *d, BOOL cut)
{
    size_t n;
    const char *s = ze_selected(d->doc, &n);
    if (!n) return 0;
    if (!clip_write(s, n)) { say(obj, d, "Copy failed: the clipboard could not be written"); return 0; }
    if (cut && !d->readonly && result(obj, d, ze_backspace(d->doc))) changed(obj, d, TRUE);
    return 1;
}

static IPTR do_paste(Object *obj, struct ZkEditData *d)
{
    size_t n;
    const char *err;
    char *t = clip_read(&n, &err);
    int r;
    if (!t) { say(obj, d, err); return 0; }
    r = insert_text(obj, d, t, n);
    free(t);
    return (IPTR)r;
}

/* ----------------------------------------------------------- input */

static IPTR handle_key(Object *obj, struct ZkEditData *d, struct IntuiMessage *im)
{
    UWORD qual = im->Qualifier, code = im->Code;
    BOOL shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
    BOOL ctrl = (qual & IEQUALIFIER_CONTROL) != 0;
    BOOL amiga = (qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) != 0;
    BOOL alt = (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
    struct InputEvent ie;
    UBYTE mapped[16];
    LONG n, page;
    size_t len;

    if (code & IECODE_UP_PREFIX) return 0;
    if (code == RAWKEY_TAB || code == RAWKEY_ESCAPE) return 0;
    relayout(d, obj);
    page = d->view.lineh ? (_mheight(obj) - 2 * PADY) / d->view.lineh : 1;
    if (page < 1) page = 1;
    switch (code) {
    case RAWKEY_LEFT: case RAWKEY_RIGHT:
        if (ctrl || alt) ze_word_step(d->doc, code == RAWKEY_RIGHT, shift);
        else ze_step(d->doc, code == RAWKEY_RIGHT, shift);
        d->goal = -1;
        changed(obj, d, FALSE);
        return MUI_EventHandlerRC_Eat;
    case RAWKEY_UP: case RAWKEY_DOWN: case RAWKEY_PAGEUP: case RAWKEY_PAGEDOWN:
        if (ctrl && (code == RAWKEY_UP || code == RAWKEY_DOWN)) {
            ze_text(d->doc, &len);
            ze_set_caret(d->doc, code == RAWKEY_UP ? 0 : len, shift);
            d->goal = -1;
        } else {
            long lines = code == RAWKEY_UP ? -1 : code == RAWKEY_DOWN ? 1 : code == RAWKEY_PAGEUP ? -page : page;
            size_t to = ze_vertical(&d->view, d->doc, d->font, ze_caret(d->doc), lines, &d->goal);
            ze_set_caret(d->doc, to, shift);
        }
        changed(obj, d, FALSE);
        return MUI_EventHandlerRC_Eat;
    case RAWKEY_HOME: case RAWKEY_END:
        ze_text(d->doc, &len);
        if (ctrl) ze_set_caret(d->doc, code == RAWKEY_HOME ? 0 : len, shift);
        else ze_set_caret(d->doc, code == RAWKEY_HOME ? ze_line_home(&d->view, ze_caret(d->doc))
                                                       : ze_line_end(&d->view, ze_caret(d->doc)), shift);
        d->goal = -1;
        changed(obj, d, FALSE);
        return MUI_EventHandlerRC_Eat;
    case RAWKEY_BACKSPACE: case RAWKEY_DELETE:
        if (!d->readonly && result(obj, d, code == RAWKEY_BACKSPACE ? ze_backspace(d->doc) : ze_delete(d->doc))) {
            d->goal = -1;
            changed(obj, d, TRUE);
        }
        return MUI_EventHandlerRC_Eat;
    case RAWKEY_RETURN: case RAWKEY_KP_ENTER:
        if (d->single) { d->ack++; set(obj, MUIA_ZkEdit_Acknowledge, (IPTR)d->ack); }
        else insert_text(obj, d, "\n", 1);
        return MUI_EventHandlerRC_Eat;
    }

    memset(&ie, 0, sizeof ie);
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = code;
    ie.ie_Qualifier = qual;
    /* Zune hands handlers a copy whose IAddress points at the dead-key
       state, not at the original event (as MkEditor found). */
    if (im->IAddress) ie.ie_EventAddress = (APTR)(IPTR)*(ULONG *)im->IAddress;
    if (ctrl || amiga) {
        ie.ie_Qualifier &= ~(IEQUALIFIER_CONTROL | IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND |
                             IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT | IEQUALIFIER_LALT | IEQUALIFIER_RALT);
        if (MapRawKey(&ie, (STRPTR)mapped, sizeof mapped, NULL) != 1) return 0;
        switch (mapped[0] | 0x20) {
        case 'a': DoMethod(obj, MUIM_ZkEdit_SelectAll); break;
        case 'c': DoMethod(obj, MUIM_ZkEdit_Copy); break;
        case 'x': DoMethod(obj, MUIM_ZkEdit_Cut); break;
        case 'v': DoMethod(obj, MUIM_ZkEdit_Paste); break;
        case 'z': DoMethod(obj, shift ? MUIM_ZkEdit_Redo : MUIM_ZkEdit_Undo); break;
        case 'y': DoMethod(obj, MUIM_ZkEdit_Redo); break;
        default: return 0;                         /* menu shortcuts stay the window's */
        }
        return MUI_EventHandlerRC_Eat;
    }
    n = MapRawKey(&ie, (STRPTR)mapped, sizeof mapped, NULL);
    if (n > 0) {
        char utf8[64];
        size_t k = 0;
        LONG i;
        /* the keymap gives the system's character set: each byte to UTF-8 */
        for (i = 0; i < n && k + 4 < sizeof utf8; i++)
            if (mapped[i] >= 0x20 && mapped[i] != 0x7f && !(mapped[i] >= 0x80 && mapped[i] < 0xa0))
                k += zt_from_local(mapped[i], utf8 + k);
        if (k) insert_text(obj, d, utf8, k);
    }
    return MUI_EventHandlerRC_Eat;
}

static void point(Object *obj, struct ZkEditData *d, LONG mx, LONG my, BOOL extend)
{
    long x = mx - _mleft(obj) - PADX + (d->single ? d->hscroll : 0), y = my - _mtop(obj) - PADY + d->top;
    size_t off;
    if (!relayout(d, obj)) return;
    if (y < 0) y = 0;
    off = ze_hit(&d->view, d->doc, d->font, x, y);
    ze_set_caret(d->doc, off, extend);
    d->goal = -1;
}

static IPTR ZkEdit_HandleEvent(struct IClass *cl, Object *obj, struct MUIP_HandleEvent *msg)
{
    struct ZkEditData *d = INST_DATA(cl, obj);
    struct IntuiMessage *im = msg->imsg;
    BOOL in;
    if (!im || !d->font) return 0;
    in = im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) && im->MouseY <= _mbottom(obj);
    switch (im->Class) {
    case IDCMP_MOUSEBUTTONS:
        if (im->Code == SELECTDOWN && in) {
            if (!d->active) set(_win(obj), MUIA_Window_ActiveObject, (IPTR)obj);
            point(obj, d, im->MouseX, im->MouseY, (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0);
            d->dragging = TRUE;
            changed(obj, d, FALSE);
            return MUI_EventHandlerRC_Eat;
        }
        if (im->Code == SELECTUP && d->dragging) { d->dragging = FALSE; return MUI_EventHandlerRC_Eat; }
        break;
    case IDCMP_MOUSEMOVE:
        if (d->dragging) {
            if (im->MouseY < _mtop(obj)) d->top -= d->view.lineh;
            else if (im->MouseY > _mbottom(obj)) d->top += d->view.lineh;
            point(obj, d, im->MouseX, im->MouseY < _mtop(obj) ? _mtop(obj) : im->MouseY > _mbottom(obj) ? _mbottom(obj) : im->MouseY, TRUE);
            changed(obj, d, FALSE);
            return MUI_EventHandlerRC_Eat;
        }
        break;
    case IDCMP_RAWKEY:
        if (d->active) return handle_key(obj, d, im);
        break;
    }
    return 0;
}

/* ----------------------------------------------------------- drawing */

static void rgb_of(Object *obj, ULONG pen, UBYTE *c)
{
    ULONG v[3];
    GetRGB32(_screen(obj)->ViewPort.ColorMap, pen, 1, v);
    c[0] = (UBYTE)(v[0] >> 24); c[1] = (UBYTE)(v[1] >> 24); c[2] = (UBYTE)(v[2] >> 24);
}

static void fill(UBYTE *px, LONG w, LONG h, LONG x0, LONG y0, LONG x1, LONG y1, const UBYTE *c)
{
    LONG x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) { UBYTE *p = px + (y * w + x) * 3; p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; }
}

/* Blends a line's coverage (w wide, lh high) at row y; columns [s0, s1)
   in the selected colour. */
static void blend(UBYTE *px, LONG w, LONG h, const UBYTE *cov, LONG lh, LONG y, LONG s0, LONG s1,
                  const UBYTE *ink, const UBYTE *sel)
{
    LONG r, x;
    for (r = 0; r < lh; r++) {
        if (y + r < 0 || y + r >= h) continue;
        for (x = 0; x < w; x++) {
            ULONG a = cov[r * w + x];
            const UBYTE *c = x >= s0 && x < s1 ? sel : ink;
            UBYTE *p = px + ((y + r) * w + x) * 3;
            if (!a) continue;
            p[0] = (UBYTE)((p[0] * (255 - a) + c[0] * a) / 255);
            p[1] = (UBYTE)((p[1] * (255 - a) + c[1] * a) / 255);
            p[2] = (UBYTE)((p[2] * (255 - a) + c[2] * a) / 255);
        }
    }
}

static IPTR ZkEdit_Draw(struct IClass *cl, Object *obj, struct MUIP_Draw *msg)
{
    struct ZkEditData *d = INST_DATA(cl, obj);
    LONG w = _mwidth(obj), h = _mheight(obj), lh, i, dx;
    size_t sa, sb, tn;
    const char *t;
    UBYTE *px, *cov, ink[3], selbg[3], selink[3];
    BOOL rgb;
    DoSuperMethodA(cl, obj, (Msg)msg);
    if (!(msg->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) || w <= 0 || h <= 0 || !d->font) return 0;
    if (!relayout(d, obj)) return 0;
    if (msg->flags & MADF_DRAWOBJECT) reveal(d, obj);
    lh = d->view.lineh;
    t = ze_text(d->doc, &tn);
    ze_selection(d->doc, &sa, &sb);
    rgb_of(obj, _pens(obj)[MPEN_TEXT], ink);
    rgb_of(obj, _pens(obj)[MPEN_FILL], selbg);
    rgb_of(obj, _pens(obj)[MPEN_SHINE], selink);
    if (!ze_cgx_base) ze_cgx_base = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 0);
    rgb = ze_cgx_base && GetBitMapAttr(_rp(obj)->BitMap, BMA_DEPTH) > 8;
    dx = PADX - (d->single ? d->hscroll : 0);
    /* an update draws over what is there: the background first, so no old
       ink, caret or scrolled line stays */
    if (msg->flags & MADF_DRAWUPDATE)
        DoMethod(obj, MUIM_DrawBackground, _mleft(obj), _mtop(obj), w, h, _mleft(obj), _mtop(obj), 0);
    if (!(cov = (UBYTE *)AllocVec((ULONG)(w * lh), MEMF_ANY))) return 0;
    px = rgb ? (UBYTE *)AllocVec((ULONG)(w * h * 3), MEMF_ANY) : NULL;
    if (rgb && !px) { FreeVec(cov); return 0; }
    if (px) ReadPixelArray(px, 0, 0, w * 3, _rp(obj), _mleft(obj), _mtop(obj), w, h, RECTFMT_RGB);
    for (i = 0; i < (LONG)d->view.nlines; i++) {
        const ze_line *l = &d->view.line[i];
        LONG y = l->y - d->top + PADY, s0 = 0, s1 = 0;
        if (y + lh <= 0) continue;
        if (y >= h) break;
        /* the selection's part of this line, a little past its end when it
           takes the line feed too */
        if (sa < sb && sb > l->start && sa <= l->start + l->len) {
            size_t a = sa > l->start ? sa : l->start, b = sb < l->start + l->len ? sb : l->start + l->len;
            s0 = dx + ze_caret_x(&d->view, d->doc, d->font, a, NULL);
            s1 = dx + ze_caret_x(&d->view, d->doc, d->font, b, NULL);
            if (sb > l->start + l->len) s1 += lh / 3;
            if (px) fill(px, w, h, s0, y, s1, y + lh, selbg);
            else { SetAPen(_rp(obj), _pens(obj)[MPEN_FILL]); RectFill(_rp(obj), _mleft(obj) + s0, _mtop(obj) + y, _mleft(obj) + s1 - 1, _mtop(obj) + y + lh - 1); }
        }
        memset(cov, 0, (size_t)(w * lh));
        zt_draw(d->font, t + l->start, l->len, 0, cov, w, lh, w, dx, zt_font_ascent(d->font));
        if (px) blend(px, w, h, cov, lh, y, s0, s1, ink, selink);
        else {
            zt_blit(_rp(obj), _screen(obj)->ViewPort.ColorMap, _pens(obj)[MPEN_TEXT], cov, w, lh, 0, w, _mleft(obj), _mtop(obj) + y);
        }
    }
    /* the caret, while active and nothing is selected */
    if (d->active && sa == sb) {
        size_t line;
        LONG cx = dx + ze_caret_x(&d->view, d->doc, d->font, ze_caret(d->doc), &line);
        LONG cy = (d->view.nlines ? d->view.line[line].y : 0) - d->top + PADY;
        if (px) fill(px, w, h, cx, cy, cx + 1, cy + lh, ink);
        else { SetAPen(_rp(obj), _pens(obj)[MPEN_TEXT]); RectFill(_rp(obj), _mleft(obj) + cx, _mtop(obj) + cy, _mleft(obj) + cx, _mtop(obj) + cy + lh - 1); }
    }
    if (px) {
        WritePixelArray(px, 0, 0, w * 3, _rp(obj), _mleft(obj), _mtop(obj), w, h, RECTFMT_RGB);
        FreeVec(px);
    }
    FreeVec(cov);
    return 0;
}

/* ----------------------------------------------------------- the class */

static struct TextFont *fixed_font(struct ZkEditData *d, Object *obj)
{
    Object *o = MUI_NewObject((CONST_STRPTR)MUIC_Rectangle, MUIA_Font, MUIV_Font_Fixed, TAG_DONE);
    if (!o) return NULL;
    DoMethod(o, MUIM_ConnectParent, (IPTR)obj);
    if (DoMethod(o, MUIM_Setup, (IPTR)muiRenderInfo(obj)) && _font(o)) { d->fixed_obj = o; return _font(o); }
    DoMethod(o, MUIM_DisconnectParent);
    MUI_DisposeObject(o);
    return NULL;
}

static void fixed_font_cleanup(struct ZkEditData *d)
{
    if (!d->fixed_obj) return;
    DoMethod(d->fixed_obj, MUIM_Cleanup);
    DoMethod(d->fixed_obj, MUIM_DisconnectParent);
    MUI_DisposeObject(d->fixed_obj);
    d->fixed_obj = NULL;
    d->fixed_tf = NULL;
}

static IPTR ZkEdit_New(struct IClass *cl, Object *obj, struct opSet *msg)
{
    struct ZkEditData *d;
    ze_doc *doc = ze_new();
    STRPTR text;
    if (!doc) return 0;
    obj = (Object *)DoSuperNewTags(cl, obj, NULL, MUIA_CycleChain, 1, MUIA_Frame, MUIV_Frame_String,
                                   MUIA_Background, MUII_TextBack, TAG_MORE, (IPTR)msg->ops_AttrList);
    if (!obj) { ze_free(doc); return 0; }
    d = INST_DATA(cl, obj);
    memset(d, 0, sizeof *d);
    d->doc = doc;
    ze_view_init(&d->view);
    d->goal = -1;
    d->laid_w = -1;
    d->fixed = GetTagData(MUIA_ZkEdit_Fixed, FALSE, msg->ops_AttrList) != 0;
    d->readonly = GetTagData(MUIA_ZkEdit_ReadOnly, FALSE, msg->ops_AttrList) != 0;
    d->single = GetTagData(MUIA_ZkEdit_SingleLine, FALSE, msg->ops_AttrList) != 0;
    d->fallbacks = (CONST_STRPTR *)GetTagData(MUIA_ZkEdit_FallbackFonts, 0, msg->ops_AttrList);
    d->lines = (LONG)GetTagData(MUIA_ZkEdit_Lines, d->single ? 1 : 5, msg->ops_AttrList);
    d->maxbytes = (ULONG)GetTagData(MUIA_ZkEdit_MaxBytes, 0, msg->ops_AttrList);
    if (d->lines < 1) d->lines = 1;
    if ((text = (STRPTR)GetTagData(MUIA_ZkEdit_Contents, 0, msg->ops_AttrList)))
        ze_set_text(d->doc, (const char *)text, strlen((const char *)text));
    return (IPTR)obj;
}

static IPTR ZkEdit_Dispose(struct IClass *cl, Object *obj, Msg msg)
{
    struct ZkEditData *d = INST_DATA(cl, obj);
    ze_view_free(&d->view);
    ze_free(d->doc);
    return DoSuperMethodA(cl, obj, msg);
}

static IPTR set_text(Object *obj, struct ZkEditData *d, const char *s, size_t n)
{
    int r;
    if (d->maxbytes && n > d->maxbytes) { say(obj, d, "Refused: longer than the field takes"); return 0; }
    r = ze_set_text(d->doc, s, n);
    if (result(obj, d, r)) { d->top = 0; d->hscroll = 0; changed(obj, d, TRUE); }
    return (IPTR)(LONG)r;
}

static IPTR ZkEdit_Set(struct IClass *cl, Object *obj, struct opSet *msg)
{
    struct ZkEditData *d = INST_DATA(cl, obj);
    struct TagItem *tags = msg->ops_AttrList, *tag;
    while ((tag = NextTagItem(&tags))) {
        switch (tag->ti_Tag) {
        case MUIA_ZkEdit_Contents:
            if (tag->ti_Data) set_text(obj, d, (const char *)tag->ti_Data, strlen((const char *)tag->ti_Data));
            else set_text(obj, d, "", 0);
            break;
        case MUIA_ZkEdit_ReadOnly: d->readonly = tag->ti_Data != 0; break;
        case MUIA_ZkEdit_Caret: {
            size_t len;
            ze_text(d->doc, &len);
            ze_set_caret(d->doc, tag->ti_Data > len ? len : (size_t)tag->ti_Data, 0);
            d->goal = -1;
            changed(obj, d, FALSE);
            break;
        }
        case MUIA_ZkEdit_Top:
            if (d->setup && (LONG)tag->ti_Data != d->top) {
                d->top = (LONG)tag->ti_Data;
                if (d->top > d->view.height - (_mheight(obj) - 2 * PADY)) d->top = d->view.height - (_mheight(obj) - 2 * PADY);
                if (d->top < 0) d->top = 0;
                d->pub_top = d->top;
                MUI_Redraw(obj, MADF_DRAWUPDATE);
            }
            break;
        }
    }
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

static IPTR ZkEdit_Get(struct IClass *cl, Object *obj, struct opGet *msg)
{
    struct ZkEditData *d = INST_DATA(cl, obj);
    IPTR *store = msg->opg_Storage;
    switch (msg->opg_AttrID) {
    case MUIA_ZkEdit_Contents: *store = (IPTR)ze_text(d->doc, NULL); return TRUE;
    case MUIA_ZkEdit_Revision: *store = (IPTR)ze_revision(d->doc); return TRUE;
    case MUIA_ZkEdit_ReadOnly: *store = d->readonly; return TRUE;
    case MUIA_ZkEdit_Acknowledge: *store = d->ack; return TRUE;
    case MUIA_ZkEdit_Top: *store = (IPTR)d->top; return TRUE;
    case MUIA_ZkEdit_Total: *store = (IPTR)d->total; return TRUE;
    case MUIA_ZkEdit_Visible: *store = d->setup ? (IPTR)(_mheight(obj) - 2 * PADY) : 0; return TRUE;
    case MUIA_ZkEdit_Message: *store = (IPTR)d->message; return TRUE;
    case MUIA_ZkEdit_Caret: *store = (IPTR)ze_caret(d->doc); return TRUE;
    }
    return DoSuperMethodA(cl, obj, (Msg)msg);
}

static IPTR ZkEdit_AskMinMax(struct IClass *cl, Object *obj, struct MUIP_AskMinMax *msg)
{
    struct ZkEditData *d = INST_DATA(cl, obj);
    LONG lh = d->font ? zt_font_height(d->font) : 16;
    DoSuperMethodA(cl, obj, (Msg)msg);
    msg->MinMaxInfo->MinWidth += 60;
    msg->MinMaxInfo->DefWidth += 300;
    msg->MinMaxInfo->MaxWidth += MUI_MAXMAX;
    msg->MinMaxInfo->MinHeight += lh + 2 * PADY;
    msg->MinMaxInfo->DefHeight += lh * d->lines + 2 * PADY;
    msg->MinMaxInfo->MaxHeight += d->single ? lh + 2 * PADY : MUI_MAXMAX;
    return 0;
}

static IPTR ZkEdit_Setup(struct IClass *cl, Object *obj, Msg msg)
{
    struct ZkEditData *d = INST_DATA(cl, obj);
    struct TextFont *tf;
    if (!DoSuperMethodA(cl, obj, msg)) return FALSE;
    tf = _font(obj);
    if (d->fixed) d->fixed_tf = fixed_font(d, obj);
    d->font = d->fixed ? zt_open_for(d->fixed_tf, d->fixed_tf ? d->fixed_tf->tf_YSize : (tf ? tf->tf_YSize : 13), d->fallbacks, 1)
                       : zt_open_for(tf, tf ? tf->tf_YSize : 13, d->fallbacks, 0);
    if (!d->font) {
        /* nothing to draw Unicode text with: refuse, rather than an empty
           field that looks right (zk_edit_fonts says why first) */
        fixed_font_cleanup(d);
        DoSuperMethod(cl, obj, MUIM_Cleanup);
        return FALSE;
    }
    d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY;
    d->ehn.ehn_Priority = 0;
    d->ehn.ehn_Flags = 0;
    d->ehn.ehn_Object = obj;
    d->ehn.ehn_Class = cl;
    DoMethod(_win(obj), MUIM_Window_AddEventHandler, (IPTR)&d->ehn);
    d->setup = TRUE;
    d->laid_w = -1;
    return TRUE;
}

static IPTR ZkEdit_Cleanup(struct IClass *cl, Object *obj, Msg msg)
{
    struct ZkEditData *d = INST_DATA(cl, obj);
    DoMethod(_win(obj), MUIM_Window_RemEventHandler, (IPTR)&d->ehn);
    d->dragging = FALSE;
    d->setup = FALSE;
    zt_font_close(d->font);
    d->font = NULL;
    fixed_font_cleanup(d);
    return DoSuperMethodA(cl, obj, msg);
}

BOOPSI_DISPATCHER(IPTR, ZkEditDispatcher, cl, obj, msg)
{
    struct ZkEditData *d;
    switch (msg->MethodID) {
    case OM_NEW: return ZkEdit_New(cl, obj, (struct opSet *)msg);
    case OM_DISPOSE: return ZkEdit_Dispose(cl, obj, msg);
    case OM_SET: return ZkEdit_Set(cl, obj, (struct opSet *)msg);
    case OM_GET: return ZkEdit_Get(cl, obj, (struct opGet *)msg);
    case MUIM_AskMinMax: return ZkEdit_AskMinMax(cl, obj, (struct MUIP_AskMinMax *)msg);
    case MUIM_Draw: return ZkEdit_Draw(cl, obj, (struct MUIP_Draw *)msg);
    case MUIM_HandleEvent: return ZkEdit_HandleEvent(cl, obj, (struct MUIP_HandleEvent *)msg);
    case MUIM_Setup: return ZkEdit_Setup(cl, obj, msg);
    case MUIM_Cleanup: return ZkEdit_Cleanup(cl, obj, msg);
    case MUIM_GoActive:
        d = INST_DATA(cl, obj);
        d->active = TRUE;
        MUI_Redraw(obj, MADF_DRAWUPDATE);
        break;
    case MUIM_GoInactive:
        d = INST_DATA(cl, obj);
        d->active = FALSE;
        d->dragging = FALSE;
        MUI_Redraw(obj, MADF_DRAWUPDATE);
        break;
    case MUIM_ZkEdit_SetText: {
        struct MUIP_ZkEdit_SetText *m = (struct MUIP_ZkEdit_SetText *)msg;
        d = INST_DATA(cl, obj);
        return set_text(obj, d, (const char *)m->Text, m->Length == (IPTR)~0UL ? strlen((const char *)m->Text) : (size_t)m->Length);
    }
    case MUIM_ZkEdit_Insert: {
        struct MUIP_ZkEdit_Insert *m = (struct MUIP_ZkEdit_Insert *)msg;
        d = INST_DATA(cl, obj);
        return insert_text(obj, d, (const char *)m->Text, m->Length == (IPTR)~0UL ? strlen((const char *)m->Text) : (size_t)m->Length);
    }
    case MUIM_ZkEdit_Undo:
        d = INST_DATA(cl, obj);
        if (d->readonly || !result(obj, d, ze_undo(d->doc))) return 0;
        d->goal = -1;
        changed(obj, d, TRUE);
        return 1;
    case MUIM_ZkEdit_Redo:
        d = INST_DATA(cl, obj);
        if (d->readonly || !result(obj, d, ze_redo(d->doc))) return 0;
        d->goal = -1;
        changed(obj, d, TRUE);
        return 1;
    case MUIM_ZkEdit_Copy: d = INST_DATA(cl, obj); return do_copy(obj, d, FALSE);
    case MUIM_ZkEdit_Cut: d = INST_DATA(cl, obj); return do_copy(obj, d, !d->readonly);
    case MUIM_ZkEdit_Paste: d = INST_DATA(cl, obj); return d->readonly ? 0 : do_paste(obj, d);
    case MUIM_ZkEdit_SelectAll:
        d = INST_DATA(cl, obj);
        ze_select_all(d->doc);
        changed(obj, d, FALSE);
        return 1;
    }
    return DoSuperMethodA(cl, obj, msg);
}
BOOPSI_DISPATCHER_END

int zk_edit_init(void)
{
    if (ZkEditClass) return 1;
    ZkEditClass = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Area, NULL, sizeof(struct ZkEditData), ZkEditDispatcher);
    return ZkEditClass != NULL;
}

void zk_edit_exit(void)
{
    if (ZkEditClass) { MUI_DeleteCustomClass(ZkEditClass); ZkEditClass = NULL; }
    if (ze_iff_base) { CloseLibrary(ze_iff_base); ze_iff_base = NULL; }
    if (ze_cgx_base) { CloseLibrary(ze_cgx_base); ze_cgx_base = NULL; }
    zt_aros_close();
}

struct MUI_CustomClass *zk_edit_class(void) { return ZkEditClass; }

Object *zk_edit_new(Object **edit, int scroller, const struct TagItem *extra)
{
    Object *e, *bar, *g;
    if (!ZkEditClass) return NULL;
    e = (Object *)NewObjectA(ZkEditClass->mcc_Class, NULL, (struct TagItem *)extra);
    if (!e) return NULL;
    *edit = e;
    if (!scroller) return e;
    bar = MUI_NewObject((CONST_STRPTR)MUIC_Scrollbar, TAG_DONE);
    if (!bar) { MUI_DisposeObject(e); return NULL; }
    g = MUI_NewObject((CONST_STRPTR)MUIC_Group, MUIA_Group_Horiz, TRUE, MUIA_Group_Spacing, 0,
                      MUIA_Group_Child, (IPTR)e, MUIA_Group_Child, (IPTR)bar, TAG_DONE);
    if (!g) { MUI_DisposeObject(e); MUI_DisposeObject(bar); return NULL; }
    /* the scroller and the editor follow each other in pixels */
    DoMethod(e, MUIM_Notify, MUIA_ZkEdit_Total, MUIV_EveryTime, (IPTR)bar, 3, MUIM_NoNotifySet, MUIA_Prop_Entries, MUIV_TriggerValue);
    DoMethod(e, MUIM_Notify, MUIA_ZkEdit_Top, MUIV_EveryTime, (IPTR)bar, 3, MUIM_NoNotifySet, MUIA_Prop_First, MUIV_TriggerValue);
    DoMethod(e, MUIM_Notify, MUIA_ZkEdit_Visible, MUIV_EveryTime, (IPTR)bar, 3, MUIM_NoNotifySet, MUIA_Prop_Visible, MUIV_TriggerValue);
    DoMethod(bar, MUIM_Notify, MUIA_Prop_First, MUIV_EveryTime, (IPTR)e, 3, MUIM_NoNotifySet, MUIA_ZkEdit_Top, MUIV_TriggerValue);
    return g;
}

int zk_edit_fonts(CONST_STRPTR const *fallbacks, char *why, ULONG whylen)
{
    return zt_fonts_usable(fallbacks, why, whylen);
}
