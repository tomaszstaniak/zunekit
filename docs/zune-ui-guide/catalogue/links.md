---
type: reference
updated: 2026-11-06
title: Links in text
group: text
status: draft
classes: "Urltext (Z); TextEditor (Z); Linktext (zunekit, proposed for Zune)"
summary: "What Zune offers for links, and Linktext, which puts links inside wrapped text."
---

# Links in text

Use a separate link control when an address stands on its own, such as a line in an About window. Use Linktext when links belong inside sentences, or when text should stay one scrolling paragraph with its addresses in place. A read-only TextEditor with a double-click hook suits a program that already shows text in TextEditor and can accept a double click.

## What Zune has

| Class | What it does | Links inside text |
|---|---|---|
| `Floattext.mui` | Wraps text through List | No link ranges and no event |
| `Urltext.mcc` (contrib) | One address as one object, with its own opening through OpenURL | Not inside a paragraph; it keeps 255 characters of the address and opens on the application's task |
| `TextEditor.mcc` 15.45 | Editor with `MUIA_TextEditor_ReadOnly` and `MUIA_TextEditor_DoubleClickHook` | Yes, by double click; its autodoc shows opening an HTTP address from a line |

Sources checked on 1 November 2026: AROS `master` `126b90d4cad7874b316e0bef3837d4f9eb7f0922` and deadwood2/AROS `master` `5b5fd4cfd4f039927c0d1bb9cdd7f4fc8644d23e` have the same TextEditor autodoc (blob `01bd68608f99`). [Floattext](https://github.com/aros-development-team/AROS/blob/master/workbench/libs/muimaster/classes/floattext.c), [TextEditor autodoc](https://github.com/aros-development-team/AROS/blob/master/workbench/classes/zune/texteditor/doc/MCC_TextEditor.doc), [TextEditor input handling](https://github.com/aros-development-team/AROS/blob/master/workbench/classes/zune/texteditor/mcc/HandleInput.c), [Urltext](https://github.com/aros-development-team/contrib/tree/master/workbench/classes/mui/urltext).

## What the TextEditor path does

A small program ran a read-only TextEditor 15.45 on mainline AROS and on ABIv11 2026.09. What it showed:

- Position mapping across wrapping is correct. The hook and `MUIA_TextEditor_CursorX`/`CursorY` give the byte offset in the paragraph, whichever wrapped line was clicked.
- The hook is called for a double click only. A single click has no notification.
- TextEditor adds its event handler with its own class, so a subclass's `MUIM_HandleEvent` is never called. An extra handler at a higher priority does see the events, and can read the cursor at the release.
- A click past the end of a line, or below the text, moves the cursor to the end of that line or text. An application therefore cannot tell a click on the last word of a line from a click beside it. The class has no call that gives the pixel position of a character.
- In read-only mode the arrow keys and Return move nothing that could reach a link, so the keyboard has no way to a link. Dragging selects correctly.
- A label with a separate target has to be matched by the application from the cursor offset, and the text styles come from TextEditor's own import codes.

These are the reasons for a separate class. Changing TextEditor could add a single-click hook, but keyboard links, hit tests and spans would change an editor of about 17000 lines, a program could not carry that class inside itself, and existing users would gain nothing.

## Linktext

Linktext is a small read-only class on `MUIC_Area`. It is written in zunekit (`src/linktext*.c`), compiled into programs by `zk_linktext`, and offered to AROS as `Linktext.mcc`. An external class and the compiled-in form are the same source.

The application gives plain text and explicit spans. A span covers bytes of the text and either makes them a link, when it has a target, or only sets their style. Linktext draws, wraps, scrolls, selects and reports. It does not look for addresses in the text and does not parse a target. It starts no browser, runs no Shell command and makes no network request.

```c
static const struct MUI_LinktextSpan spans[] = {
    { 4, 15, 0, (CONST_STRPTR)"https://example.org/project", 1 },
    { 24, 5, MUIV_Linktext_Span_Bold, NULL, 0 },
};

DoMethod(view, MUIM_Linktext_SetContent,
         (IPTR)"See Project website and today.", ~0UL, (IPTR)spans, 2);
```

### Text and spans

- Text is one byte per character in the system's 8-bit character set, as for `Text()` and Floattext. Offsets count those bytes. Convert UTF-8 first.
- A newline ends a paragraph. Any other control character is shown as a space. No escape codes are interpreted.
- `MUIM_Linktext_SetContent` replaces the content at once or not at all, and returns a status. Spans must lie inside the text, be at least one byte long, come in text order and not overlap, and a link's target must not be empty. Anything else is refused, with the old content kept.
- The class copies the text and the targets during the call. A target is any non-empty text and stays exact: a final `!`, `;`, `?`, `'` or `.` is part of it.
- Limits are explicit: 64 MiB of text, 1000000 spans, 16 MiB per target. Over a limit the status is `MUIV_Linktext_Status_TooLarge`. There is no limit on how many links are shown or on how they are placed.

### Documents: paragraph blocks and quieter text

`MUIM_Linktext_SetDocument` is `SetContent` with paragraph blocks, for a view such as a conversation: messages in a box at the right, code in a filled box, secondary lines quieter. It takes the same text and spans, the same statuses and the same all-or-nothing rule.

- A block covers whole paragraphs: it starts at a paragraph's start and ends at a paragraph's end. Blocks come in text order and do not overlap. Paragraphs outside every block are laid out as before.
- `Indent` and `RightIndent` are in average characters of the normal font, so they follow the user's font. Lines wrap to the block's area.
- `MUIV_Linktext_Block_Right` puts the block's lines together at the right of its area, as wide as its longest line. `MUIV_Linktext_Block_Fill` draws a box behind them in `Background` (an `MPEN_*` value plus one; 0 is `MPEN_HALFSHINE`). An empty paragraph inside a block gives it room above or below.
- Span flags add `MUIV_Linktext_Span_Fixed` (the user's fixed-width font, for code), `MUIV_Linktext_Span_Dim` (a quieter pen for secondary text, chosen against the list background) and `MUIV_Linktext_Span_NoUnderline` (a link that reads as a line of its own, such as a tool line that opens its result).
- `MUIA_Linktext_MaxColumns` limits the text column to a number of average characters and keeps it at the centre of a wider object. Use it for reading width; the object still fills its space.

### Spacing and colour

A conversation reads better with paragraphs closer than a line apart and with room inside its boxes.

- `MUIA_Linktext_EmptyLine` sets the height of an empty paragraph in eighths of a line (1 to 8; 8 is the default, a whole line). With 4, one empty paragraph between paragraphs is half a line, and two around a box are one line.
- `MUIV_Linktext_Block_Space` makes a block's empty paragraphs `PadY` eighths of a line high (1 to 16) instead of `EmptyLine`. A block of one empty paragraph is a gap of its own height: a conversation can keep paragraphs half a line apart, give a heading more room before it than after, and set a box or a new exchange further off, each a separate value.
- `MUIV_Linktext_Span_Large` sets a span in the user's big font, for headings; its line is that much higher. When the big font is not larger than the normal one, the span uses the normal font.
- `MUIV_Linktext_Block_Table` lays a block out as a table: each paragraph a row, its cells separated by TAB, at most 16 columns. Columns are as wide as their widest cell when the table fits; when it does not, each keeps at least its longest word and the rest is shared out, and cells wrap inside their columns. `Align` holds each column's alignment, two bits a column. The class draws the background (`Background` with Fill), the header row (`Head`, with `MUIV_Linktext_Block_Head`) and the lines between rows and columns and around the table (`Rule`). `PadX` is the room at the sides of a cell's text, `PadY` above and below each row. A selection copied from a table gives rows of tab-separated cells. Keep the table's source, such as Markdown, for a copy of the whole reply.
- `MUIV_Linktext_Block_Pad` gives a filled block its own room: `PadX` eighths of an average character at the sides instead of half a character, and `PadY` eighths of a line above and below (at most a whole line). The vertical room is drawn into the paragraphs next to the block, so keep empty paragraphs there, at least as high as `PadY`.
- `Background` also takes `MUIV_Linktext_RGB(0xrrggbb)`. `MUIA_Linktext_TextColor`, `MUIA_Linktext_DimColor`, `MUIA_Linktext_AccentColor` and `MUIA_Linktext_ShadeColor` take the same; 0 keeps the pen from the user's settings. `MUIV_Linktext_Span_Accent` draws a span in the accent colour, and links use it when it is set. `MUIV_Linktext_Span_Shade` puts a span on a box in the shade colour, for inline code.
- The class obtains the nearest pens on the screen when it is set up and releases them at cleanup. Set the object's background to match the colours, for example `MUIA_Background, "2:rffffffff,ffffffff,ffffffff"`: colours chosen for one settings scheme can be unreadable on another.

### Replacing the content

A view that grows while the user reads it, such as a reply that streams in, replaces the whole content each time. The class keeps what the user is doing where it can:

- `First` stays the same line number, within the new line count.
- What lies in the bytes the old and the new text start with stays: the selection, a drag that is making one, a pressed link and the keyboard's link, when it is the same link at the same place with the same target. Text that grows at the end therefore keeps them. A change before them clears them.
- To follow the end, read `First`, `MUIA_Linktext_Visible` and `MUIA_Linktext_LineCount` before replacing: the view is at the end when `First + Visible >= LineCount`. After replacing, set `First` to `~0UL`, which goes to the last page.
- A view that shows its last line keeps showing it when it is made smaller or larger, for example when a panel opens below it.

The class copies the whole text at each replacement but wraps only from the first paragraph that changed in its text or in the font of its spans (bold, fixed width), keeping the lines before it. A reply streaming at the end of a long conversation costs its own paragraphs. In Bishop on ABIv11 2026.09 in QEMU, with a 47 KB conversation, a replacement took 9 ms on average and at most 52 ms (the resume, which wraps everything); wrapping the whole text each time it was 43 ms on average.

With paragraphs of different heights, `First`, `LineCount` and `Visible` still count lines, while the scroller (`MUIA_Linktext_Scroller`) counts pixels: the class sets its entries, visible part and position, and an arrow moves one line.

### When memory runs out

Every replacement is whole or not at all. `SetContent` and `SetDocument` return `MUIV_Linktext_Status_NoMemory` with the old content still shown, its selection and position unchanged. An application that must not act on text the user has not seen checks the status: a permission request, for example, should allow its Allow button only after its own text was set.

### Encoding and fonts

- Offsets count bytes of the 8-bit text the class is given. Convert UTF-8 to the system's character set first, for example with codesets.library, and keep the UTF-8 original for copying and saving: the class copies its own 8-bit text.
- The normal font is the object's (`MUIA_Font`, the list font by default). Bold is that font's bold style. The fixed-width font is the user's fixed font from the MUI settings, opened at `MUIM_Setup` and closed at `MUIM_Cleanup`; when it cannot be opened, fixed spans use the normal font.

### Actions

`MUIA_Linktext_ActionHook` gets a `struct MUI_LinktextAction`: open or copy, the link's number, its ID and its target.

The hook runs on the application's task inside the object's event handling. `Target` stays valid until the hook returns, even if the hook replaces the content, because the class holds a reference to that content for the call. Copy it to keep it. The hook may replace the content and close the window. To dispose the object, push the disposal with `MUIM_Application_PushMethod`.

For Copy, a hook that returns FALSE, or no hook, makes the class copy the target to clipboard unit 0 itself.

### What the user can do

- A click on a link opens it, at any wrapped fragment of the link. A click beside the text does nothing.
- A drag selects text and opens nothing, also when it starts on a link. It scrolls when held past the edge.
- The context menu opens or copies the link under the pointer, or copies the selection.
- A bubble shows the whole target, broken into lines. It appears beside the pointer, not under it, so it does not take the next click.
- With the object active, Left and Right go from link to link and bring the link into view, Return opens, Amiga+C copies the link or the selection, Up, Down and the page keys scroll, and Tab goes on to the next object. Tab cannot walk through the links because Zune's window handles it before any object.

Links use the text pen with an underline, in the user's font. The Mark pen is not used: in stock settings it equals the list background. A selection uses the Fill and Shine pens. The wheel scrolls three lines.

### Gaps

- No word or line selection by double and triple click, and no selection with the keyboard.
- Text is drawn with `Text()`, so there is no shaping and no right-to-left text. Fonts come from the MUI settings.
- The menu's three labels are English.
- Links have one look; visited links are not told apart.

## Status

| What | State |
|---|---|
| Floattext, Urltext, TextEditor | Existing classes, described above |
| `zk_linktext` in zunekit | Implemented; host tests of the text logic; run on mainline x86_64, ABIv11 2026.09 and AROS One 1.3 |
| Blocks, fixed and quieter spans, column limit, kept selection and end (zunekit) | Implemented; host tests; run on AROS One 1.3 in Bishop's window. Not part of the class offered to AROS yet: it needs the AROS build at an exact revision and a run on mainline first |
| PkgManager, Bishop | Use it compiled in; not yet in a release |
| `Linktext.mcc` in AROS | Built through mmake and run on mainline x86_64; offered upstream, not accepted. Attribute numbers are a proposal until then |
| Raspberry Pi | Compiled into PkgManager; not run |
