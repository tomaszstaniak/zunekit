/* SPDX-License-Identifier: MIT */
/* zk_text on AROS: faces for a Zune object's font, and coverage drawn
 * into a RastPort. Shared by Linktext's UTF-8 mode and zk_edit, so both
 * pick fonts and draw text the same way. */
#ifndef ZK_TEXT_AROS_H
#define ZK_TEXT_AROS_H

#include <exec/types.h>
#include <graphics/text.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include "zk_text.h"

/* The outline file of an AROS font: the file its .otag in FONTS: (or where
   its name says) names. 0 for a bitmap font. */
int zt_font_file(struct TextFont *tf, char *out, size_t outn);

/* Faces for text in tf at px pixels: tf's own file, then the caller's
   fallbacks (NULL-ended, may be NULL), then the defaults. With fixed, the
   defaults start with the system's Vera Sans Mono, so code stays in one
   width when tf is a bitmap font. NULL when none opens. */
zt_font *zt_open_for(struct TextFont *tf, int px, CONST_STRPTR const *fallbacks, int fixed);

/* Whether a set of faces opens at all (the defaults after fallbacks): 1,
   or 0 with the reason in why. */
int zt_fonts_usable(CONST_STRPTR const *fallbacks, char *why, ULONG whylen);

/* Draws the columns c0..c1-1 of a coverage buffer (bw x bh, one byte a
   pixel) at x + c0, y in the pen's colour: blended into the pixels read
   back on a screen of more than 256 colours, pixels at half coverage and
   more set on others. */
void zt_blit(struct RastPort *rp, struct ColorMap *cm, ULONG pen, const UBYTE *cov, LONG bw, LONG bh,
             LONG c0, LONG c1, LONG x, LONG y);

/* UTF-8 to and from the system's 8-bit set, for keys and older clipboard
   readers: through codesets.library when it is there, else Latin-1.
   zt_from_local writes at most 4 bytes for one character; zt_to_local
   gives '?' for what the set has not. */
size_t zt_from_local(UBYTE ch, char *utf8);
UBYTE zt_to_local(unsigned long cp);

#endif
