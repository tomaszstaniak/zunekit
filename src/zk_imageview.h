/* SPDX-License-Identifier: MIT */
/* zk_imageview: a picture in a Zune window, at any zoom, with panning, the
 * pixel under the pointer and a layer on top for the program's own drawing
 * (annotations, a selection, a ruler).
 *
 *   - the picture is 32-bit ARGB in memory (four bytes A, R, G, B per pixel,
 *     as RECTFMT_ARGB); the view does not copy it: the program keeps it
 *     until it sets another one or disposes the view, and calls
 *     zk_imageview_changed() after changing pixels;
 *   - zoom 0 fits the picture into the view (never above 100 %); other
 *     values are percent. The wheel scrolls, Ctrl+wheel zooms around the
 *     pointer, Space+drag and the middle button pan, keys 0 (fit), 1 (100 %),
 *     + and - zoom while the view is active;
 *   - the area around the picture is drawn in a background colour of the
 *     program's choice (dark behind screenshots);
 *   - drawing goes through an off-screen bitmap, so redraws do not flicker.
 *
 * Coordinates given to the program are picture pixels; -1 when the pointer
 * is outside the picture. */
#ifndef ZK_IMAGEVIEW_H
#define ZK_IMAGEVIEW_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <intuition/classusr.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>

#ifndef MUIB_AROS
#define MUIB_AROS (MUIB_RSVD | 0x00070000)
#endif
#define MUIB_ZkImageView (MUIB_AROS | 0x00000E00)

/* [.SGN] LONG: zoom in percent, 0 = fit. */
#define MUIA_ZkImageView_Zoom       (MUIB_ZkImageView | 0x01)
/* [..GN] LONG: zoom actually shown in percent (resolves fit). */
#define MUIA_ZkImageView_Shown      (MUIB_ZkImageView | 0x02)
/* [..GN] LONG: picture pixel under the pointer, -1 outside. Notifies on
 * every change of either, pointer moves only (no button needed). */
#define MUIA_ZkImageView_PointerX   (MUIB_ZkImageView | 0x03)
#define MUIA_ZkImageView_PointerY   (MUIB_ZkImageView | 0x04)
/* [IS..] ULONG 0x00RRGGBB: colour around the picture. Default 0x00202020. */
#define MUIA_ZkImageView_Background (MUIB_ZkImageView | 0x05)
/* [IS..] struct Hook *: the program's layer, see zk_imageview_draw_msg. */
#define MUIA_ZkImageView_DrawHook   (MUIB_ZkImageView | 0x06)
/* [IS..] struct Hook *: pointer input for tools, see zk_imageview_input_msg. */
#define MUIA_ZkImageView_InputHook  (MUIB_ZkImageView | 0x07)

/* DrawHook: called after the picture is drawn into the off-screen bitmap.
 * Picture point (px, py) is at view point (left + px * scale, top + py *
 * scale) with scale = shown / 100.0; clip is the part being redrawn, in
 * view coordinates. Draw only inside it. Return value unused. */
struct zk_imageview_draw_msg
{
    struct RastPort *rp;
    LONG left, top;           /* where picture point 0,0 is */
    LONG shown;               /* zoom in percent */
    struct Rectangle clip;
};

enum zk_imageview_event
{
    ZK_IV_PRESS,              /* left button down */
    ZK_IV_MOVE,               /* pointer moved (with or without the button) */
    ZK_IV_RELEASE,            /* left button up */
    ZK_IV_CANCEL              /* Esc or right button during a press */
};

/* InputHook: picture coordinates, not clamped (a drag may leave the
 * picture). Return nonzero when the program used the event; the view then
 * does not pan. Not called while Space is held or for the middle button. */
struct zk_imageview_input_msg
{
    ULONG event;              /* enum zk_imageview_event */
    LONG x, y;
    UWORD qualifier;          /* IEQUALIFIER_* at the time */
};

int zk_imageview_init(void);
void zk_imageview_exit(void);

Object *zk_imageview_new(const struct TagItem *extra);

/* Shows pixels (w x h ARGB, not copied; NULL for none). */
void zk_imageview_set_image(Object *view, UBYTE *pixels, LONG w, LONG h);
/* Redraws the picture rectangle x, y, w, h (picture pixels) including the
 * program's layer; w or h 0 means all. */
void zk_imageview_changed(Object *view, LONG x, LONG y, LONG w, LONG h);
/* The ARGB value of a picture pixel, 0 outside. */
ULONG zk_imageview_pixel(Object *view, LONG x, LONG y);

#endif
