/* SPDX-License-Identifier: MIT */
/* zk_widget_snapshot: what a widget's provider answers (status, title,
 * fields, rows), written and read the same way by providers and hosts.
 * Taken from Dockbar's widget host; the protocol is in Dockbar's
 * docs/applets.md. */
#ifndef ZK_WIDGET_SNAPSHOT_H
#define ZK_WIDGET_SNAPSHOT_H
#include <stddef.h>
#include <stdio.h>
#define ZW_ID 80
/* Widget API versions this host speaks (applets.md). */
#define ZW_API_MIN 1
#define ZW_API_MAX 2
#define ZW_PATH 1024
#define ZW_FIELDS 24
#define ZW_ROWS 64
#define ZW_RESPONSE_LIMIT (128u * 1024u)
enum ZkWidgetStatus { ZW_LOADING, ZW_READY, ZW_STALE, ZW_ERROR };
struct ZkWidgetField {
  char key[40], text[160], unit[32];
  int numeric;
  double number;
};
struct ZkWidgetRow {
  char label[160], value[160], icon[48];
  char section[40], caption[80], detail[80];
  double plot;
  int has_plot;
};
struct ZkWidgetSnapshot {
  char package[ZW_ID], instance[ZW_ID], session[ZW_ID], fingerprint[160];
  unsigned long generation;
  double updated, source_time;
  enum ZkWidgetStatus status;
  char title[160], icon[48], message[160], attribution[160], url[256];
  struct ZkWidgetField fields[ZW_FIELDS];
  int field_count;
  struct ZkWidgetRow rows[ZW_ROWS];
  int row_count;
};
int zk_widget_id_valid(const char *id);
int zk_widget_snapshot_parse(const char *, size_t,
                               struct ZkWidgetSnapshot *);
int zk_widget_snapshot_write(FILE *, const struct ZkWidgetSnapshot *);
const char *zk_widget_field(const struct ZkWidgetSnapshot *,
                              const char *key);
#endif
