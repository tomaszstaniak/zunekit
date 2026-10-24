/* SPDX-License-Identifier: MIT */
/* zk_icon: a Workbench icon's image in a Zune window, such as a program's own
 * icon in the header of its main window. icon.library draws it, so PNG icons
 * keep their alpha channel on any background; there is no label and no frame.
 * The object is as large as the image; without an icon it takes no room. */
#ifndef ZK_ICON_H
#define ZK_ICON_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <utility/tagitem.h>

/* The class; 0 on failure. Call once before creating icons, zk_icon_exit()
 * after the last one is disposed. */
int zk_icon_init(void);
void zk_icon_exit(void);

/* The icon of name (as for GetDiskObject: "PROGDIR:Unarc" for Unarc.info).
 * extra (may be NULL) are more tags for the object. NULL on failure; a missing
 * icon is not a failure. */
Object *zk_icon_new(const char *name, const struct TagItem *extra);

#endif
