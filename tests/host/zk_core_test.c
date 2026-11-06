/* SPDX-License-Identifier: MIT */
/* The pure logic of zk_imageview (zoom, fit, placement) and zk_dragout
 * (names, thumbnail size) on the host.
 *   cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc \
 *      tests/host/zk_core_test.c src/zk_imageview_core.c src/zk_dragout_core.c \
 *      -o build/host/zk-core-test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zk_imageview_core.h"
#include "zk_dragout_core.h"

static int checks;
#define OK(x) do { checks++; if (!(x)) { printf("FAILED line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void fit(void)
{
    OK(zk_iv_fit(960, 540, 1920, 1080) == 50);
    OK(zk_iv_fit(1000, 1000, 1920, 1080) == 52);      /* width decides */
    OK(zk_iv_fit(800, 300, 1920, 1080) == 27);        /* height decides */
    OK(zk_iv_fit(4000, 4000, 640, 480) == 100);       /* never above 100 */
    OK(zk_iv_fit(10, 10, 100000, 100000) == ZK_IV_ZOOM_MIN);
    OK(zk_iv_fit(0, 100, 10, 10) == 100);
    OK(zk_iv_fit(100, 100, 0, 10) == 100);
    /* The fitted picture really fits. */
    {
        long vw, vh;
        for (vw = 50; vw < 2000; vw += 37)
            for (vh = 50; vh < 1200; vh += 41) {
                long z = zk_iv_fit(vw, vh, 1920, 1080);
                OK(zk_iv_scaled(1920, z) <= vw || z == ZK_IV_ZOOM_MIN);
                OK(zk_iv_scaled(1080, z) <= vh || z == ZK_IV_ZOOM_MIN);
            }
    }
}

static void steps(void)
{
    OK(zk_iv_step(100, 1) == 150);
    OK(zk_iv_step(100, -1) == 67);
    OK(zk_iv_step(52, 1) == 67);
    OK(zk_iv_step(52, -1) == 50);
    OK(zk_iv_step(3200, 1) == 3200);
    OK(zk_iv_step(10, -1) == 10);
    OK(zk_iv_step(5, -1) == 10);
    OK(zk_iv_step(5, 1) == 10);
    OK(zk_iv_clamp(0) == ZK_IV_ZOOM_MIN && zk_iv_clamp(99999) == ZK_IV_ZOOM_MAX);
}

static void scaling(void)
{
    long n, z, d, s0, s1, prev;
    OK(zk_iv_scaled(1920, 50) == 960);
    OK(zk_iv_scaled(1921, 50) == 960);
    OK(zk_iv_scaled(3, 10) == 1);
    OK(zk_iv_scaled(3, 150) == 5);                    /* 4.5 -> last half pixel shown */
    OK(zk_iv_scaled(100, 100) == 100);
    OK(zk_iv_scaled(0, 100) == 0);
    /* Boxes cover the source exactly once, in order. */
    for (n = 1; n < 300; n += 7)
        for (z = 1; z < 100; z += 3) {
            long sc = zk_iv_scaled(n, z);
            prev = 0;
            for (d = 0; d < sc; d++) {
                zk_iv_box(d, n, z, &s0, &s1);
                OK(s0 == prev && s1 > s0 && s1 <= n);
                prev = s1;
            }
            OK(prev == n);
        }
    /* Every view position inside the scaled picture maps into the picture. */
    for (n = 1; n < 200; n += 13)
        for (z = 1; z <= 1600; z += 29) {
            long sc = zk_iv_scaled(n, z), v;
            for (v = 0; v < sc; v++) {
                long p = zk_iv_to_pic(v, 0, z);
                OK(p >= 0 && p < n);
            }
            OK(zk_iv_to_pic(-1, 0, z) < 0);
            OK(zk_iv_to_pic(sc, 0, z) >= n || z < 100);
        }
}

static void placement(void)
{
    long s;
    s = 50;
    OK(zk_iv_place(800, 600, &s) == 100 && s == 0);  /* centred */
    s = -20;
    OK(zk_iv_place(800, 1000, &s) == 0 && s == 0);
    s = 150;
    OK(zk_iv_place(800, 1000, &s) == -150 && s == 150);
    s = 500;
    OK(zk_iv_place(800, 1000, &s) == -200 && s == 200);
    /* Pixel edges. */
    OK(zk_iv_to_view(0, -150, 200) == -150);
    OK(zk_iv_to_view(1, 0, 150) == 2);                /* 1.5 -> 2 */
    OK(zk_iv_to_pic(-150 + 3, -150, 200) == 1);
    OK(zk_iv_to_pic(9, 10, 100) == -1);
    OK(zk_iv_to_pic(-1, 0, 50) == -2);
    /* Zoom about a point: the picture point under it stays. */
    {
        long z0, z1, v, o;
        for (z0 = 10; z0 <= 1600; z0 = zk_iv_step(z0, 1))
            for (z1 = 10; z1 <= 1600; z1 = zk_iv_step(z1, 1)) {
                for (v = 0; v < 800; v += 97) {
                    long p, sc, o1, sc1;
                    o = -123;
                    p = zk_iv_to_pic(v, o, z0);
                    s = zk_iv_scroll_about(v, o, z0, z1);
                    sc1 = zk_iv_scaled(100000, z1);
                    o1 = zk_iv_place(800, sc1, &s);
                    sc = zk_iv_to_pic(v, o1, z1);
                    /* within a view pixel at either zoom */
                    OK(labs(sc - p) <= 100 / z0 + 100 / z1 + 1 || o1 == 0);
                }
                if (z1 == 1600)
                    break;
            }
    }
}

static const char *taken[] = { "Work:Pics/shot.png", "Work:Pics/shot-2.png", "RAM:notes",
                               "RAM:.config", NULL };

static int exists(void *ctx, const char *path)
{
    int i;
    (void)ctx;
    for (i = 0; taken[i]; i++)
        if (!strcmp(taken[i], path))
            return 1;
    return 0;
}

static int always(void *ctx, const char *path)
{
    (void)ctx;
    (void)path;
    return 1;
}

static void names(void)
{
    char b[64], tiny[6];
    long w, h;
    OK(zk_do_join("Work:Pics", "a.png", b, sizeof(b)) && !strcmp(b, "Work:Pics/a.png"));
    OK(zk_do_join("Work:", "a.png", b, sizeof(b)) && !strcmp(b, "Work:a.png"));
    OK(zk_do_join("T:x/", "a", b, sizeof(b)) && !strcmp(b, "T:x/a"));
    OK(zk_do_join("", "a", b, sizeof(b)) && !strcmp(b, "a"));
    OK(!zk_do_join("Work:", "abcdef", tiny, sizeof(tiny)));
    OK(zk_do_numbered("shot.png", 1, b, sizeof(b)) && !strcmp(b, "shot.png"));
    OK(zk_do_numbered("shot.png", 2, b, sizeof(b)) && !strcmp(b, "shot-2.png"));
    OK(zk_do_numbered("a.tar.gz", 3, b, sizeof(b)) && !strcmp(b, "a.tar-3.gz"));
    OK(zk_do_numbered("notes", 2, b, sizeof(b)) && !strcmp(b, "notes-2"));
    OK(zk_do_numbered(".config", 2, b, sizeof(b)) && !strcmp(b, ".config-2"));
    OK(zk_do_numbered("x.", 12, b, sizeof(b)) && !strcmp(b, "x-12."));
    OK(!zk_do_numbered("shot.png", 2, tiny, sizeof(tiny)));
    OK(zk_do_unique("Work:Pics", "shot.png", exists, NULL, b, sizeof(b)) &&
       !strcmp(b, "Work:Pics/shot-3.png"));
    OK(zk_do_unique("Work:Pics", "other.png", exists, NULL, b, sizeof(b)) &&
       !strcmp(b, "Work:Pics/other.png"));
    OK(zk_do_unique("RAM:", "notes", exists, NULL, b, sizeof(b)) && !strcmp(b, "RAM:notes-2"));
    OK(zk_do_unique("RAM:", ".config", exists, NULL, b, sizeof(b)) && !strcmp(b, "RAM:.config-2"));
    OK(!zk_do_unique("RAM:", "a", always, NULL, b, sizeof(b)));
    OK(!zk_do_unique("RAM:", "long-name", exists, NULL, tiny, sizeof(tiny)));
    OK(zk_do_is_wanderer("Wanderer") && zk_do_is_wanderer("Wanderer handler"));
    OK(!zk_do_is_wanderer("Dockbar") && !zk_do_is_wanderer(NULL));
    OK(zk_do_is_drawer_title("Work:Pictures") && !zk_do_is_drawer_title("Wanderer") &&
       !zk_do_is_drawer_title(NULL));
    zk_do_fit_thumb(1920, 1080, 160, &w, &h);
    OK(w == 160 && h == 90);
    zk_do_fit_thumb(100, 400, 160, &w, &h);
    OK(w == 40 && h == 160);
    zk_do_fit_thumb(64, 48, 160, &w, &h);
    OK(w == 64 && h == 48);
    zk_do_fit_thumb(10000, 1, 160, &w, &h);
    OK(w == 160 && h == 1);
    zk_do_fit_thumb(0, 5, 160, &w, &h);
    OK(w == 1 && h == 1);
}

int main(void)
{
    fit();
    steps();
    scaling();
    placement();
    names();
    printf("zk core: %d checks passed\n", checks);
    return 0;
}
