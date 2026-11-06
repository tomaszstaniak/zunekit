/* SPDX-License-Identifier: MIT */
/* zk_dragout_core: the name and size rules of zk_dragout, without AROS
 * calls, so they can be tested on the host. */
#ifndef ZK_DRAGOUT_CORE_H
#define ZK_DRAGOUT_CORE_H

#include <stddef.h>

/* dir and name as one AmigaDOS path: nothing is added after a ':' or '/'
 * and before an empty dir. 1, or 0 when out is too short. */
int zk_do_join(const char *dir, const char *name, char *out, size_t n);

/* name with the number k before its extension ("shot.png", 2 ->
 * "shot-2.png"; "notes" -> "notes-2"; a leading dot is not an extension:
 * ".config" -> ".config-2"). k < 2 gives name itself. 1, or 0 when out is
 * too short. */
int zk_do_numbered(const char *name, unsigned k, char *out, size_t n);

/* The first of name, name-2, name-3, ... (zk_do_numbered) in dir that
 * exists() denies, as a full path in out. 1, or 0 when out is too short
 * or 9999 names are taken. */
int zk_do_unique(const char *dir, const char *name, int (*exists)(void *ctx, const char *path),
                 void *ctx, char *out, size_t n);

/* Wanderer's windows: its task's name contains "Wanderer"; a drawer
 * window's title is the drawer's path, so it has a ':'. */
int zk_do_is_wanderer(const char *task_name);
int zk_do_is_drawer_title(const char *title);

/* The thumbnail size for a w x h picture in a max x max box, keeping the
 * aspect ratio, never larger than the picture, at least 1 x 1. */
void zk_do_fit_thumb(long w, long h, long max, long *tw, long *th);

#endif
