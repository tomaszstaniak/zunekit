/* SPDX-License-Identifier: MIT */
#include "zk_widget_snapshot.h"
#include "zk_json.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int zk_widget_id_valid(const char *s) {
  if (!s || !*s || strlen(s) >= ZW_ID || !strcmp(s, ".") || !strcmp(s, ".."))
    return 0;
  for (; *s; s++)
    if (!isalnum((unsigned char)*s) && *s != '.' && *s != '-' && *s != '_')
      return 0;
  return 1;
}
static int str(const struct ZkJson *j, int obj, const char *key, char *out,
               size_t cap, int required) {
  int t = zk_json_get(j, obj, key);
  if (t < 0)
    return !required;
  return zk_json_string(j, t, out, cap);
}
int zk_widget_snapshot_parse(const char *text, size_t len,
                               struct ZkWidgetSnapshot *out) {
  int ok = 0, api, g;
  struct ZkJson *j = malloc(sizeof *j);
  struct ZkWidgetSnapshot *s = calloc(1, sizeof *s);
  if (!j || !s)
    goto done;
  if (len > ZW_RESPONSE_LIMIT || !zk_json_parse(j, text, len) ||
      !zk_json_int(j, zk_json_get(j, 0, "api"), &api) ||
      api < ZW_API_MIN || api > ZW_API_MAX)
    goto done;
  if (!str(j, 0, "package", s->package, sizeof s->package, 1) ||
      !str(j, 0, "instance", s->instance, sizeof s->instance, 1) ||
      !str(j, 0, "session", s->session, sizeof s->session, 1) ||
      !zk_widget_id_valid(s->package) || !zk_widget_id_valid(s->instance) ||
      !zk_widget_id_valid(s->session))
    goto done;
  if (!zk_json_int(j, zk_json_get(j, 0, "generation"), &g) || g < 1)
    goto done;
  s->generation = (unsigned long)g;
  int st = zk_json_get(j, 0, "status");
  if (zk_json_is(j, st, "ready"))
    s->status = ZW_READY;
  else if (zk_json_is(j, st, "stale"))
    s->status = ZW_STALE;
  else if (zk_json_is(j, st, "error"))
    s->status = ZW_ERROR;
  else if (zk_json_is(j, st, "loading"))
    s->status = ZW_LOADING;
  else
    goto done;
  if (!zk_json_number(j, zk_json_get(j, 0, "updated"), &s->updated) ||
      s->updated < 0 ||
      !zk_json_number(j, zk_json_get(j, 0, "sourceTime"),
                        &s->source_time) ||
      s->source_time < 0)
    goto done;
  if (!str(j, 0, "title", s->title, sizeof s->title, 1) ||
      !str(j, 0, "icon", s->icon, sizeof s->icon, 0) ||
      !str(j, 0, "fingerprint", s->fingerprint, sizeof s->fingerprint, 1) ||
      !str(j, 0, "message", s->message, sizeof s->message, 0) ||
      !str(j, 0, "attribution", s->attribution, sizeof s->attribution, 0) ||
      !str(j, 0, "url", s->url, sizeof s->url, 0))
    goto done;
  int fields = zk_json_get(j, 0, "fields");
  if (fields < 0 || j->token[fields].type != DJ_OBJECT ||
      j->token[fields].count > ZW_FIELDS)
    goto done;
  for (int k = fields + 1; k < j->token[fields].next;) {
    struct ZkWidgetField *f = &s->fields[s->field_count++];
    int obj = j->token[k].next, v = zk_json_get(j, obj, "value");
    char unit[32] = "";
    if (!zk_json_string(j, k, f->key, sizeof f->key) || v < 0 ||
        !str(j, obj, "unit", unit, sizeof unit, 0))
      goto done;
    if (j->token[v].type == DJ_NULL)
      strcpy(f->text, "--");
    else if (j->token[v].type == DJ_STRING) {
      if (!zk_json_string(j, v, f->text, sizeof f->text))
        goto done;
    } else {
      if (!zk_json_number(j, v, &f->number) || fabs(f->number) > 1e9)
        goto done;
      f->numeric = 1;
      strcpy(f->unit, unit);
      snprintf(f->text, sizeof f->text, "%.0f%s%s", round(f->number),
               unit[0] ? " " : "", unit);
    }
    k = j->token[obj].next;
  }
  int rows = zk_json_get(j, 0, "rows");
  if (rows >= 0) {
    if (j->token[rows].type != DJ_ARRAY || j->token[rows].count > ZW_ROWS)
      goto done;
    s->row_count = j->token[rows].count;
    for (int i = 0; i < s->row_count; i++) {
      int r = zk_json_at(j, rows, i);
      struct ZkWidgetRow *row = &s->rows[i];
      if (!str(j, r, "label", row->label, sizeof row->label, 1) ||
          !str(j, r, "value", row->value, sizeof row->value, 1) ||
          !str(j, r, "icon", row->icon, sizeof row->icon, 0) ||
          !str(j,r,"section",row->section,sizeof row->section,0) ||
          !str(j,r,"caption",row->caption,sizeof row->caption,0) ||
          !str(j,r,"detail",row->detail,sizeof row->detail,0))
        goto done;
      int v = zk_json_get(j, r, "plot");
      if (v >= 0 && j->token[v].type != DJ_NULL) {
        if (!zk_json_number(j, v, &row->plot) || fabs(row->plot) > 1e9)
          goto done;
        row->has_plot = 1;
      }
    }
  }
  *out = *s;
  ok = 1;
done:
  free(j);
  free(s);
  return ok;
}
const char *zk_widget_field(const struct ZkWidgetSnapshot *s,
                              const char *key) {
  for (int i = 0; s && i < s->field_count; i++)
    if (!strcmp(s->fields[i].key, key))
      return s->fields[i].text;
  return "--";
}

static int quoted(FILE *f, const char *s) {
  char b[2048];
  return zk_json_quote(s, b, sizeof b) && fputs(b, f) >= 0;
}
static int prop(FILE *f, const char *key, const char *value) {
  return quoted(f, key) && fputc(':', f) != EOF && quoted(f, value);
}
int zk_widget_snapshot_write(FILE *f, const struct ZkWidgetSnapshot *s) {
  if (fputs("{\"api\":1,", f) < 0 || !prop(f, "package", s->package) ||
      fputc(',', f) == EOF || !prop(f, "instance", s->instance) ||
      fputc(',', f) == EOF || !prop(f, "session", s->session))
    return 0;
  if (fprintf(f, ",\"generation\":%lu,\"updated\":%.0f,\"sourceTime\":%.0f,",
              s->generation, s->updated, s->source_time) < 0)
    return 0;
  const char *status = s->status == ZW_READY   ? "ready"
                       : s->status == ZW_STALE ? "stale"
                       : s->status == ZW_ERROR ? "error"
                                               : "loading";
  if (!prop(f, "status", status) || fputc(',', f) == EOF ||
      !prop(f, "fingerprint", s->fingerprint) || fputc(',', f) == EOF ||
      !prop(f, "title", s->title) || fputc(',', f) == EOF ||
      !prop(f, "icon", s->icon) || fputc(',', f) == EOF ||
      !prop(f, "message", s->message) || fputc(',', f) == EOF ||
      !prop(f, "attribution", s->attribution) || fputc(',', f) == EOF ||
      !prop(f, "url", s->url) || fputs(",\"fields\":{", f) < 0)
    return 0;
  for (int i = 0; i < s->field_count; i++) {
    const struct ZkWidgetField *v = &s->fields[i];
    if ((i && fputc(',', f) == EOF) || !quoted(f, v->key) ||
        fputs(":{\"value\":", f) < 0)
      return 0;
    if (v->numeric) {
      if (fprintf(f, "%.8g,", v->number) < 0 || !prop(f, "unit", v->unit))
        return 0;
    } else if (!quoted(f, v->text))
      return 0;
    if (fputc('}', f) == EOF)
      return 0;
  }
  if (fputs("},\"rows\":[", f) < 0)
    return 0;
  for (int i = 0; i < s->row_count; i++) {
    const struct ZkWidgetRow *r = &s->rows[i];
    if ((i && fputc(',', f) == EOF) || fputc('{', f) == EOF ||
        !prop(f, "label", r->label) || fputc(',', f) == EOF ||
        !prop(f, "value", r->value) || fputc(',', f) == EOF ||
        !prop(f, "icon", r->icon) || fputc(',',f)==EOF ||
        !prop(f,"section",r->section) || fputc(',',f)==EOF ||
        !prop(f,"caption",r->caption) || fputc(',',f)==EOF ||
        !prop(f,"detail",r->detail))
      return 0;
    if (r->has_plot && fprintf(f, ",\"plot\":%.8g", r->plot) < 0)
      return 0;
    if (fputc('}', f) == EOF)
      return 0;
  }
  return fputs("]}\n", f) >= 0 && !ferror(f);
}
