/* SPDX-License-Identifier: MIT */
/* zk_toolbar: a single row of icon buttons for Zune windows, as image
 * editors and capture tools use above their content.
 *
 *   - actions (Copy, Save) report a press; tools form one group of which
 *     exactly one is active (Select, Rectangle, Arrow); toggles keep their own
 *     on/off state; separators and a flexible space arrange the row;
 *   - every button has a help bubble (MUIA_ShortHelp) that names its key,
 *     and its key works while the window is active and no text field has
 *     the focus;
 *   - icons are SVG files (outline icons such as Tabler's) rendered at run
 *     time by svgicon.library at a size that follows the window font (or
 *     MUIA_ZkToolbar_IconSize); the rendered coverage is drawn in the text
 *     pen of the current Zune preferences, so icons follow light and dark
 *     settings alike. Without svgicon.library, or when a file cannot be
 *     read or rendered, the button shows its label instead. The active tool
 *     and pressed buttons get the preferences' selected background;
 *   - objects added after the space (zk_toolbar_add) sit at the right end:
 *     status cells such as a colour readout, an image size or a zoom level.
 *
 * The row does not scroll or fold: its minimum width holds every button and
 * status cell, so the window cannot be made narrower than that. */
#ifndef ZK_TOOLBAR_H
#define ZK_TOOLBAR_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <utility/tagitem.h>

#ifndef MUIB_AROS
#define MUIB_AROS (MUIB_RSVD | 0x00070000)
#endif
#define MUIB_ZkToolbar (MUIB_AROS | 0x00000D00)

/* [.SGN] ULONG: id of the active tool. Setting it to a tool's id activates
 * that tool; notifications fire when the user or a key changes it. */
#define MUIA_ZkToolbar_Tool    (MUIB_ZkToolbar | 0x01)
/* [I...] ULONG: icon size in pixels; 0 (default) follows the font. */
#define MUIA_ZkToolbar_IconSize (MUIB_ZkToolbar | 0x03)
/* [..GN] ULONG: id of the action or toggle last pressed. Notify on
 * MUIV_EveryTime: the same action pressed twice notifies twice. */
#define MUIA_ZkToolbar_Pressed (MUIB_ZkToolbar | 0x02)

enum zk_toolbar_kind
{
    ZK_TB_ACTION,
    ZK_TB_TOOL,
    ZK_TB_TOGGLE,
    ZK_TB_SEPARATOR,
    ZK_TB_SPACE      /* flexible: everything after it goes to the right */
};

struct zk_toolbar_item
{
    UBYTE kind;               /* enum zk_toolbar_kind */
    char key;                 /* shortcut key, 0 for none; letters are case-blind */
    ULONG id;                 /* nonzero for buttons, unique in the toolbar */
    const char *svg;          /* icon file, e.g. "PROGDIR:Icons/square.svg" */
    const char *label;        /* short text when the icon is missing, e.g. "Rect" */
    const char *help;         /* bubble text, e.g. "Rectangle (R)" */
};

/* The class; 0 on failure. Call once before creating toolbars,
 * zk_toolbar_exit() after the last one is disposed. */
int zk_toolbar_init(void);
void zk_toolbar_exit(void);

/* A toolbar of count items; the first tool is active. extra (may be NULL)
 * are more tags for the row group. NULL on failure. */
Object *zk_toolbar_new(const struct zk_toolbar_item *items, ULONG count,
                       const struct TagItem *extra);

/* Adds obj at the right end (after a ZK_TB_SPACE item). 0 on failure; the
 * toolbar then does not own obj. */
int zk_toolbar_add(Object *toolbar, Object *obj);

/* Disables or enables the button id. */
void zk_toolbar_set_disabled(Object *toolbar, ULONG id, BOOL disabled);
/* Sets or reads a toggle's state. */
void zk_toolbar_set_toggle(Object *toolbar, ULONG id, BOOL on);
BOOL zk_toolbar_get_toggle(Object *toolbar, ULONG id);

#endif
