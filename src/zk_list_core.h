/* SPDX-License-Identifier: MIT */
/* zk_list's pure part: a cell's text as NList gives it (UTF-8 with NList's
 * escape sequences) cut into what the list keeps for itself and the runs
 * zk_list draws. No AROS calls; tested on the host. */
#ifndef ZK_LIST_CORE_H
#define ZK_LIST_CORE_H

#include <stddef.h>

/* Run flags. Italic is kept so it can be drawn once the layer has it; it
   is drawn upright now. */
#define ZL_BOLD      1u
#define ZL_ITALIC    2u
#define ZL_UNDERLINE 4u

typedef struct zl_run {
    size_t start, len;          /* bytes of the cell text */
    unsigned flags;
} zl_run;

/* The length of the alignment escapes (ESC c, ESC r, ESC l, ESC j) at the
   start of s[0..n): NList keeps those, as they place the whole cell. */
size_t zl_lead(const char *s, size_t n);

/* The runs of s[0..n): text between escapes, each with the style the
   escapes before it set (ESC b, ESC i, ESC u; ESC n ends them all). Other
   escapes (pens, images, ...) are left out, ESC and its letter, and a
   bracketed argument after the letter. An empty run is not made. Returns
   the number of runs; at most max are written, the rest only counted. */
size_t zl_runs(const char *s, size_t n, zl_run *out, size_t max);

/* The columns of an NList format ("BAR,P=\33r BAR,"): its commas plus one,
   1 for none or an empty one. */
int zl_format_columns(const char *format);

/* FNV-1a of s[0..n) with flags: the width cache's key. */
unsigned long zl_hash(const char *s, size_t n);

#endif
