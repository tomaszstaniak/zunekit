/* SPDX-License-Identifier: MIT */
/* zk_text on AROS: see zk_text_aros.h. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <graphics/gfx.h>
#include <libraries/codesets.h>
#include <string.h>
#include <stdlib.h>

#define __CYBERGRAPHICS_NOLIBBASE__
#define __CYBERGRAPHICS_LIBBASE zt_cgx_base
static struct Library *zt_cgx_base;
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>

#define __CODESETS_NOLIBBASE__
#define __CODESETS_LIBBASE zt_cs_base
static struct Library *zt_cs_base;
static int zt_cs_tried;
static struct codeset *zt_cs;          /* the system's set, NULL: Latin-1 */
#include <proto/codesets.h>

#include "zk_text_aros.h"

int zt_font_file(struct TextFont *tf, char *out, size_t outn)
{
    char otag[300];
    const char *name = tf && tf->tf_Message.mn_Node.ln_Name ? (const char *)tf->tf_Message.mn_Node.ln_Name : NULL;
    unsigned char *buf;
    size_t len;
    BPTR fh;
    LONG got;
    int ok = 0;
    if (!name || !*name) return 0;
    len = strlen(name);
    if (len > 5 && !strcmp(name + len - 5, ".font")) len -= 5;
    if (len + 12 > sizeof otag) return 0;
    if (strchr(name, ':')) otag[0] = 0;
    else strcpy(otag, "FONTS:");
    strncat(otag, name, len);
    strcat(otag, ".otag");
    if (!(fh = Open((CONST_STRPTR)otag, MODE_OLDFILE))) return 0;
    if ((buf = (unsigned char *)AllocVec(8192, MEMF_ANY))) {
        got = Read(fh, buf, 8192);
        if (got > 0) ok = zt_otag_file(buf, (size_t)got, out, outn);
        FreeVec(buf);
    }
    Close(fh);
    return ok;
}

static const char *const proportional[] = {
    "FONTS:TrueType/OpenSans-Regular.ttf", "FONTS:TrueType/VeraSans.ttf", NULL
};
static const char *const monospace[] = {
    "FONTS:TrueType/VeraMono.ttf", "FONTS:TrueType/OpenSans-Regular.ttf", "FONTS:TrueType/VeraSans.ttf", NULL
};

zt_font *zt_open_for(struct TextFont *tf, int px, CONST_STRPTR const *fallbacks, int fixed)
{
    const char *paths[ZT_MAX_FACES];
    const char *const *def = fixed ? monospace : proportional;
    char own[256];
    int n = 0, i;
    /* a fixed font without its own file starts with the system's monospace */
    if (tf && zt_font_file(tf, own, sizeof own)) paths[n++] = own;
    else if (fixed) paths[n++] = monospace[0];
    for (i = 0; fallbacks && fallbacks[i] && n < ZT_MAX_FACES; i++) paths[n++] = (const char *)fallbacks[i];
    for (i = 0; def[i] && n < ZT_MAX_FACES; i++) paths[n++] = def[i];
    return zt_font_open(paths, n, px > 0 ? px : 13);
}

int zt_fonts_usable(CONST_STRPTR const *fallbacks, char *why, ULONG whylen)
{
    zt_font *z = zt_open_for(NULL, 13, fallbacks, 0);
    if (z) { zt_font_close(z); if (why && whylen) why[0] = 0; return 1; }
    if (why && whylen) {
        const char *m = "no outline font for Unicode text: neither the fallback fonts nor "
                        "FONTS:TrueType/OpenSans-Regular.ttf and VeraSans.ttf opened, "
                        "or freetype2.library is missing";
        ULONG k = 0;
        while (m[k] && k + 1 < whylen) { why[k] = m[k]; k++; }
        why[k] = 0;
    }
    return 0;
}

void zt_blit(struct RastPort *rp, struct ColorMap *cm, ULONG pen, const UBYTE *cov, LONG bw, LONG bh,
             LONG c0, LONG c1, LONG x, LONG y)
{
    ULONG rgb[3];
    LONG i, j;
    if (c0 < 0) c0 = 0;
    if (c1 > bw) c1 = bw;
    if (c1 <= c0 || bh <= 0 || !cm) return;
    GetRGB32(cm, pen, 1, rgb);
    if (!zt_cgx_base) zt_cgx_base = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 0);
    if (zt_cgx_base && GetBitMapAttr(rp->BitMap, BMA_DEPTH) > 8) {
        LONG pw = c1 - c0;
        UBYTE *px = (UBYTE *)AllocVec((ULONG)(pw * bh * 3), MEMF_ANY);
        ULONG pr = rgb[0] >> 24, pg = rgb[1] >> 24, pb = rgb[2] >> 24;
        if (!px) return;
        ReadPixelArray(px, 0, 0, pw * 3, rp, x + c0, y, pw, bh, RECTFMT_RGB);
        for (j = 0; j < bh; j++)
            for (i = 0; i < pw; i++) {
                ULONG a = cov[j * bw + c0 + i];
                UBYTE *p = px + (j * pw + i) * 3;
                if (!a) continue;
                p[0] = (UBYTE)((p[0] * (255 - a) + pr * a) / 255);
                p[1] = (UBYTE)((p[1] * (255 - a) + pg * a) / 255);
                p[2] = (UBYTE)((p[2] * (255 - a) + pb * a) / 255);
            }
        WritePixelArray(px, 0, 0, pw * 3, rp, x + c0, y, pw, bh, RECTFMT_RGB);
        FreeVec(px);
    } else {
        SetAPen(rp, pen);
        for (j = 0; j < bh; j++)
            for (i = c0; i < c1; i++)
                if (cov[j * bw + i] >= 128) WritePixel(rp, x + i, y + j);
    }
}

/* The system's 8-bit set: the one ENV:CHARSET names (set by the Locale
   preferences; diskfont maps outline fonts by it too), else codesets'
   own default. Read once: codesets' default alone is fixed when the
   library loads, so a CHARSET set later was not seen. */
static int codesets(void)
{
    if (!zt_cs_tried) {
        char name[64];
        zt_cs_tried = 1;
        zt_cs_base = OpenLibrary((CONST_STRPTR)"codesets.library", 6);
        if (zt_cs_base) {
            if (GetVar((CONST_STRPTR)"CHARSET", (STRPTR)name, sizeof name, GVF_GLOBAL_ONLY) > 0)
                zt_cs = CodesetsFind((CONST_STRPTR)name, CSA_FallbackToDefault, FALSE, TAG_DONE);
            if (!zt_cs) zt_cs = CodesetsFind(NULL, CSA_FallbackToDefault, TRUE, TAG_DONE);
        }
    }
    return zt_cs_base != NULL && zt_cs != NULL;
}

void zt_aros_close(void)
{
    if (zt_cs_base) CloseLibrary(zt_cs_base);
    if (zt_cgx_base) CloseLibrary(zt_cgx_base);
    zt_cs_base = zt_cgx_base = NULL;
    zt_cs = NULL;
    zt_cs_tried = 0;
}

/* Latin-1 as UTF-8. */
static size_t latin1(UBYTE ch, char *u)
{
    if (ch < 0x80) { u[0] = (char)ch; return 1; }
    u[0] = (char)(0xc0 | (ch >> 6));
    u[1] = (char)(0x80 | (ch & 0x3f));
    return 2;
}

size_t zt_from_local(UBYTE ch, char *utf8)
{
    size_t n = 0;
    if (ch < 0x80 || !codesets()) return latin1(ch, utf8);
    {
        char src[2];
        UTF8 *u;
        src[0] = (char)ch; src[1] = 0;
        u = CodesetsUTF8Create(CSA_SourceCodeset, (IPTR)zt_cs, CSA_Source, (IPTR)src, CSA_SourceLen, 1, TAG_DONE);
        if (u) {
            n = strlen((const char *)u);
            if (n > 4 || !n) n = 0;
            else memcpy(utf8, u, n);
            CodesetsFreeA(u, NULL);
        }
    }
    return n ? n : latin1(ch, utf8);
}

UBYTE zt_to_local(unsigned long cp)
{
    char u[5];
    size_t n;
    if (cp < 0x80) return (UBYTE)cp;
    if (cp > 0x10ffff) return '?';
    if (cp < 0x800) { u[0] = (char)(0xc0 | (cp >> 6)); u[1] = (char)(0x80 | (cp & 0x3f)); n = 2; }
    else if (cp < 0x10000) { u[0] = (char)(0xe0 | (cp >> 12)); u[1] = (char)(0x80 | ((cp >> 6) & 0x3f)); u[2] = (char)(0x80 | (cp & 0x3f)); n = 3; }
    else { u[0] = (char)(0xf0 | (cp >> 18)); u[1] = (char)(0x80 | ((cp >> 12) & 0x3f)); u[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); u[3] = (char)(0x80 | (cp & 0x3f)); n = 4; }
    u[n] = 0;
    if (codesets()) {
        STRPTR s = CodesetsUTF8ToStr(CSA_DestCodeset, (IPTR)zt_cs, CSA_Source, (IPTR)u, CSA_SourceLen, n, TAG_DONE);
        UBYTE r = '?';
        if (s) {
            if (s[0] && !s[1]) r = (UBYTE)s[0];
            CodesetsFreeA(s, NULL);
        }
        return r;
    }
    return cp < 256 ? (UBYTE)cp : '?';
}

char *zt_local_copy(const char *s, size_t n, size_t *outn)
{
    size_t cn, i = 0, k = 0;
    char *nfc = zt_compose(s, n, &cn), *out;
    const char *c = nfc ? nfc : s;
    if (!nfc) cn = n;
    if (!(out = (char *)malloc(cn + 1))) { free(nfc); return NULL; }
    while (i < cn) {
        size_t len;
        unsigned long cp = zt_decode(c, cn, i, &len);
        if (cp != 0x200d && !(cp >= 0xfe00 && cp <= 0xfe0f)) out[k++] = (char)zt_to_local(cp);
        i += len;
    }
    out[k] = 0;
    free(nfc);
    *outn = k;
    return out;
}
