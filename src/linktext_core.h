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
    lt_ulong start, length, flags, indent, rindent, background;
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
} lt_content;

enum { LT_OK, LT_RANGE, LT_ORDER, LT_TARGET, LT_TOOLARGE, LT_NOMEM };
/* Span flags, the same numbers as MUIV_Linktext_Span_*. */
#define LT_BOLD        1UL
#define LT_FIXED       2UL
#define LT_DIM         4UL
#define LT_NOUNDERLINE 8UL
#define LT_FONT_FLAGS  (LT_BOLD | LT_FIXED)
#define LT_SPAN_FLAGS  (LT_BOLD | LT_FIXED | LT_DIM | LT_NOUNDERLINE)
/* Block flags, the same numbers as MUIV_Linktext_Block_*. */
#define LT_BLOCK_RIGHT 1UL
#define LT_BLOCK_FILL  2UL

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
void lt_content_unref(lt_content *c);

/* Width in pixels of n bytes; `flags` has LT_BOLD and LT_FIXED of the run. */
typedef long (*lt_measure)(void *ctx, const char *s, lt_ulong n, lt_ulong flags);

typedef struct {
    lt_ulong start;  /* byte offset                                   */
    lt_ulong len;    /* bytes, spaces at the break included           */
    lt_ulong shown;  /* bytes that are drawn: len without those spaces */
    long x;          /* pixels from the column's left                 */
    long block;      /* the block it is in, -1 for none               */
} lt_line;

/* Wraps to a column `width` pixels wide: *out is a new array of *n lines,
   for lt_free. `avg` is the average character width, for block indents.
   Never splits a line into nothing: every line holds at least one byte, or
   is an empty paragraph. 0 on failure, with *out and *n unchanged. */
int lt_layout(const lt_content *c, long width, long avg, lt_measure m, void *ctx,
              lt_line **out, lt_ulong *n);

/* A block's area in the column: *left and *width in pixels. Its paragraphs
   wrap to that width, which is at least one character. */
void lt_block_area(const lt_block *b, long width, long avg, long *left, long *area);

/* The box behind block `b`, laid out by lt_layout with the same width and
   avg: lines *first..*last, pixels *x0..*x1 (x1 excluded) from the column's
   left. 0 when the block has no lines. */
int lt_block_box(const lt_content *c, const lt_line *l, lt_ulong nl, lt_ulong b,
                 long width, long avg, lt_ulong *first, lt_ulong *last, long *x0, long *x1);

long lt_width(const lt_content *c, lt_ulong start, lt_ulong n, lt_measure m, void *ctx);

/* The line that holds offset `off` (a line's end belongs to the next line,
   except for the last one). 0 when there are no lines. */
lt_ulong lt_line_of(const lt_line *l, lt_ulong n, lt_ulong off);

/* What is at x pixels from the column's left on a line: *caret the nearest boundary
   between characters, for selecting; the return value the offset of the
   character under x, or -1 past the end of the line's shown text. */
long lt_hit(const lt_content *c, const lt_line *line, long x, lt_measure m, void *ctx,
            lt_ulong *caret);

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
