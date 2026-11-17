/* SPDX-License-Identifier: MIT */
/* zk_glass core: the blur behind frosted glass, without AROS. */
#ifndef ZK_GLASS_CORE_H
#define ZK_GLASS_CORE_H

/* Blurs the colours of a picture in place: w x h pixels of four bytes
 * (A,R,G,B); alpha is left as it is. A box blur over 2*radius+1 pixels,
 * rows then columns, run twice, which is close enough to soft glass.
 * Radius 0 changes nothing. Without memory for one line the picture stays
 * as it was. */
void zk_glass_blur(unsigned char *argb, int w, int h, int radius);

#endif
