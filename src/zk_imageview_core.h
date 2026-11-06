/* SPDX-License-Identifier: MIT */
/* zk_imageview_core: the zoom and placement arithmetic of zk_imageview,
 * without AROS calls, so it can be tested on the host. Lengths are pixels,
 * zoom values percent. One axis at a time: the view and the picture are
 * placed the same way horizontally and vertically. */
#ifndef ZK_IMAGEVIEW_CORE_H
#define ZK_IMAGEVIEW_CORE_H

#define ZK_IV_ZOOM_MIN 1
#define ZK_IV_ZOOM_MAX 3200

/* The zoom that shows a pw x ph picture whole in a vw x vh view, never above
 * 100 and never below ZK_IV_ZOOM_MIN; 100 for an empty picture or view. */
long zk_iv_fit(long vw, long vh, long pw, long ph);

/* The zoom clamped to ZK_IV_ZOOM_MIN..ZK_IV_ZOOM_MAX. */
long zk_iv_clamp(long zoom);

/* The next zoom step above (dir > 0) or below (dir < 0) shown: 10, 25, 33,
 * 50, 67, 100, 150, 200, 300, 400, 600, 800, 1200, 1600, 3200; beyond the
 * ends the ends. A zoom between steps goes to the neighbouring step. */
long zk_iv_step(long shown, int dir);

/* Length of n picture pixels at shown percent. Below 100 the picture is
 * scaled down by whole box cells (at least 1 pixel); from 100 the last
 * partly visible pixel counts. */
long zk_iv_scaled(long n, long shown);

/* Places a scaled length in a view of length view: a shorter picture is
 * centred and *scroll becomes 0; a longer one is scrolled by *scroll,
 * clamped to 0..scaled-view. Returns where picture point 0 is in the view
 * (the origin), which is <= 0 for a scrolled picture. */
long zk_iv_place(long view, long scaled, long *scroll);

/* The picture pixel at view position v (floor, may be outside the
 * picture); the origin comes from zk_iv_place. */
long zk_iv_to_pic(long v, long origin, long shown);

/* The view position of the left (top) edge of picture pixel p. */
long zk_iv_to_view(long p, long origin, long shown);

/* The scroll after a zoom from old_shown to new_shown that keeps the
 * picture point at view position v where it is. Pass it to zk_iv_place. */
long zk_iv_scroll_about(long v, long origin, long old_shown, long new_shown);

/* The source pixels s0 <= s < s1 that make up the scaled-down pixel d of
 * a length n at shown percent (shown < 100); s1 > s0 always. */
void zk_iv_box(long d, long n, long shown, long *s0, long *s1);

#endif
