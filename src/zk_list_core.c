/* SPDX-License-Identifier: MIT */
/* zk_list's pure part: see zk_list_core.h. */
#include "zk_list_core.h"

#define ESC 0x1b

size_t zl_lead(const char *s, size_t n)
{
    size_t i = 0;
    while (i + 1 < n && s[i] == ESC &&
           (s[i + 1] == 'c' || s[i + 1] == 'r' || s[i + 1] == 'l' || s[i + 1] == 'j'))
        i += 2;
    return i;
}

/* The end of the escape at s[i] (s[i] is ESC): past its letter and a
   bracketed argument. */
static size_t esc_end(const char *s, size_t n, size_t i)
{
    i++;                                /* ESC */
    if (i < n) i++;                     /* its letter */
    if (i < n && s[i] == '[') {
        while (i < n && s[i] != ']') i++;
        if (i < n) i++;
    }
    return i;
}

size_t zl_runs(const char *s, size_t n, zl_run *out, size_t max)
{
    size_t i = 0, k = 0, a = 0;
    unsigned fl = 0;
    while (i <= n) {
        if (i == n || s[i] == ESC) {
            if (i > a) {
                if (k < max) { out[k].start = a; out[k].len = i - a; out[k].flags = fl; }
                k++;
            }
            if (i == n) break;
            if (i + 1 < n) {
                switch (s[i + 1]) {
                case 'b': fl |= ZL_BOLD; break;
                case 'i': fl |= ZL_ITALIC; break;
                case 'u': fl |= ZL_UNDERLINE; break;
                case 'n': fl = 0; break;
                default: break;
                }
            }
            i = esc_end(s, n, i);
            a = i;
            continue;
        }
        i++;
    }
    return k;
}

int zl_format_columns(const char *format)
{
    int c = 1;
    if (!format) return 1;
    for (; *format; format++)
        if (*format == ',') c++;
    return c;
}

unsigned long zl_hash(const char *s, size_t n)
{
    unsigned long h = 2166136261UL;
    size_t i;
    for (i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619UL; h &= 0xffffffffUL; }
    return h;
}
