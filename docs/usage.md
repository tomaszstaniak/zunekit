---
type: guide
updated: 2026-10-21
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
disabled slider draws as Zune draws disabled objects.
