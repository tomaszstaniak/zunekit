/* zk_glass core: the blur. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "zk_glass_core.h"

enum { W = 31, H = 23 };

int main(void)
{
    unsigned char *p = malloc(W * H * 4), *q = malloc(W * H * 4);
    int i, x, y, c;
    long sum;

    /* a uniform picture stays uniform, alpha untouched */
    for (i = 0; i < W * H; i++) { p[i * 4] = (unsigned char)i; p[i * 4 + 1] = 10; p[i * 4 + 2] = 200; p[i * 4 + 3] = 77; }
    zk_glass_blur(p, W, H, 5);
    for (i = 0; i < W * H; i++)
        assert(p[i * 4] == (unsigned char)i && p[i * 4 + 1] == 10 && p[i * 4 + 2] == 200 && p[i * 4 + 3] == 77);

    /* radius 0 changes nothing */
    for (i = 0; i < W * H * 4; i++) p[i] = (unsigned char)(i * 7);
    memcpy(q, p, W * H * 4);
    zk_glass_blur(p, W, H, 0);
    assert(!memcmp(p, q, W * H * 4));

    /* one bright pixel in the middle spreads evenly to both sides and
       keeps (within rounding) its light */
    memset(p, 0, W * H * 4);
    for (c = 1; c < 4; c++) p[(11 * W + 15) * 4 + c] = 255;
    zk_glass_blur(p, W, H, 3);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            assert(p[(y * W + x) * 4 + 1] == p[(y * W + (30 - x)) * 4 + 1]);
            assert(p[(y * W + x) * 4 + 1] == p[((22 - y) * W + x) * 4 + 1]);
        }
    assert(p[(11 * W + 15) * 4 + 1] > 0 && p[(11 * W + 15) * 4 + 1] < 255);
    assert(p[(11 * W + 15) * 4 + 1] >= p[(11 * W + 18) * 4 + 1]);
    assert(p[(0 * W + 0) * 4 + 1] == 0);       /* out of reach */
    sum = 0;
    for (i = 0; i < W * H; i++) sum += p[i * 4 + 1];
    assert(sum <= 255 + W * H / 2);            /* nothing is added */

    /* a radius larger than the picture, and a one-pixel picture */
    zk_glass_blur(p, W, H, 200);
    p[0] = 1; p[1] = 2; p[2] = 3; p[3] = 4;
    zk_glass_blur(p, 1, 1, 4);
    assert(p[1] == 2 && p[2] == 3 && p[3] == 4);

    free(p); free(q);
    return 0;
}
