/* SPDX-License-Identifier: MIT */
/* zk_https: one HTTPS GET with server verification, on libcurl. Taken
 * from Dockbar's Weather provider so that every widget provider fetches
 * the same way. Link with curl and its TLS library. */
#ifndef ZK_HTTPS_H
#define ZK_HTTPS_H
#include <stddef.h>
struct ZkHttpsBuffer {
  char *data;
  size_t size, limit;
};
size_t zk_https_write(char *, size_t, size_t, void *);
int zk_https_get(const char *, size_t, char **, size_t *, char *, size_t);
/* Set by main before requests: the CA bundle shipped with the package
 * (empty: none was readable) and the log file (empty: no log). */
extern char zk_https_package_ca[512];
extern char zk_https_log_path[512];
/* What the request names itself as; "" for a neutral name. */
extern char zk_https_user_agent[64];
/* Certificate files to try, in order: the bundled one, CURL_CA_BUNDLE, the
 * system's; empty and repeated entries are skipped. Returns the count. */
int zk_https_ca_order(const char *package, const char *env,
                          const char *system, const char *out[3]);
/* Adds to a failure message that the bundled certificates were not found,
 * after the failure itself, which stays the main message (a DNS error is
 * not a certificate problem). Nothing when the bundled file was found. */
void zk_https_bundle_note(char *error, size_t cap, const char *package_ca);
void zk_https_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
#endif
