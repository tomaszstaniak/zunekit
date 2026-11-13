/* SPDX-License-Identifier: MIT */
/* zk_list: the standard NList class (NList.mcc, as the system has it) with
 * its texts in UTF-8, drawn through zk_text, for programs that cannot wait
 * for NList itself to have a UTF-8 mode.
 *
 * A subclass of NList and nothing else: entries, columns, titles, the
 * format, sorting, selection, notifications, construct/display hooks and
 * the escapes ESC b, ESC i, ESC u, ESC n, ESC c, ESC r, ESC l, ESC j work
 * as with NList. What changes, with MUIA_ZkList_UTF8 TRUE, is what the
 * texts the display gives (the display hook's, or the entries themselves
 * without one) are: UTF-8, every character drawn as written.
 *
 * How: the class takes MUIM_NList_Display. After NList's own (which calls
 * the program's hook), each column's text is kept, and NList is given an
 * ESC O[...] instead: one image object of this class's (made with
 * MUIM_NList_CreateImage, as NList's documentation describes), drawn by
 * NList at the cell's place with the cell's column, which draws the kept
 * text with zk_text. NList itself is not changed and no copy of it is
 * carried, so the system's NList, on ABIv11 and mainline, is the one used.
 *
 * When no face can be opened, or the image could not be made, the class
 * shows 8-bit copies in the system's set ('?' for what it lacks) and says
 * so in MUIA_ZkList_Status: the program tells the user, rather than show
 * nearest letters as if they were the text.
 *
 * Not done: NList's own copy to the clipboard (Amiga+C in the list, the
 * CopyToClip methods) copies the cell escapes, so it gives nothing for a
 * UTF-8 column; a program copies rows from its own data. Italic is drawn
 * upright. Texts are drawn on one line (NList's wrapping is not used). */
#ifndef ZK_LIST_H
#define ZK_LIST_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <libraries/mui.h>
#include "zk_text_aros.h"

#define ZKLIST_TAGBASE (TAG_USER | 0x5a4c0000)

/* I.G BOOL: the texts are UTF-8 (default FALSE: NList as it is). */
#define MUIA_ZkList_UTF8     (ZKLIST_TAGBASE + 1)
/* ..G ULONG, one of ZKLIST_*; notifies when it changes. */
#define MUIA_ZkList_Status   (ZKLIST_TAGBASE + 2)
/* I.. const zt_fontset *: faces of the program's own (default the
   system's, zt_system_fontset). Must live as long as the list. */
#define MUIA_ZkList_FontSet  (ZKLIST_TAGBASE + 3)

enum {
    ZKLIST_DRAWN = 0,       /* UTF-8 drawn as written */
    ZKLIST_NOT_SET_UP = 1,  /* not in a window yet */
    ZKLIST_COPIES = 2,      /* no face opened or no image: 8-bit copies shown */
    ZKLIST_MEMORY = 3       /* a cell could not be drawn (memory); its place is
                               marked as not current (zt_stale) */
};

/* The class; 0 when NList.mcc cannot be opened. Call once before creating
 * lists, zk_list_exit() after the last one is disposed. Without
 * freetype2.library the class is still made: its lists show copies
 * (ZKLIST_COPIES). */
int zk_list_init(void);
void zk_list_exit(void);
struct MUI_CustomClass *zk_list_class(void);

/* NewObject of the class with tags; NULL without the class. */
Object *zk_list_new(const struct TagItem *tags);

/* For a window built with MUI's object macros: ZkListObject, tags,
 * ZkListEnd (in place of End), as NListObject. The tags go into an IPTR
 * array, each converted as an initializer is. Not a variadic macro: those
 * do not expand inside the arguments of MUI's own object macros, and End
 * gives its closing parenthesis only once expanded. */
#define ZkListObject ({ const IPTR zk_list_tags_[] = {
#define ZkListEnd TAG_DONE }; zk_list_new((const struct TagItem *)zk_list_tags_); })

#endif
