/* SPDX-License-Identifier: MIT */
/* zk_dragout: drags a file out of a Zune window onto the rest of the
 * system, as a picture is dragged out of a capture tool into a drawer.
 *
 * The program calls zk_dragout_run() when a press in its own object has
 * moved far enough to be a drag. A small borderless window with a thumbnail
 * follows the pointer until the left button is let go; the right button
 * cancels. Then the target under the pointer decides:
 *
 *   - a Wanderer drawer window: write_file() writes the file into that
 *     drawer under file_name (a name that exists gets -2, -3, ...);
 *   - another program's AppWindow (Dockbar, an editor that accepts drops):
 *     the file is written to temp_dir first, then sent to that window with
 *     SendAppWindowMessage, as Wanderer does;
 *   - the Wanderer desktop or anything else: nothing is written; the result
 *     says what was there, so the program can offer another way.
 *
 * The pointer is followed by polling (screen position and input.device
 * qualifiers) on the calling task, so it works over other programs' windows
 * without their cooperation. The program's windows do not get input while
 * the drag runs. */
#ifndef ZK_DRAGOUT_H
#define ZK_DRAGOUT_H

#include <exec/types.h>
#include <stddef.h>
#include <intuition/intuition.h>

enum zk_dragout_result
{
    ZK_DRAGOUT_DRAWER,        /* written into a Wanderer drawer */
    ZK_DRAGOUT_APPWINDOW,     /* sent to another program's AppWindow */
    ZK_DRAGOUT_DESKTOP,       /* dropped on the Wanderer desktop: nothing done */
    ZK_DRAGOUT_NOWHERE,       /* own window, a window that takes no drops, no window */
    ZK_DRAGOUT_CANCELLED,     /* right button */
    ZK_DRAGOUT_FAILED         /* the target was valid, writing failed */
};

struct zk_dragout_cfg
{
    /* Thumbnail, ARGB like zk_imageview; NULL for a plain frame. At most
     * 160 x 160 is used; larger ones are scaled down. */
    const UBYTE *thumb;
    LONG thumb_w, thumb_h;
    /* The name the file gets in the target, without a path. */
    const char *file_name;
    /* Writes the file to path; 1 on success, else 0 and a reason in why. */
    int (*write_file)(void *ctx, const char *path, char *why, size_t whyn);
    void *ctx;
    /* Where files for AppWindow drops are written (e.g. "T:"). NULL refuses
     * AppWindow drops (ZK_DRAGOUT_NOWHERE). */
    const char *temp_dir;
};

/* Runs a drag from window from. path gets the written file's full name
 * (DRAWER, APPWINDOW); why gets a sentence for the user in every case. */
enum zk_dragout_result zk_dragout_run(struct Window *from, const struct zk_dragout_cfg *cfg,
                                      char *path, size_t pathn, char *why, size_t whyn);

/* The drawer shown by the Wanderer window at screen point (x, y) of the
 * screen of from: 1 and the path in out when it is a Wanderer drawer
 * window and the path is a directory; otherwise 0 and a reason in why.
 * (Moved here from zk_table; zk_wanderer_drawer_at stays as a wrapper.) */
int zk_dragout_drawer_at(struct Window *from, WORD x, WORD y, char *out, size_t n,
                         char *why, size_t whyn);

#endif
