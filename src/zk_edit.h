/* SPDX-License-Identifier: MIT */
/* zk_edit: a Zune text editing class for UTF-8, compiled into a program.
 *
 * The text is UTF-8 and stays as given: the class edits it, never converts
 * it to the system's 8-bit set. Carets, selection, Backspace and Delete
 * move by grapheme clusters; typing merges into one undo step per word;
 * copy puts UTF-8 on the clipboard with an 8-bit copy for older programs.
 * Behaviour and tests follow MarkEdit's editor (aros-markedit); text is
 * shaped and drawn through zk_text (HarfBuzz and FreeType) from the
 * window font's own file, or, for Fixed, the user's fixed font or the
 * system's Vera Sans Mono, then fallbacks. A change that runs out of
 * memory is not made, the draft stays, and MUIA_ZkEdit_Message says so.
 *
 * Keys while active: printable keys through the keymap (the system's
 * character set, converted to UTF-8), Return (a new line, or Acknowledge
 * with SingleLine), Backspace, Del, cursor keys (+Shift selects, +Ctrl or
 * Alt by word), Home/End (the line), Ctrl+Home/End (the text), PageUp/
 * PageDown, Amiga+A select all, C copy, X cut, V paste, Z undo, Shift+Z or
 * Y redo. Tab is left to Zune.
 *
 * Compile src/zk_edit.c, src/zk_edit_core.c, src/zk_text.c,
 * src/zk_text_aros.c, third_party/utf8proc/utf8proc.c and HarfBuzz
 * (third_party/harfbuzz/src/harfbuzz.cc, C++11), link -lfreetype2
 * -lstdc++. */
#ifndef ZK_EDIT_H
#define ZK_EDIT_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <utility/tagitem.h>
#include <libraries/mui.h>

#define ZKEDIT_TAGBASE (TAG_USER | 0x5a450000)

/* ISG STRPTR  the text, UTF-8, NUL-ended. Setting it replaces the text and
               clears undo; text that is not UTF-8 is refused (the old
               stays, Message says why). Getting it returns the class's own
               copy, valid until the next change. */
#define MUIA_ZkEdit_Contents      (ZKEDIT_TAGBASE + 1)
/* I.. BOOL    text in a fixed-width font (code). */
#define MUIA_ZkEdit_Fixed         (ZKEDIT_TAGBASE + 2)
/* ISG BOOL    refuse edits; selection and copy still work. */
#define MUIA_ZkEdit_ReadOnly      (ZKEDIT_TAGBASE + 3)
/* ..GN ULONG  changes with every edit, undo, redo and Contents. */
#define MUIA_ZkEdit_Revision      (ZKEDIT_TAGBASE + 4)
/* I.. BOOL    one line: no line feeds (a pasted one becomes a space);
               Return sets Acknowledge. For fields. */
#define MUIA_ZkEdit_SingleLine    (ZKEDIT_TAGBASE + 5)
/* ..GN ULONG  counts Return in a SingleLine field: notify on it. */
#define MUIA_ZkEdit_Acknowledge   (ZKEDIT_TAGBASE + 6)
/* .SGN LONG   scroll offset in pixels (MUIA_Prop_First of a scroller). */
#define MUIA_ZkEdit_Top           (ZKEDIT_TAGBASE + 7)
/* ..GN LONG   text height in pixels (MUIA_Prop_Entries). */
#define MUIA_ZkEdit_Total         (ZKEDIT_TAGBASE + 8)
/* ..GN LONG   view height in pixels (MUIA_Prop_Visible). */
#define MUIA_ZkEdit_Visible       (ZKEDIT_TAGBASE + 9)
/* ..GN STRPTR the last problem in words (memory, clipboard, invalid text),
               "" after a success. Owned by the object. */
#define MUIA_ZkEdit_Message       (ZKEDIT_TAGBASE + 10)
/* I.. STRPTR * NULL-ended font files after the window font's own (as
               Linktext's FallbackFonts); the caller's, living as long as
               the object. */
#define MUIA_ZkEdit_FallbackFonts (ZKEDIT_TAGBASE + 11)
/* I.. LONG    lines high (default 5; 1 with SingleLine). */
#define MUIA_ZkEdit_Lines         (ZKEDIT_TAGBASE + 12)

/* Replaces the text: 1, -2 not UTF-8, -1 out of memory (the old stays). */
#define MUIM_ZkEdit_SetText       (ZKEDIT_TAGBASE + 0x100)
struct MUIP_ZkEdit_SetText { STACKED ULONG MethodID; STACKED CONST_STRPTR Text; STACKED IPTR Length; };
/* No arguments; 1 when something happened. */
#define MUIM_ZkEdit_Undo          (ZKEDIT_TAGBASE + 0x101)
#define MUIM_ZkEdit_Redo          (ZKEDIT_TAGBASE + 0x102)
#define MUIM_ZkEdit_Copy          (ZKEDIT_TAGBASE + 0x103)
#define MUIM_ZkEdit_Cut           (ZKEDIT_TAGBASE + 0x104)
#define MUIM_ZkEdit_Paste         (ZKEDIT_TAGBASE + 0x105)
#define MUIM_ZkEdit_SelectAll     (ZKEDIT_TAGBASE + 0x106)
/* Inserts UTF-8 at the caret as typing would (1, -2, -1, 0 read-only). */
#define MUIM_ZkEdit_Insert        (ZKEDIT_TAGBASE + 0x107)
struct MUIP_ZkEdit_Insert { STACKED ULONG MethodID; STACKED CONST_STRPTR Text; STACKED IPTR Length; };

/* The class, once before the first object; zk_edit_exit after the last. */
int zk_edit_init(void);
void zk_edit_exit(void);
/* An editor in *edit; what the window tree takes is returned (with a
   scroller, a group). extra: more tags, may be NULL. NULL on failure. */
Object *zk_edit_new(Object **edit, int scroller, const struct TagItem *extra);
/* Whether the class can draw Unicode text here (a font opens): 1, or 0
   and the reason. A window with an editor that cannot does not open. */
int zk_edit_fonts(CONST_STRPTR const *fallbacks, char *why, ULONG whylen);

#endif
