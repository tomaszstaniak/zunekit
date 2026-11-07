/* SPDX-License-Identifier: MIT */
/* zk_edit core: an editable UTF-8 document and its layout, without AROS.
 *
 * The behaviour follows MarkEdit's document model (aros-markedit editor/,
 * `mkdoc`): offsets are bytes and always grapheme cluster boundaries; text
 * going in that is not valid UTF-8 is refused whole; typing merges into
 * one undo step per word (a new word, a line feed or a jump starts a new
 * step), Backspace and Delete runs merge the same way; undo restores the
 * selection the step replaced; at most ZE_UNDO_LIMIT steps are kept.
 *
 * Every change is whole or not at all: memory is taken before the text
 * changes, so a failed allocation returns ZE_NOMEM and leaves the text, the
 * caret, the selection and the history as they were (the draft is never
 * lost). Caller-owned results (ze_text, ze_selected) stay valid until the
 * next call that changes the text. */
#ifndef ZK_EDIT_CORE_H
#define ZK_EDIT_CORE_H

#include <stddef.h>
#include "zk_text.h"

#define ZE_UNDO_LIMIT 1000

enum { ZE_NOMEM = -1, ZE_INVALID = -2 };

typedef struct ze_doc ze_doc;

ze_doc *ze_new(void);
void ze_free(ze_doc *d);

/* Replaces the whole text and clears the history; caret at the end.
   1, ZE_INVALID (not UTF-8) or ZE_NOMEM; the old text stays on failure. */
int ze_set_text(ze_doc *d, const char *s, size_t n);
/* The text, NUL-ended, and its length in bytes. */
const char *ze_text(const ze_doc *d, size_t *n);
/* Changes with every edit, undo, redo and set_text. */
unsigned long ze_revision(const ze_doc *d);

size_t ze_caret(const ze_doc *d);
size_t ze_anchor(const ze_doc *d);
/* The selection as start <= end; equal when there is none. */
void ze_selection(const ze_doc *d, size_t *start, size_t *end);
const char *ze_selected(const ze_doc *d, size_t *n);

/* Moves the caret to pos, snapped to the cluster boundary at or before it;
   extend keeps the anchor (Shift). */
void ze_set_caret(ze_doc *d, size_t pos, int extend);
void ze_select(ze_doc *d, size_t anchor, size_t caret);
void ze_select_all(ze_doc *d);
/* Left/Right: one cluster, or a selection collapsed to its edge. */
void ze_step(ze_doc *d, int forward, int extend);
/* Ctrl+Left/Right: to the start of the next or previous word. */
void ze_word_step(ze_doc *d, int forward, int extend);

/* Replaces the selection (or inserts at the caret): 1 changed, 0 nothing
   to do, ZE_INVALID, ZE_NOMEM. */
int ze_insert(ze_doc *d, const char *s, size_t n);
/* The selection, or the cluster before (Backspace) or after (Delete) the
   caret: 1, 0 (nothing there), ZE_NOMEM. */
int ze_backspace(ze_doc *d);
int ze_delete(ze_doc *d);
/* 1 when a step was undone or redone, 0 when there was none. */
int ze_undo(ze_doc *d);
int ze_redo(ze_doc *d);
int ze_can_undo(const ze_doc *d);
int ze_can_redo(const ze_doc *d);

/* --- layout ------------------------------------------------------------ */

/* A line of the laid-out text: bytes [start, start + len), the line feed
   or the spaces it breaks at not counted in len; y of its top. */
typedef struct { size_t start, len; long y; } ze_line;

typedef struct {
    ze_line *line;
    size_t nlines, cap;
    long width, lineh, height;
    unsigned flags;          /* ZT_BOLD for the whole text */
} ze_view;

void ze_view_init(ze_view *v);
void ze_view_free(ze_view *v);
/* Wraps d's text to width pixels with f (words kept whole where they fit,
   a word wider than the line broken at a cluster). 0 when out of memory:
   the view's lines stay as they were. */
int ze_layout(ze_view *v, const ze_doc *d, zt_font *f, long width);
/* The line that holds offset off (a caret at a wrap belongs to the next). */
size_t ze_line_of(const ze_view *v, size_t off);
/* The caret's place: x from the line's left, the line. */
long ze_caret_x(const ze_view *v, const ze_doc *d, zt_font *f, size_t off, size_t *line);
/* The boundary nearest to the point (x, y) in view pixels. */
size_t ze_hit(const ze_view *v, const ze_doc *d, zt_font *f, long x, long y);
/* The boundary dlines lines up (<0) or down from off, at the x *goal (set
   from off when < 0); Home/End: the line's start or end. */
size_t ze_vertical(const ze_view *v, const ze_doc *d, zt_font *f, size_t off, long dlines, long *goal);
size_t ze_line_home(const ze_view *v, size_t off);
size_t ze_line_end(const ze_view *v, size_t off);

#endif
