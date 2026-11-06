/* SPDX-License-Identifier: MIT */
/* zk_dragout_core: see zk_dragout_core.h. */
#include <stdio.h>
#include <string.h>
#include "zk_dragout_core.h"

int zk_do_join(const char *dir, const char *name, char *out, size_t n)
{
    size_t dl = strlen(dir);
    const char *sep = (dl == 0 || dir[dl - 1] == ':' || dir[dl - 1] == '/') ? "" : "/";
    int r = snprintf(out, n, "%s%s%s", dir, sep, name);
    return r >= 0 && (size_t)r < n;
}

int zk_do_numbered(const char *name, unsigned k, char *out, size_t n)
{
    const char *dot = strrchr(name, '.');
    int r;
    if (k < 2)
        r = snprintf(out, n, "%s", name);
    else if (!dot || dot == name)
        r = snprintf(out, n, "%s-%u", name, k);
    else
        r = snprintf(out, n, "%.*s-%u%s", (int)(dot - name), name, k, dot);
    return r >= 0 && (size_t)r < n;
}

int zk_do_unique(const char *dir, const char *name, int (*exists)(void *ctx, const char *path),
                 void *ctx, char *out, size_t n)
{
    char leaf[256];
    unsigned k;
    for (k = 1; k < 10000; k++) {
        if (!zk_do_numbered(name, k, leaf, sizeof(leaf)) || !zk_do_join(dir, leaf, out, n))
            return 0;
        if (!exists(ctx, out))
            return 1;
    }
    return 0;
}

int zk_do_is_wanderer(const char *task_name)
{
    return task_name && strstr(task_name, "Wanderer") != NULL;
}

int zk_do_is_drawer_title(const char *title)
{
    return title && strchr(title, ':') != NULL;
}

void zk_do_fit_thumb(long w, long h, long max, long *tw, long *th)
{
    long a = w, b = h;
    if (w <= 0 || h <= 0 || max <= 0) {
        *tw = *th = 1;
        return;
    }
    if (a > max || b > max) {
        if (w >= h) {
            a = max;
            b = (long)((long long)h * max / w);
        } else {
            b = max;
            a = (long)((long long)w * max / h);
        }
    }
    *tw = a < 1 ? 1 : a;
    *th = b < 1 ? 1 : b;
}
