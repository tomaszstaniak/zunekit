/* SPDX-License-Identifier: MIT */
/* zk_resample: scaling a picture with alpha, without AROS. */
#ifndef ZK_RESAMPLE_H
#define ZK_RESAMPLE_H
/* Straight A,R,G,B bytes. Input is immutable, tightly packed. On success output
 * is a new allocation released with free(). On failure output is NULL.
 * Source dimensions: 1..1024. Output dimensions: 1..512.
 * Lanczos-3, widened on reduction, filters premultiplied linear-light colour.
 * Call while loading icons, never from a repaint. Not thread-safe (lazy LUT). */
int zk_resample_argb(const unsigned char *source, int source_width,
                      int source_height, int width, int height,
                      unsigned char **output);
#endif
