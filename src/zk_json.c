/* SPDX-License-Identifier: MIT */
#include "zk_json.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Parser {
  struct ZkJson *j;
  size_t p;
};
static void ws(struct Parser *p) {
  while (p->p < p->j->length && strchr(" \t\r\n", p->j->text[p->p]))
    p->p++;
}
static int hex4(const char *s, unsigned *v) {
  *v = 0;
  for (int i = 0; i < 4; i++) {
    unsigned char c = (unsigned char)s[i];
    int n = c >= '0' && c <= '9'   ? c - '0'
            : c >= 'a' && c <= 'f' ? c - 'a' + 10
            : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                   : -1;
    if (n < 0)
      return 0;
    *v = *v * 16 + (unsigned)n;
  }
  return 1;
}
static int utf8(char *out, size_t cap, size_t *n, unsigned u) {
  unsigned char b[4];
  int len;
  if (u < 128) {
    b[0] = (unsigned char)u;
    len = 1;
  } else if (u < 2048) {
    b[0] = 192 | (u >> 6);
    b[1] = 128 | (u & 63);
    len = 2;
  } else if (u < 65536) {
    b[0] = 224 | (u >> 12);
    b[1] = 128 | ((u >> 6) & 63);
    b[2] = 128 | (u & 63);
    len = 3;
  } else {
    b[0] = 240 | (u >> 18);
    b[1] = 128 | ((u >> 12) & 63);
    b[2] = 128 | ((u >> 6) & 63);
    b[3] = 128 | (u & 63);
    len = 4;
  }
  if (*n + (size_t)len >= cap)
    return 0;
  for (int i = 0; i < len; i++)
    out[(*n)++] = (char)b[i];
  return 1;
}
int zk_json_string(const struct ZkJson *j, int t, char *out, size_t cap) {
  if (!j || t < 0 || t >= j->count || j->token[t].type != DJ_STRING || !out ||
      !cap)
    return 0;
  size_t n = 0;
  int end = j->token[t].end;
  for (int i = j->token[t].start; i < end; i++) {
    unsigned char c = (unsigned char)j->text[i];
    if (c == '\\') {
      if (++i >= end)
        return 0;
      c = (unsigned char)j->text[i];
      if (c == 'u') {
        unsigned u;
        if (i + 4 >= end || !hex4(j->text + i + 1, &u))
          return 0;
        i += 4;
        if (u >= 0xd800 && u <= 0xdbff) {
          unsigned lo;
          if (i + 6 >= end || j->text[i + 1] != '\\' || j->text[i + 2] != 'u' ||
              !hex4(j->text + i + 3, &lo) || lo < 0xdc00 || lo > 0xdfff)
            return 0;
          u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
          i += 6;
        } else if (u >= 0xdc00 && u <= 0xdfff)
          return 0;
        if (!u || !utf8(out, cap, &n, u))
          return 0;
        continue;
      }
      switch (c) {
      case '"':
      case '\\':
      case '/':
        break;
      case 'b':
        c = '\b';
        break;
      case 'f':
        c = '\f';
        break;
      case 'n':
        c = '\n';
        break;
      case 'r':
        c = '\r';
        break;
      case 't':
        c = '\t';
        break;
      default:
        return 0;
      }
    }
    if (n + 1 >= cap)
      return 0;
    out[n++] = (char)c;
  }
  out[n] = 0;
  return 1;
}
int zk_json_number(const struct ZkJson *j, int t, double *v) {
  if (!j || t < 0 || t >= j->count || j->token[t].type != DJ_NUMBER)
    return 0;
  char b[80], *end;
  int n = j->token[t].end - j->token[t].start;
  if (n <= 0 || n >= (int)sizeof b)
    return 0;
  memcpy(b, j->text + j->token[t].start, (size_t)n);
  b[n] = 0;
  double x = strtod(b, &end);
  if (*end || !isfinite(x))
    return 0;
  *v = x;
  return 1;
}
int zk_json_int(const struct ZkJson *j, int t, int *v) {
  double d;
  if (!zk_json_number(j, t, &d) || d < INT_MIN || d > INT_MAX || d != (int)d)
    return 0;
  *v = (int)d;
  return 1;
}
int zk_json_is(const struct ZkJson *j, int t, const char *s) {
  char b[256];
  return zk_json_string(j, t, b, sizeof b) && !strcmp(b, s);
}
static int value(struct Parser *p, int depth) {
  struct ZkJson *j = p->j;
  ws(p);
  if (depth > 16 || p->p >= j->length || j->count == ZK_JSON_TOKENS)
    return -1;
  int t = j->count++;
  struct ZkJsonToken *k = &j->token[t];
  memset(k, 0, sizeof *k);
  k->start = (int)p->p;
  char c = j->text[p->p++];
  if (c == '{' || c == '[') {
    k->type = c == '{' ? DJ_OBJECT : DJ_ARRAY;
    ws(p);
    char close = c == '{' ? '}' : ']';
    if (p->p < j->length && j->text[p->p] == close) {
      p->p++;
      goto done;
    }
    for (;;) {
      if (c == '{') {
        ws(p);
        if (p->p >= j->length || j->text[p->p] != '"')
          return -1;
        int key = value(p, depth + 1);
        if (key < 0)
          return -1;
        char a[256], b[256];
        if (!zk_json_string(j, key, a, sizeof a))
          return -1;
        for (int prev = t + 1; prev < key;
             prev = j->token[j->token[prev].next].next) {
          if (!zk_json_string(j, prev, b, sizeof b) || !strcmp(a, b))
            return -1;
        }
        ws(p);
        if (p->p >= j->length || j->text[p->p++] != ':')
          return -1;
      }
      if (value(p, depth + 1) < 0)
        return -1;
      k->count++;
      ws(p);
      if (p->p >= j->length)
        return -1;
      char sep = j->text[p->p++];
      if (sep == close)
        break;
      if (sep != ',')
        return -1;
    }
  } else if (c == '"') {
    k->type = DJ_STRING;
    k->start = (int)p->p;
    int closed = 0;
    while (p->p < j->length) {
      unsigned char ch = (unsigned char)j->text[p->p++];
      if (ch == '"') {
        closed = 1;
        break;
      }
      if (ch < 32)
        return -1;
      if (ch == '\\') {
        if (p->p >= j->length)
          return -1;
        char e = j->text[p->p++];
        if (e == 'u') {
          if (p->p + 4 > j->length)
            return -1;
          unsigned u;
          if (!hex4(j->text + p->p, &u))
            return -1;
          p->p += 4;
        } else if (!strchr("\"\\/bfnrt", e))
          return -1;
      }
    }
    if (!closed)
      return -1;
    k->end = (int)p->p - 1;
    k->next = j->count;
    /* Decode once to reject invalid surrogate pairs even for unused fields. */
    size_t cap = (size_t)(k->end - k->start) + 1;
    char *b = malloc(cap);
    if (!b)
      return -1;
    int ok = zk_json_string(j, t, b, cap);
    free(b);
    return ok ? t : -1;
  } else if (c == 't' || c == 'f' || c == 'n') {
    const char *lit = c == 't' ? "true" : c == 'f' ? "false" : "null";
    size_t n = strlen(lit);
    if ((size_t)k->start + n > j->length || memcmp(j->text + k->start, lit, n))
      return -1;
    p->p = (size_t)k->start + n;
    k->type = c == 'n' ? DJ_NULL : DJ_BOOL;
  } else {
    k->type = DJ_NUMBER;
    p->p = (size_t)k->start;
    if (j->text[p->p] == '-')
      p->p++;
    if (p->p >= j->length)
      return -1;
    if (j->text[p->p] == '0')
      p->p++;
    else {
      if (j->text[p->p] < '1' || j->text[p->p] > '9')
        return -1;
      while (p->p < j->length && isdigit((unsigned char)j->text[p->p]))
        p->p++;
    }
    if (p->p < j->length && j->text[p->p] == '.') {
      p->p++;
      size_t first = p->p;
      while (p->p < j->length && isdigit((unsigned char)j->text[p->p]))
        p->p++;
      if (first == p->p)
        return -1;
    }
    if (p->p < j->length && (j->text[p->p] == 'e' || j->text[p->p] == 'E')) {
      p->p++;
      if (p->p < j->length && (j->text[p->p] == '+' || j->text[p->p] == '-'))
        p->p++;
      size_t first = p->p;
      while (p->p < j->length && isdigit((unsigned char)j->text[p->p]))
        p->p++;
      if (first == p->p)
        return -1;
    }
    k->end = (int)p->p;
    double d;
    if (!zk_json_number(j, t, &d))
      return -1;
  }
done:
  k->end = (int)p->p;
  k->next = j->count;
  return t;
}
int zk_json_parse(struct ZkJson *j, const char *text, size_t len) {
  if (!j || !text || !len || len > ZK_JSON_LIMIT || memchr(text, 0, len))
    return 0;
  j->text = text;
  j->length = len;
  j->count = 0;
  struct Parser p = {j, 0};
  if (value(&p, 0) != 0)
    return 0;
  ws(&p);
  return p.p == len;
}
int zk_json_get(const struct ZkJson *j, int t, const char *key) {
  if (!j || t < 0 || t >= j->count || j->token[t].type != DJ_OBJECT)
    return -1;
  for (int i = t + 1; i < j->token[t].next;) {
    int v = j->token[i].next;
    if (zk_json_is(j, i, key))
      return v;
    i = j->token[v].next;
  }
  return -1;
}
int zk_json_at(const struct ZkJson *j, int t, int at) {
  if (!j || t < 0 || t >= j->count || j->token[t].type != DJ_ARRAY || at < 0)
    return -1;
  for (int i = t + 1; i < j->token[t].next; i = j->token[i].next)
    if (!at--)
      return i;
  return -1;
}
int zk_json_count(const struct ZkJson *j, int t) {
  return j && t >= 0 && t < j->count &&
                 (j->token[t].type == DJ_ARRAY || j->token[t].type == DJ_OBJECT)
             ? j->token[t].count
             : 0;
}
char *zk_widget_read(const char *path, size_t limit, size_t *length) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  char *b = malloc(limit + 1);
  if (!b) {
    fclose(f);
    return NULL;
  }
  size_t n = fread(b, 1, limit + 1, f);
  int bad = ferror(f) || n > limit || memchr(b, 0, n) != NULL;
  fclose(f);
  if (bad) {
    free(b);
    return NULL;
  }
  b[n] = 0;
  if (length)
    *length = n;
  return b;
}
int zk_json_quote(const char *s, char *out, size_t cap) {
  size_t n = 0;
  if (cap < 3)
    return 0;
  out[n++] = '"';
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    const char *e = c == '"'    ? "\\\""
                    : c == '\\' ? "\\\\"
                    : c == '\n' ? "\\n"
                    : c == '\r' ? "\\r"
                    : c == '\t' ? "\\t"
                                : NULL;
    if (c < 32 && !e)
      return 0;
    size_t k = e ? 2 : 1;
    if (n + k + 2 > cap)
      return 0;
    if (e) {
      out[n++] = e[0];
      out[n++] = e[1];
    } else
      out[n++] = (char)c;
  }
  out[n++] = '"';
  out[n] = 0;
  return 1;
}
