#include "zk_json.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  struct ZkJson j;
  char s[32];
  double n;
  assert(zk_json_parse(&j, "{\"a\":[null,-2.5,true],\"city\":\"Łódź\"}",
                         strlen("{\"a\":[null,-2.5,true],\"city\":\"Łódź\"}")));
  assert(zk_json_string(&j, zk_json_get(&j, 0, "city"), s, sizeof s) &&
         !strcmp(s, "Łódź"));
  int a = zk_json_get(&j, 0, "a");
  assert(zk_json_count(&j, a) == 3);
  assert(zk_json_number(&j, zk_json_at(&j, a, 1), &n) && n == -2.5);
  assert(!zk_json_number(&j, zk_json_at(&j, a, 0), &n));
  assert(!zk_json_parse(&j, "{\"a\":1,\"a\":2}", 13));
  const char *dup = "{\"a\":1,\"\\u0061\":2}";
  assert(!zk_json_parse(&j, dup, strlen(dup)));
  const char *bad[] = {"{",           "[1,]",    "{\"x\":NaN}",
                       "1e999",       "01",      "{\"x\":1} garbage",
                       "\"\\uD800\"", "\"a\nb\""};
  for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
    assert(!zk_json_parse(&j, bad[i], strlen(bad[i])));
  char deep[80];
  memset(deep, '[', 20);
  memset(deep + 20, ']', 20);
  assert(!zk_json_parse(&j, deep, 40));
  const char *u = "\"\\uD83C\\uDF27\"";
  assert(zk_json_parse(&j, u, strlen(u)));
  assert(zk_json_string(&j, 0, s, sizeof s));
  assert(strlen(s) == 4);
  puts("applet JSON: passed");
  return 0;
}
