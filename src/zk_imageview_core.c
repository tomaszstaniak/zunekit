/* SPDX-License-Identifier: MIT */
/* zk_imageview_core: see zk_imageview_core.h. Products are taken in long
 * long: a 32767-pixel picture at 3200 % does not fit 32 bits. */
#include "zk_imageview_core.h"

static const long steps[] = { 10, 25, 33, 50, 67, 100, 150, 200, 300, 400, 600, 800,
                              1200, 1600, 3200 };
#define NSTEPS ((int)(sizeof(steps) / sizeof(steps[0])))

/* Floor division for a positive divisor. */
static long long fdiv(long long a, long long b)
{
    long long q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

long zk_iv_clamp(long zoom)
{
    return zoom < ZK_IV_ZOOM_MIN ? ZK_IV_ZOOM_MIN : zoom > ZK_IV_ZOOM_MAX ? ZK_IV_ZOOM_MAX : zoom;
}

long zk_iv_fit(long vw, long vh, long pw, long ph)
{
    long long zx, zy, z;
    if (vw <= 0 || vh <= 0 || pw <= 0 || ph <= 0)
        return 100;
    zx = (long long)vw * 100 / pw;
    zy = (long long)vh * 100 / ph;
    z = zx < zy ? zx : zy;
    if (z > 100)
        z = 100;
    return zk_iv_clamp((long)z);
}

long zk_iv_step(long shown, int dir)
{
    int i;
    if (dir > 0) {
        for (i = 0; i < NSTEPS; i++)
            if (steps[i] > shown)
                return steps[i];
        return steps[NSTEPS - 1];
    }
    for (i = NSTEPS - 1; i >= 0; i--)
        if (steps[i] < shown)
            return steps[i];
    return steps[0];
}

long zk_iv_scaled(long n, long shown)
{
    long long s;
    if (n <= 0 || shown <= 0)
        return 0;
    if (shown < 100) {
        s = (long long)n * shown / 100;
        return s < 1 ? 1 : (long)s;
    }
    return (long)(((long long)n * shown + 99) / 100);
}

long zk_iv_place(long view, long scaled, long *scroll)
{
    if (scaled <= view) {
        *scroll = 0;
        return (view - scaled) / 2;
    }
    if (*scroll < 0)
        *scroll = 0;
    if (*scroll > scaled - view)
        *scroll = scaled - view;
    return -*scroll;
}

long zk_iv_to_pic(long v, long origin, long shown)
{
    return (long)fdiv((long long)(v - origin) * 100, shown);
}

long zk_iv_to_view(long p, long origin, long shown)
{
    return origin + (long)-fdiv(-(long long)p * shown, 100);
}

long zk_iv_scroll_about(long v, long origin, long old_shown, long new_shown)
{
    long long rel = (long long)(v - origin) * new_shown;
    long long o = v - fdiv(rel + old_shown / 2, old_shown);
    return (long)-o;
}

void zk_iv_box(long d, long n, long shown, long *s0, long *s1)
{
    long last = zk_iv_scaled(n, shown) - 1;
    long a = (long)((long long)d * 100 / shown);
    long b = d >= last ? n : (long)((long long)(d + 1) * 100 / shown);
    if (a >= n)
        a = n - 1;
    if (b > n)
        b = n;
    if (b <= a)
        b = a + 1;
    *s0 = a;
    *s1 = b;
}
