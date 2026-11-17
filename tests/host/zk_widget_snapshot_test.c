#include "zk_widget_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  struct ZkWidgetSnapshot *s = calloc(1, sizeof *s),
                            *old = calloc(1, sizeof *old);
  assert(s && old);
  const char *valid =
      "{\"api\":1,\"package\":\"org.aros.weather\",\"instance\":\"one\","
      "\"session\":\"s1\",\"generation\":2,\"status\":\"ready\",\"updated\":"
      "123,\"sourceTime\":120,\"fingerprint\":\"loc1\",\"title\":\"Berlin\","
      "\"icon\":\"rain\",\"fields\":{\"temperature\":{\"value\":-2.5,\"unit\":"
      "\"C\"},\"location\":{\"value\":\"Berlin\"},\"humidity\":{\"value\":null}"
      "},\"rows\":[{\"label\":\"12:00\",\"value\":\"-2 C\",\"plot\":-2}]}";
  assert(zk_widget_snapshot_parse(valid, strlen(valid), s));
  assert(s->generation == 2 && s->row_count == 1);
  assert(!strcmp(zk_widget_field(s, "temperature"), "-3 C"));
  assert(!strcmp(zk_widget_field(s, "humidity"), "--"));
  strcpy(s->rows[0].section,"hourly");strcpy(s->rows[0].caption,"Now");strcpy(s->rows[0].detail,"15%");
  *old = *s;
  /* An action widget answers in API 2; 3 is newer than this host. */
  {
    const char *v2 =
        "{\"api\":2,\"package\":\"org.aros.capture\",\"instance\":\"i\","
        "\"session\":\"s1\",\"generation\":1,\"status\":\"ready\","
        "\"updated\":1,\"sourceTime\":1,\"fingerprint\":\"\",\"title\":"
        "\"AROS Capture\",\"message\":\"Started\",\"fields\":{}}";
    struct ZkWidgetSnapshot *t = calloc(1, sizeof *t);
    assert(t && zk_widget_snapshot_parse(v2, strlen(v2), t) &&
           !strcmp(t->message, "Started"));
    char v3[400];
    snprintf(v3, sizeof v3, "%s", v2);
    v3[7] = '3';
    assert(!zk_widget_snapshot_parse(v3, strlen(v3), t));
    free(t);
  }
  const char *bad = "{\"api\":99}";
  assert(!zk_widget_snapshot_parse(bad, strlen(bad), s));
  assert(!memcmp(old, s, sizeof *s));
  FILE *file = tmpfile();
  assert(file && zk_widget_snapshot_write(file, s));
  assert(!fseek(file, 0, SEEK_END));
  long bytes = ftell(file);
  assert(bytes > 0 && bytes < 32768);
  rewind(file);
  char *encoded = malloc((size_t)bytes + 1);
  assert(encoded && fread(encoded, 1, (size_t)bytes, file) == (size_t)bytes);
  encoded[bytes] = 0;
  assert(zk_widget_snapshot_parse(encoded, (size_t)bytes, old));
  assert(!strcmp(zk_widget_field(old, "temperature"), "-3 C"));
  assert(old->rows[0].has_plot && old->rows[0].plot == -2);
  assert(!strcmp(old->rows[0].section,"hourly")&&!strcmp(old->rows[0].caption,"Now")&&!strcmp(old->rows[0].detail,"15%"));
  free(encoded);
  fclose(file);
  puts("applet snapshot: passed");
  free(s);
  free(old);
  return 0;
}
