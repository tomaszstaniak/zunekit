/* SPDX-License-Identifier: MIT */
/* Linktext's logic without any AROS call: content, wrapping, hit testing and
 * selection runs. The host tests cover this file through a fake font. */
#ifndef LINKTEXT_CORE_H
#define LINKTEXT_CORE_H

#include <stddef.h>

typedef unsigned long lt_ulong;
typedef unsigned long lt_iptr;  /* wide enough for an IPTR on both host and guest */

/* What the class gives; mirrors struct MUI_LinktextSpan. */
typedef struct {
    lt_ulong start, length, flags;
    const char *target;
    lt_iptr id;
} lt_span_in;

typedef struct {
    lt_ulong start, length, flags;
    char *target;              /* NULL for a style span */
    lt_ulong target_len;
    lt_iptr id;
} lt_span;

/* What the class gives; mirrors struct MUI_LinktextBlock. */
typedef struct {
    lt_ulong start, length, flags, indent, rindent, background, padx, pady;
    lt_ulong align, rule, head;   /* a Table block's columns, rules, header */
} lt_block_in;

typedef lt_block_in lt_block;

/* One allocation, shared by reference: a hook keeps its target alive this way
   while the content is being replaced under it. */
typedef struct {
    int refs;
    char *text;
    lt_ulong len;
    lt_span *spans;
    lt_ulong nspans;
    lt_ulong *links;           /* indexes of spans with a target */
    lt_ulong nlinks;
    lt_block *blocks;
    lt_ulong nblocks;
    int any_font;              /* a span measures in another font */
    int utf8;                  /* the text is UTF-8: lines break, carets land and
                                  lines are hit only on grapheme cluster
                                  boundaries (zk_text); set it before laying
                                  the content out, lt_set_utf8 */
} lt_content;

enum { LT_OK, LT_RANGE, LT_ORDER, LT_TARGET, LT_TOOLARGE, LT_NOMEM };
/* Span flags, the same numbers as MUIV_Linktext_Span_*. */
#define LT_BOLD        1UL
#define LT_FIXED       2UL
#define LT_DIM         4UL
#define LT_NOUNDERLINE 8UL
#define LT_ACCENT      16UL
#define LT_SHADE       32UL
#define LT_LARGE       64UL
#define LT_FONT_FLAGS  (LT_BOLD | LT_FIXED | LT_LARGE)
#define LT_SPAN_FLAGS  (LT_BOLD | LT_FIXED | LT_DIM | LT_NOUNDERLINE | LT_ACCENT | LT_SHADE | LT_LARGE)
/* Block flags, the same numbers as MUIV_Linktext_Block_*. */
#define LT_BLOCK_RIGHT 1UL
#define LT_BLOCK_FILL  2UL
#define LT_BLOCK_PAD   4UL
#define LT_BLOCK_SPACE 8UL
#define LT_BLOCK_TABLE 16UL
#define LT_BLOCK_HEAD  32UL
#define LT_PAD_MAX     64UL    /* PadX, in eighths of a character */
#define LT_SPACE_MAX   16UL    /* PadY of a Space block, in eighths of a line */
/* A Table block: each paragraph a row, its cells separated by TAB. At most
   LT_TABLE_COLS columns; a TAB past them is a space. align holds two bits
   a column, column 0 lowest: 0 left, 1 centre, 2 right. */
#define LT_TABLE_COLS  16
#define LT_ALIGN_LEFT   0UL
#define LT_ALIGN_CENTER 1UL
#define LT_ALIGN_RIGHT  2UL

/* Provided by the build: memory, so that the class needs no C library and
   the tests can fail any allocation. */
void *lt_alloc(size_t n);
void lt_free(void *p);

/* A new content with one reference, copied from the arguments; NULL and
   *status on failure. len (lt_ulong)-1 means up to the first NUL. */
lt_content *lt_content_new(const char *text, lt_ulong len, const lt_span_in *spans,
                           lt_ulong n, int *status);
/* The same with blocks, which are copied too. A block starts at a
   paragraph's start and ends at a paragraph's end; blocks come in order and
   do not overlap. More blocks than the text has paragraphs is LT_ORDER. */
lt_content *lt_document_new(const char *text, lt_ulong len, const lt_span_in *spans,
                            lt_ulong n, const lt_block_in *blocks, lt_ulong nb, int *status);
/* The text's length as lt_document_new takes it, for a check before the
   blocks are copied: (lt_ulong)-1 counts up to the first NUL. */
lt_ulong lt_text_length(const char *text, lt_ulong len);
void lt_content_ref(lt_content *c);
/* Marks c as UTF-8 (on 1) or as the system's 8-bit set (0, as created). */
void lt_set_utf8(lt_content *c, int on);
void lt_content_unref(lt_content *c);

/* Width in pixels of n bytes; `flags` has LT_BOLD and LT_FIXED of the run. */
typedef long (*lt_measure)(void *ctx, const char *s, lt_ulong n, lt_ulong flags);

typedef struct {
    lt_ulong start;  /* byte offset                                   */
    lt_ulong len;    /* bytes, spaces at the break included           */
    lt_ulong shown;  /* bytes that are drawn: len without those spaces */
    long x;          /* pixels from the column's left                 */
    long block;      /* the block it is in, -1 for none               */
    long y, h;       /* pixels from the text's top, height: lt_heights */
    /* Lines come in the order they are shown, top to bottom. In a table a
       row's cells are side by side: a cell's lines follow the line of the
       cell to their left (cont), with the same y. Their bytes are then not
       in order, so lines are searched by pstart, the start of the line's
       paragraph (its own start outside tables), which is in order. */
    lt_ulong pstart;
    long cell;       /* the column, -1 outside tables                  */
    long trow;       /* the row in its table, -1 outside tables        */
    long cx0, cx1;   /* the cell's box, pixels from the column's left  */
    int cont;        /* beside the line before it, same y              */
    int large;       /* holds Large text: lt_heights gives it largeh   */
    long ty;         /* text below the line's top: a table row's room  */
} lt_line;

/* Wraps to a column `width` pixels wide: *out is a new array of *n lines,
   for lt_free. `avg` is the average character width, for block indents.
   Never splits a line into nothing: every line holds at least one byte, or
   is an empty paragraph. 0 on failure, with *out and *n unchanged. */
int lt_layout(const lt_content *c, long width, long avg, lt_measure m, void *ctx,
              lt_line **out, lt_ulong *n);

/* The same as lt_layout for c, reusing the lines `ol` (on of them) that
   lt_layout made for `old` at the same width and avg: the paragraphs
   before the first `keep` bytes' last paragraph start (lt_common_prefix)
   are not wrapped again, nor are those where the bytes are measured in
   other fonts than before (lt_font_prefix). Blocks that differ, or reach
   into the rest, are laid out again from their start. A text growing at
   its end costs its last paragraphs, not the whole text. */
int lt_layout_keep(const lt_content *c, long width, long avg, lt_measure m, void *ctx,
                   const lt_content *old, const lt_line *ol, lt_ulong on, lt_ulong keep,
                   lt_line **out, lt_ulong *n);

/* The first offset below `upto` at which a's and b's bytes are measured in
   different fonts (LT_BOLD, LT_FIXED); upto when there is none. */
lt_ulong lt_font_prefix(const lt_content *a, const lt_content *b, lt_ulong upto);

/* Heights and places of laid-out lines: `lineh` each, `largeh` for a line
   with Large text, an empty paragraph `empty` (1..lineh), an empty
   paragraph of a Space block its PadY eighths of lineh (1..LT_SPACE_MAX).
   A table row gets PadY eighths of lineh (at most a line) above and below
   its text; lines beside each other share y and h. */
void lt_heights(const lt_content *c, lt_line *l, lt_ulong n, long lineh, long largeh, long empty);
/* Among the lines beside line i (i itself and those after it with cont),
   the one whose cell holds x; i when none does. */
lt_ulong lt_line_at_x(const lt_line *l, lt_ulong n, lt_ulong i, long x);
/* The lines of the block that line i is in: *first..*last. */
void lt_block_lines(const lt_line *l, lt_ulong n, lt_ulong i, lt_ulong *first, lt_ulong *last);
/* The first line that can be at the top with the last line still whole in
   `height` pixels. */
lt_ulong lt_last_top(const lt_line *l, lt_ulong n, long height);
/* Whole lines from `first` that fit in `height` pixels, at least 1. */
lt_ulong lt_fit_lines(const lt_line *l, lt_ulong n, lt_ulong first, long height);
/* The line at `dy` pixels below the top of line `first`; n when past the
   last line. */
lt_ulong lt_line_at_y(const lt_line *l, lt_ulong n, lt_ulong first, long dy);

/* A block's area in the column: *left and *width in pixels. Its paragraphs
   wrap to that width, which is at least one character. */
void lt_block_area(const lt_block *b, long width, long avg, long *left, long *area);

/* The box behind block `b`, laid out by lt_layout with the same width and
   avg: lines *first..*last, pixels *x0..*x1 (x1 excluded) from the column's
   left. 0 when the block has no lines. Its vertical room, in pixels for a
   line `lineh` high: lt_block_pady. */
int lt_block_box(const lt_content *c, const lt_line *l, lt_ulong nl, lt_ulong b,
                 long width, long avg, lt_ulong *first, lt_ulong *last, long *x0, long *x1);

long lt_block_pady(const lt_block *b, long lineh);

long lt_width(const lt_content *c, lt_ulong start, lt_ulong n, lt_measure m, void *ctx);

/* The line that holds offset `off` (a line's end belongs to the next line,
   except for the last one; in a table the cell's line that holds it, or
   the last line of the row before it). 0 when there are no lines. */
lt_ulong lt_line_of(const lt_line *l, lt_ulong n, lt_ulong off);

/* What is at x pixels from the column's left on a line: *caret the nearest boundary
   between characters, for selecting; the return value the offset of the
   character under x, or -1 past the end of the line's shown text. */
long lt_hit(const lt_content *c, const lt_line *line, long x, lt_measure m, void *ctx,
            lt_ulong *caret);

/* What a new content keeps of the old one, for SetDocument: the bytes at
   the start that are the same in both. A selection that ends there, or a
   link wholly in them that lt_same_link finds again, survives the change. */
lt_ulong lt_common_prefix(const lt_content *a, const lt_content *b);
/* Link `link` of a is link `link` of b: the same place, length and target,
   ending within the first `upto` bytes. */
int lt_same_link(const lt_content *a, const lt_content *b, long link, lt_ulong upto);

/* The link at byte `off`, counted in text order; -1 for none. */
long lt_link_at(const lt_content *c, lt_ulong off);
const lt_span *lt_link(const lt_content *c, lt_ulong link);
/* Link after (dir > 0) or before cur, -1 starting from outside; -1 past the end. */
long lt_link_step(const lt_content *c, long cur, int dir);

/* One fragment of a link: the f-th line it reaches. 1 and *line, *x0, *x1
   (pixels from the column's left) when there is one. */
int lt_link_fragment(const lt_content *c, const lt_line *l, lt_ulong nl, lt_ulong link,
                     lt_ulong f, lt_measure m, void *ctx, lt_ulong *line, long *x0, long *x1);

/* A stretch of a line that draws the same way. Start with pos = line start;
   returns its length, 0 at the end. `sel_a` <= `sel_b` is the selection.
   `*flags` gets the span's LT_SPAN_FLAGS and the two below. */
#define LT_RUN_LINK 0x100UL
#define LT_RUN_SEL  0x200UL
lt_ulong lt_run(const lt_content *c, const lt_line *line, lt_ulong pos,
                lt_ulong sel_a, lt_ulong sel_b, lt_ulong *flags);

#endif
