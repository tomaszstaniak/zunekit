/* SPDX-License-Identifier: MIT */
/* zk_present: a picture with per-pixel alpha as a window of its own shape
 * (a dock, a launcher panel, a popup), on systems with and without window
 * composition.
 *
 * The program draws its whole window in memory as A,R,G,B bytes with
 * straight (not premultiplied) alpha and hands the picture over. Three
 * ways it reaches the screen:
 *
 *   - composed: the window has an alpha surface and the system's
 *     compositor lays it over what is behind it (mainline AROS from
 *     2026-10-24, on screens that can compose). Smooth edges, shadows and
 *     see-through parts are real, and the window takes input only where
 *     its alpha is not 0.
 *   - pictured: an ordinary rectangular window; the picture is blended
 *     over a picture of the screen taken before the window opened
 *     (zk_glass_capture). It looks the same until something behind the
 *     window changes, so it suits windows that live a short time.
 *   - shaped: an ordinary window cut to the picture's outline with
 *     WA_ShapeRegion (zk_present_region); edges are whole pixels.
 *
 * zk_present_compose.c is built where the SDK has the compositor's window
 * tags (WA_AlphaSurface in intuition/intuition.h), zk_present_none.c
 * elsewhere; zk_present.c always. Link with -lcybergraphics. */
#ifndef ZK_PRESENT_H
#define ZK_PRESENT_H

#include <exec/types.h>
#include <graphics/regions.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>

/* The screen composes windows with per-pixel alpha. 0 where this build
 * has no compositor support. */
int zk_present_can_compose(struct Screen *screen);

/* OpenWindowTagList(NULL, tags), asking for an alpha surface when compose
 * is set. The system may still give an ordinary window;
 * zk_present_composed() tells. */
struct Window *zk_present_open(int compose, struct TagItem *tags);

/* The window has its own alpha surface now. Ask after opening and after
 * every size change. */
int zk_present_composed(struct Window *window);

/* The outline of a picture as a region for WA_ShapeRegion: the pixels
 * whose alpha is at least 128. NULL when out of memory. The caller frees
 * it with DisposeRegion() after the window is closed. Changing the shape
 * of an open window is not safe on every system; open with it instead. */
struct Region *zk_present_region(const unsigned char *argb, int w, int h);

/* Puts the picture (w x h, the window's inner size) on the window. On a
 * composed window the pixels go out with their alpha. Otherwise they are
 * blended over `under` (w x h A,R,G,B bytes, what was on the screen here)
 * or, when under is NULL, over black. 0 when out of memory or when the
 * system did not take every pixel. */
int zk_present_put(struct Window *window, const unsigned char *argb, int w, int h,
                   const unsigned char *under);

/* Reads w x h pixels of the screen at x, y into argb (A,R,G,B bytes,
 * alpha set to 255). 0 when not every pixel came back; argb is then not
 * to be used. */
int zk_glass_capture(struct Screen *screen, int x, int y, int w, int h, unsigned char *argb);

#endif
