/* SPDX-License-Identifier: MIT */
/* zk_table: a multi-column table for Zune programs, on the standard NList
 * class (NList.mcc, shipped with AROS), with what a file browser needs on top:
 *
 *   - rows appended in batches without the list being cleared on screen;
 *   - multiple selection, Amiga+A for all rows;
 *   - selected rows dragged out of the window, with the Wanderer drawer
 *     window under the pointer found at the release.
 *
 * Rows are opaque pointers (APTR) owned by the program; the table only shows
 * them through the columns callback. The program keeps its own data and
 * logic; nothing here knows what a row stands for.
 *
 * Notifications: the program may notify on the NList object itself, e.g.
 * MUIA_NList_SelectChange and MUIA_NList_Active, as with any NList. */
#ifndef ZK_TABLE_H
#define ZK_TABLE_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <utility/tagitem.h>
#include <stddef.h>

struct zk_table_cfg {
    /* NList format string, one entry per column ("BAR,P=\33r BAR,..."). */
    const char *format;
    /* Fill cols[0..n-1] with the texts of a row; entry NULL asks for the
     * titles. The strings must stay valid until the next call. */
    void (*columns)(void *ctx, APTR entry, char **cols);
    void *ctx;
    /* Optional. Called once a drag of selected rows has started (moved six
     * pixels with the button held on a selected row). */
    void (*drag_started)(void *ctx);
    /* Optional. Called at the release: target is the drawer of the Wanderer
     * window under the pointer, or NULL; why says why there is none ("released
     * inside the window", "dropped on a window of ...", "cancelled"). */
    void (*drag_ended)(void *ctx, const char *target, const char *why);
    /* Optional. Called after Amiga+A selected every row. */
    void (*selected_all)(void *ctx);
};

/* The class; 0 when NList.mcc cannot be opened. Call once before creating
 * tables, zk_table_exit() after the last one is disposed. */
int zk_table_init(void);
void zk_table_exit(void);

/* A table inside its NListview (with scrollers). Returns the NListview to
 * put in a group; *table receives the NList object the other calls take.
 * cfg is copied; extra (may be NULL) are more tags for the NList. NULL on
 * failure. */
Object *zk_table_new(const struct zk_table_cfg *cfg, Object **table,
                     const struct TagItem *extra);

/* Rows at the end, in one go: one redraw for the batch, and none of the rows
 * already on screen cleared first. */
void zk_table_append(Object *table, APTR *rows, ULONG n);
void zk_table_clear(Object *table);
ULONG zk_table_count(Object *table);
APTR zk_table_row(Object *table, LONG pos);

/* pos -1: every row. */
void zk_table_select(Object *table, LONG pos, int on);
int zk_table_is_selected(Object *table, LONG pos);
/* Next selected row after pos (start with -1); -1 when there is none. */
LONG zk_table_next_selected(Object *table, LONG pos);
ULONG zk_table_selected_count(Object *table);
LONG zk_table_active(Object *table);
void zk_table_set_active(Object *table, LONG pos);
/* Quiet on or off around many changes; balanced calls nest as in NList. */
void zk_table_quiet(Object *table, int on);

/* The drawer shown by the Wanderer window at (mx, my), relative to the window
 * from. 1 and the path in out when it is a drawer window of Wanderer and the
 * drawer exists; 0 and the reason in why otherwise. Usable without a table.
 * A wrapper of zk_dragout_drawer_at (zk_dragout.h, screen coordinates):
 * link src/zk_dragout.c and src/zk_dragout_core.c with src/zk_table.c. */
int zk_wanderer_drawer_at(struct Window *from, WORD mx, WORD my, char *out, size_t n,
                          char *why, size_t whyn);

#endif
