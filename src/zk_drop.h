/* SPDX-License-Identifier: MIT */
/* zk_drop: the place in a window where files are dropped. It shows a dashed
 * border, an arrow and two lines of text (the first in bold), drawn with the
 * pens and the font of the MUI preferences. It only shows the place: the
 * window is the AppWindow and receives the drop (MUIA_Window_AppWindow,
 * MUIA_AppMessage), as with any Zune window. Set MUIA_Background in extra
 * to set it apart from the window. */
#ifndef ZK_DROP_H
#define ZK_DROP_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <utility/tagitem.h>

/* The class; 0 on failure. Call once before creating drop areas,
 * zk_drop_exit() after the last one is disposed. */
int zk_drop_init(void);
void zk_drop_exit(void);

/* A drop area with the two lines (either may be NULL). extra (may be NULL)
 * are more tags for the object. NULL on failure. The minimum width fits the
 * lines given here; longer lines set later are cut at the border. */
Object *zk_drop_new(const char *title, const char *text, const struct TagItem *extra);

/* New lines (copied; NULL keeps a line as it is), redrawn at once. */
void zk_drop_set(Object *obj, const char *title, const char *text);

#endif
