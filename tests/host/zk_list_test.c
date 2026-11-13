/* SPDX-License-Identifier: MIT */
/* Host tests of zk_list's pure part (src/zk_list_core.c). */
#include <stdio.h>
#include <string.h>
#include "zk_list_core.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static int run_is(const char *s, const zl_run *r, const char *text, unsigned fl)
{
    return r->len == strlen(text) && !memcmp(s + r->start, text, r->len) && r->flags == fl;
}

int main(void)
{
    zl_run r[8];
    const char *s;
    size_t k;

    /* alignment kept by NList */
    CHECK(zl_lead("\033cabc", 5) == 2);
    CHECK(zl_lead("\033r\033cx", 5) == 4);
    CHECK(zl_lead("\033babc", 5) == 0);
    CHECK(zl_lead("abc", 3) == 0);
    CHECK(zl_lead("\033", 1) == 0);

    /* plain UTF-8: one run */
    s = "Zażółć gęślą jaźń";
    k = zl_runs(s, strlen(s), r, 8);
    CHECK(k == 1 && run_is(s, &r[0], s, 0));

    /* styles on and off */
    s = "a\033bb\033ic\033nd";
    k = zl_runs(s, strlen(s), r, 8);
    CHECK(k == 4);
    CHECK(run_is(s, &r[0], "a", 0));
    CHECK(run_is(s, &r[1], "b", ZL_BOLD));
    CHECK(run_is(s, &r[2], "c", ZL_BOLD | ZL_ITALIC));
    CHECK(run_is(s, &r[3], "d", 0));

    /* other escapes and their arguments left out */
    s = "\033P[2]x\033I[6:12]y\033uz";
    k = zl_runs(s, strlen(s), r, 8);
    CHECK(k == 3);
    CHECK(run_is(s, &r[0], "x", 0));
    CHECK(run_is(s, &r[1], "y", 0));
    CHECK(run_is(s, &r[2], "z", ZL_UNDERLINE));

    /* escapes only, a cut escape at the end, an unclosed argument */
    CHECK(zl_runs("\033b\033n", 4, r, 8) == 0);
    CHECK(zl_runs("ab\033", 3, r, 8) == 1 && r[0].len == 2);
    CHECK(zl_runs("ab\033P[2", 6, r, 8) == 1 && r[0].len == 2);
    CHECK(zl_runs("", 0, r, 8) == 0);

    /* more runs than room: counted, not written past max */
    s = "a\033bb\033nc\033bd";
    memset(r, 0x5a, sizeof r);
    k = zl_runs(s, strlen(s), r, 2);
    CHECK(k == 4);
    CHECK(run_is(s, &r[1], "b", ZL_BOLD));
    CHECK(r[2].start == (size_t)0x5a5a5a5a5a5a5a5aULL);

    /* columns */
    CHECK(zl_format_columns(NULL) == 1);
    CHECK(zl_format_columns("") == 1);
    CHECK(zl_format_columns("BAR,") == 2);
    CHECK(zl_format_columns("BAR,P=\033r BAR,") == 3);

    /* hash: stable and different for different text */
    CHECK(zl_hash("abc", 3) == zl_hash("abc", 3));
    CHECK(zl_hash("abc", 3) != zl_hash("abd", 3));
    CHECK(zl_hash("", 0) == 2166136261UL);

    printf("zk_list core: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
