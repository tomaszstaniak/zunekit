/* SPDX-License-Identifier: MIT */
/* zk_linktext: the Linktext class compiled into a program, so that it needs
 * no class file on the system. linktext.h has the attributes, methods and
 * rules. Compile src/zk_linktext.c, src/linktext_class.c and
 * src/linktext_core.c with -DLINKTEXT_EMBEDDED. */
#ifndef ZK_LINKTEXT_H
#define ZK_LINKTEXT_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <utility/tagitem.h>
#include "linktext.h"

/* The class; 0 on failure. Once before the first object, zk_linktext_exit()
 * after the last one is disposed. */
int zk_linktext_init(void);
void zk_linktext_exit(void);

/* A Linktext object in *view; what the window tree takes is returned. With
 * `scroller` that is a group of the view and a scrollbar, without it the
 * view. extra (may be NULL) are more tags for the view. NULL on failure. */
Object *zk_linktext_new(Object **view, int scroller, const struct TagItem *extra);

/* Whether a view with MUIA_Linktext_UTF8 can draw with set (NULL: the
   system's, zk_text_aros.h): ZT_OK (0), or ZT_ERR_LIBRARY (no
   freetype2.library) or ZT_ERR_FONT (no face opens) with the reason in
   why. Such a view refuses to be set up and its window does not open, so
   say the reason to the user instead. The window font's own file comes on
   top at Setup. */
struct zt_fontset;
int zk_linktext_utf8_fonts(const struct zt_fontset *set, char *why, ULONG whylen);

#endif
