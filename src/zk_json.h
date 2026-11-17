/* SPDX-License-Identifier: MIT */
/* zk_json: a small JSON reader (tokens over the text) and string quoting,
 * taken from Dockbar's widget host. */
#ifndef ZK_WIDGET_JSON_H
#define ZK_WIDGET_JSON_H
#include <stddef.h>
/* Input is borrowed until the next parse. Tokens never contain pointers into
 * other processes. Bounds are shared by package and network decoders. */
#define ZK_JSON_TOKENS 16384
#define ZK_JSON_LIMIT (512u * 1024u)
enum ZkJsonType {
  DJ_OBJECT,
  DJ_ARRAY,
  DJ_STRING,
  DJ_NUMBER,
  DJ_BOOL,
  DJ_NULL
};
struct ZkJsonToken {
  int type, start, end, next, count;
};
struct ZkJson {
  const char *text;
  size_t length;
  int count;
  struct ZkJsonToken token[ZK_JSON_TOKENS];
};
int zk_json_parse(struct ZkJson *json, const char *text, size_t length);
int zk_json_get(const struct ZkJson *json, int object, const char *key);
int zk_json_at(const struct ZkJson *json, int array, int index);
int zk_json_count(const struct ZkJson *json, int array);
int zk_json_string(const struct ZkJson *json, int token, char *out,
                     size_t capacity);
int zk_json_number(const struct ZkJson *json, int token, double *value);
int zk_json_int(const struct ZkJson *json, int token, int *value);
int zk_json_is(const struct ZkJson *json, int token, const char *value);
/* Allocated bounded file. Caller frees. Rejects embedded NULs. */
char *zk_widget_read(const char *path, size_t limit, size_t *length);
/* JSON string escaping, including quotes; 0 on insufficient capacity. */
int zk_json_quote(const char *text, char *out, size_t capacity);
#endif
