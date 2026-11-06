---
type: guide
updated: 2026-11-06
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
and another file manager will not be recognised. The lookup lives in
`zk_dragout` (see below), so a program that uses the table also compiles
`src/zk_dragout.c` and `src/zk_dragout_core.c`.

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

## zk_toolbar

A single row of icon buttons above a window's content, as image editors and
capture tools have: actions (Copy, Save), one group of tools of which one
is active (Select, Rectangle, Arrow), toggles, separators, a flexible space
and, at the right end, the program's own status cells.

```c
#include "zk_toolbar.h"

static const struct zk_toolbar_item items[] = {
    { ZK_TB_ACTION, 0,   ID_COPY, "PROGDIR:Icons/copy.svg", "Copy", "Copy" },
    { ZK_TB_SEPARATOR },
    { ZK_TB_TOOL,   'v', ID_SEL,  "PROGDIR:Icons/pointer.svg", "Sel", "Select (V)" },
    { ZK_TB_TOOL,   'r', ID_RECT, "PROGDIR:Icons/square.svg", "Rect", "Rectangle (R)" },
    { ZK_TB_SPACE },
};
Object *bar;

zk_toolbar_init();                              /* once; 0 on failure */
bar = zk_toolbar_new(items, 5, NULL);           /* Select is active */
zk_toolbar_add(bar, zoom_text);                 /* right end */
DoMethod(bar, MUIM_Notify, MUIA_ZkToolbar_Tool, MUIV_EveryTime,
         (IPTR)app, 2, MUIM_Application_ReturnID, ID_TOOL);
DoMethod(bar, MUIM_Notify, MUIA_ZkToolbar_Pressed, MUIV_EveryTime,
         (IPTR)app, 2, MUIM_Application_ReturnID, ID_PRESSED);
```

Each button is a Zune Area object with Area's own input mode: an action
reports a press on a verified release (`MUIA_ZkToolbar_Pressed`, which
notifies on every press, also of the same action twice); a tool becomes
active on the press (`MUIA_ZkToolbar_Tool`, which notifies on a change by
the user, a key or the program); a toggle flips at once and reports a press
(`zk_toolbar_get_toggle` reads its state). Setting `MUIA_ZkToolbar_Tool`
activates a tool; `zk_toolbar_set_toggle` and `zk_toolbar_set_disabled`
change a button without a notification.

Look: buttons have no frame and lie flat on the window. The active tool and
a button while it is held get the selected background of the Zune
preferences (`MUII_SelectedBack`), as other Zune buttons do; a disabled
button is ghosted by Zune. Zune has no hover state, so nothing changes under
the pointer; the help bubble (`MUIA_ShortHelp`, from the item's help text)
names the button and its key.

Icons: SVG files read when the toolbar is created and rendered by
svgicon.library at a size that follows the window font, or at
`MUIA_ZkToolbar_IconSize` pixels (in the extra tags). Only the coverage
(alpha) of the rendering is used; it is drawn in the text pen of the
preferences, so outline icons such as Tabler's (`stroke="currentColor"`)
take the theme's colour. On screens of 8 bits or fewer, pixels of at least
half coverage are drawn, without smoothing. svgicon.library is optional:
without it, or for a file that cannot be read or rendered, the button
shows its label. The files are read once; a file is at most 256 KB.

Keys: an item's key works while the window is active, without Ctrl, Alt or
Amiga, and letters are case-blind. A text field with the keyboard keeps its
keys: Zune offers a key to the active object first, and the toolbar also
ignores keys while the active object is a String, BetterString, Textinput
or TextEditor object.

Limits: the row does not scroll or fold. Its minimum width holds every
button and status cell, so the window cannot be made narrower than that.

Compile `src/zk_toolbar.c`.

## zk_imageview

A picture at any zoom, with panning, the pixel under the pointer and a
layer on top for the program's own drawing: annotations, a selection, a
ruler. The picture is 32-bit ARGB in memory (bytes A, R, G, B per pixel);
the view does not copy it.

```c
#include "zk_imageview.h"

struct TagItem tags[] = {
    { MUIA_ZkImageView_DrawHook, (IPTR)&draw_hook },     /* may be omitted */
    { MUIA_ZkImageView_InputHook, (IPTR)&input_hook },
    { MUIA_CycleChain, 1 },
    { TAG_DONE, 0 } };
Object *view;

zk_imageview_init();                        /* once; 0 on failure */
view = zk_imageview_new(tags);              /* zoom 0: fit */
zk_imageview_set_image(view, argb, w, h);   /* kept by the program */
/* after changing pixels or the layer: */
zk_imageview_changed(view, x, y, cw, ch);
```

Zoom 0 fits the picture into the view, never above 100 %; other values are
percent, 1 to 3200. `MUIA_ZkImageView_Shown` is the zoom actually shown.
The wheel scrolls (with Shift, or a horizontal wheel, sideways),
Ctrl+wheel zooms around the pointer, Space+drag and the middle button pan,
and 0 (fit), 1 (100 %), + and - zoom while the view has the keyboard or no
object has. A left-button drag pans as well when there is no input hook or
the hook does not take the press.

`MUIA_ZkImageView_PointerX`/`PointerY` follow the picture pixel under the
pointer (-1 outside the picture), without a button held;
`zk_imageview_pixel` reads its value.

The DrawHook is called after the picture has been drawn into the view's
off-screen bitmap, with a RastPort, the view position of picture point 0,0,
the zoom shown and the rectangle being redrawn; picture point (px, py) is at
(left + px * shown / 100, top + py * shown / 100). Drawing is clipped to
that rectangle. The InputHook gets left-button presses, moves and releases
in picture coordinates (not clamped), and a cancel for Esc or the right
button during a press; it returns nonzero when it used the event.

Drawing goes through an off-screen bitmap (a friend of the window's), so
redraws do not flicker. The view keeps the composed picture: a window
refresh copies it, and only a zoom, a scroll, a new size or
`zk_imageview_changed` composes it again, and then only the part that
changed. Above 100 % each view pixel takes the nearest picture pixel; below
100 % the picture is scaled down once per zoom by box averaging and kept, so
scrolling a fitted 1920 x 1080 picture copies rows. Pixels with alpha below
255 are blended with the background colour (`MUIA_ZkImageView_Background`,
0x00RRGGBB, default 0x00202020).

The view needs a screen deeper than 8 bits: on a palette screen the picture
is written with WritePixelArray, which AROS maps to the nearest pens.

Compile `src/zk_imageview.c` and `src/zk_imageview_core.c`.

## zk_dragout

Drags a file out of a window onto the rest of the system, as a picture is
dragged out of a capture tool into a drawer. The program calls
`zk_dragout_run` when a press in its own object has moved far enough to be a
drag; the call returns at the release.

```c
#include "zk_dragout.h"

static int write_png(void *ctx, const char *path, char *why, size_t whyn);

struct zk_dragout_cfg cfg = {
    argb, w, h,               /* thumbnail, scaled to at most 160 x 160 */
    "Capture.png",            /* name in the target */
    write_png, my_ctx,
    "T:"                      /* for AppWindow drops; NULL refuses them */
};
char path[512], why[200];

switch (zk_dragout_run(window, &cfg, path, sizeof(path), why, sizeof(why))) {
case ZK_DRAGOUT_DRAWER:    /* path is the file written */
case ZK_DRAGOUT_APPWINDOW: /* path in T:, sent to that window */
    break;
default:                   /* why says what happened, for the user */
    break;
}
```

While the drag runs, a small borderless window with the thumbnail follows
the pointer; the right button cancels. The pointer and the buttons are read
every 1/50 s from the screen and input.device, so the drag works over other
programs' windows without their cooperation. At the release:

- on a Wanderer drawer window, `write_file` writes the file into that
  drawer; a name that exists gets -2, -3, ... before its extension
  (`Capture-2.png`);
- on another program's AppWindow (Dockbar, an editor that takes drops), the
  file is written to `temp_dir` and sent to the window with
  workbench.library's `SendAppWindowMessage`, as Wanderer sends a drop; the
  file stays there for the receiver. A window that is not an AppWindow
  gets nothing and the file is deleted again;
- on the Wanderer desktop the result is `ZK_DRAGOUT_DESKTOP` and nothing is
  written, so the program can offer another way, such as a save
  requester;
- on its own windows or on no window the result is `ZK_DRAGOUT_NOWHERE`.

The call blocks the program's input while it runs; events that arrive at
its windows meanwhile (the release among them) are handled afterwards, so
the object that started the drag should ignore its button release after
the call. Wanderer is recognised by its task name and a drawer window by
its title (the drawer's path), as in `zk_table`; `zk_dragout_drawer_at`
does this lookup for a screen point. Another file manager is not
recognised. Wanderer may show the new file only after the drawer is
updated.

Compile `src/zk_dragout.c` and `src/zk_dragout_core.c`.
