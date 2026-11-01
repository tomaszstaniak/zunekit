---
type: guide
updated: 2026-11-01
---

# Usage

## zk_table

A table of rows with columns, for lists such as the contents of an archive or
a drawer. It is NList.mcc with three additions:

- appending rows in batches does not clear the rows already on screen (Zune's
  own List class repaints its whole area, background first, for every insert);
- Amiga+A selects every row while the table, or no other object, has the
  keyboard;
- selected rows can be dragged out of the window; at the release the program
  is told the drawer of the Wanderer window under the pointer, or why there
  is none.

Rows are pointers the program owns. The table shows them through a callback
that fills the column texts; it never interprets them.

```c
#include "zk_table.h"

static void columns(void *ctx, APTR row, char **col)
{
    if (!row) { col[0] = "Name"; col[1] = "Size"; return; }   /* titles */
    col[0] = my_name(row);
    col[1] = my_size_text(row);
}

struct zk_table_cfg cfg = { "BAR,P=\33r", columns, my_ctx, NULL, my_drag_end, NULL };
Object *table, *view;

zk_table_init();                         /* once; 0 if NList.mcc is missing */
view = zk_table_new(&cfg, &table, NULL); /* put view in a group */
zk_table_append(table, rows, n);         /* as batches arrive */
```

Selection: `zk_table_select`, `zk_table_is_selected`,
`zk_table_next_selected`, `zk_table_selected_count`. Changes can be
followed with a notification on the table object for
`MUIA_NList_SelectChange` and `MUIA_NList_Active`.

The format string is NList's (`MUIA_NList_Format`): per column `BAR` for a
separator, `P=\33r` to align right, `W=` for a weight, `MAW=` for a
maximum width in percent. NList sizes a column to its widest text; give a
long name column a maximum width so that the columns after it stay visible.

Callbacks are called from the window's input handling; a program that does
its work in its main loop can send itself a return ID from them
(`MUIM_Application_ReturnID`).

Wanderer drawer lookup: Wanderer titles a drawer window with the drawer's
path, and that is how the drawer is found; `zk_wanderer_drawer_at` can be
used on its own. This is a property of Wanderer, not a Workbench interface,
and another file manager will not be recognised.

Requirements: NList.mcc (shipped with AROS; tested with 20.138).

## zk_steps

A slider over a few named steps, such as an archiver's compression levels
("Store only", "Fast", "Normal", "Best"). It is the standard Numeric class
with its own drawing and mouse handling:

- the value is the step's index, 0 to count-1, so the mouse, the cursor keys
  and the wheel all choose the same values (keys and wheel are Numeric's);
- every step has a tick; a click or a drag moves the knob to the nearest one;
- the drawing uses the pens of the MUI preferences, and the size follows the
  font.

```c
#include "zk_steps.h"

static const char *levels[] = { "Fast", "Normal", "Best" };
struct TagItem more[] = { { MUIA_CycleChain, 1 }, { TAG_DONE, 0 } };
Object *level;

zk_steps_init();                       /* once; 0 on failure */
level = zk_steps_new(3, 1, more);      /* three steps, "Normal" */
DoMethod(level, MUIM_Notify, MUIA_Numeric_Value, MUIV_EveryTime,
         (IPTR)app, 2, MUIM_Application_ReturnID, ID_LEVEL);
/* fewer or more steps later, without a notification: */
SetAttrs(level, MUIA_NoNotify, TRUE, MUIA_Numeric_Max, 1, TAG_DONE);
```

The slider shows no text; the program shows the step's name beside or above
it. Notifications are Numeric's (`MUIA_Numeric_Value`). A change of
`MUIA_Numeric_Max` relayouts the group, as for any Numeric.

Keyboard focus is shown with Area's frame, as for other Zune gadgets. A
disabled slider draws as Zune draws disabled objects. A drag ends when the
button is let go, and also when the slider is hidden or disabled (Zune gives
it no input then, so the release could be missed) and on a pointer move
without the button held.

`zk_steps_set_trace(fn)` is a test aid: fn gets a line for every press with
the pointer's x as received, the slider's position and the tick centres.

## zk_icon

The image of a Workbench icon, without label or frame, as large as the image;
a program uses it to show its own icon in the header of its main window.
icon.library draws it (`LayoutIconA` for the window's screen, then
`DrawIconStateA`), so a PNG icon keeps its alpha channel on any background,
on AROS One 1.3 as on later systems. Zune's Dtpic is not used for this: it
drew alpha correctly only from August 2026.

```c
#include "zk_icon.h"

zk_icon_init();                                  /* once; 0 on failure */
Object *icon = zk_icon_new("PROGDIR:Unarc", NULL); /* Unarc.info */
```

A missing icon is not an error: the object then takes no room.

## zk_drop

The place in a window where files are dropped: a dashed border, an arrow and
two lines of text (the first in bold), drawn with the pens and the font of the
MUI preferences. It only shows the place. The window receives the drop as any
Zune AppWindow does (`MUIA_Window_AppWindow`, a notification on
`MUIA_AppMessage`).

```c
#include "zk_drop.h"

struct TagItem back[] = { { MUIA_Background, MUII_TextBack }, { TAG_DONE, 0 } };

zk_drop_init();                                  /* once; 0 on failure */
Object *drop = zk_drop_new("Drop files here", "They are packed at once.", back);
/* while a drop cannot be taken: */
zk_drop_set(drop, "Busy", "Drop again when this job is done.");
```

The minimum width fits the lines given at creation; longer lines set later
are cut at the border. `zk_drop_set` copies the lines (NULL keeps one) and
redraws at once. Nothing shows that a drag is over the area: Wanderer sends a
drop only, and no message while dragging.

## zk_linktext

Read-only wrapped text with links inside its paragraphs: the Linktext class
compiled into a program, so it needs no class file on the system. The program
gives plain text and explicit spans, and gets a hook call when the user opens
or copies a link. It opens nothing itself.

```c
#include "zk_linktext.h"

zk_linktext_init();                                /* once; 0 on failure */
group = zk_linktext_new(&view, 1, tags);           /* with a scrollbar */
DoMethod(view, MUIM_Linktext_SetContent, (IPTR)text, ~0UL, (IPTR)spans, n);
```

Compile `src/zk_linktext.c`, `src/linktext_class.c` and `src/linktext_core.c`
with `-DLINKTEXT_EMBEDDED`. `src/linktext.h` has the attributes, methods and
rules; [Links in text](zune-ui-guide/catalogue/links.md) explains them.
`scripts/build-linktext.sh v1|v11` builds the test program, and
`tests/host/linktext_core_test.c` tests the text logic on the host.
