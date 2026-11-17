/* SPDX-License-Identifier: MIT */
#include "zk_glass_core.h"

#include <stdlib.h>

void zk_glass_blur(unsigned char *p, int w, int h, int radius)
{
    unsigned char *line;
    int pass, axis, l, c, k;
    if (radius <= 0 || w <= 0 || h <= 0) return;
    if (!(line = malloc((size_t)(w > h ? w : h) * 4))) return;
    for (pass = 0; pass < 2; pass++)
        for (axis = 0; axis < 2; axis++) {
            int n = axis ? h : w, lines = axis ? w : h;
            for (l = 0; l < lines; l++) {
#define AT(i) p[4 * (size_t)(axis ? (i) * w + l : l * w + (i)) + (size_t)c]
                for (c = 1; c < 4; c++) {
                    int sum = 0, count = 0;
                    for (k = 0; k <= radius && k < n; k++) { sum += AT(k); count++; }
                    for (k = 0; k < n; k++) {
                        int out = k - radius, in = k + radius + 1;
                        line[4 * k + c] = (unsigned char)((sum + count / 2) / count);
                        if (out >= 0) { sum -= AT(out); count--; }
                        if (in < n) { sum += AT(in); count++; }
                    }
                }
                for (k = 0; k < n; k++)
                    for (c = 1; c < 4; c++) AT(k) = line[4 * k + c];
#undef AT
            }
        }
    free(line);
}
