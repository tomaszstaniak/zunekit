/* SPDX-License-Identifier: MIT */
#include "zk_https.h"
#include <curl/curl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

/* Earlier than any certificate a request can meet: a clock before this is wrong. */
#define ZK_HTTPS_DATE_FLOOR 1790812800L /* 2026-10-01 */

char zk_https_package_ca[512];
char zk_https_log_path[512];
char zk_https_user_agent[64];

void zk_https_bundle_note(char *error, size_t cap, const char *package_ca) {
  size_t n = strlen(error);
  if (package_ca && package_ca[0])
    return;
  snprintf(error + n, n + 1 < cap ? cap - n : 0, " (Also: bundled certificates not found.)");
}
int zk_https_ca_order(const char *package, const char *env,
                          const char *system, const char *out[3]) {
  const char *all[3] = {package, env, system};
  int count = 0;
  for (int i = 0; i < 3; i++) {
    int seen = !all[i] || !*all[i];
    for (int k = 0; k < count && !seen; k++)
      seen = !strcmp(out[k], all[i]);
    if (!seen)
      out[count++] = all[i];
  }
  return count;
}

/* One line per request in the widget's state drawer, for testers and bug
 * reports; kept below 64 KB by starting over. */
void zk_https_log(const char *format, ...) {
  if (!zk_https_log_path[0])
    return;
  FILE *f = fopen(zk_https_log_path, "a");
  if (!f)
    return;
  if (fseek(f, 0, SEEK_END) == 0 && ftell(f) > 65536) {
    fclose(f);
    if (!(f = fopen(zk_https_log_path, "w")))
      return;
  }
  time_t now = time(NULL);
  struct tm *t = gmtime(&now);
  if (t)
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d ", t->tm_year + 1900, t->tm_mon + 1,
            t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
  va_list args;
  va_start(args, format);
  vfprintf(f, format, args);
  va_end(args);
  fputc('\n', f);
  fclose(f);
}

#ifdef __AROS__
#include <bsdsocket/socketbasetags.h>
#include <errno.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/socket.h>
/* Define the base here so static-link autoinit does not abort before main
 * when no TCP/IP stack is running. Offline errors must be serializable. */
struct Library *SocketBase;
static void network_close(void) {
  if (SocketBase) {
    CloseLibrary(SocketBase);
    SocketBase = NULL;
  }
}
/* The host sends Ctrl-C when it stops waiting for an answer; the transfer
 * ends then instead of running on to its own timeout. */
static int zk_https_progress(void *context, curl_off_t a, curl_off_t b,
                                 curl_off_t c, curl_off_t d) {
  (void)context; (void)a; (void)b; (void)c; (void)d;
  return (SetSignal(0, 0) & SIGBREAKF_CTRL_C) != 0;
}
#else
static void network_close(void) {}
#endif

size_t zk_https_write(char *data, size_t size, size_t count,
                          void *context) {
  struct ZkHttpsBuffer *b = context;
  if (size && count > SIZE_MAX / size)
    return 0;
  size_t n = size * count;
  if (n > b->limit - b->size)
    return 0;
  char *p = realloc(b->data, b->size + n + 1);
  if (!p)
    return 0;
  b->data = p;
  memcpy(p + b->size, data, n);
  b->size += n;
  p[b->size] = 0;
  return n;
}
int zk_https_get(const char *url, size_t limit, char **out, size_t *len,
                     char *error, size_t cap) {
  if (strncmp(url, "https://", 8)) {
    snprintf(error, cap, "HTTPS is required");
    return 0;
  }
#ifdef __AROS__
  SocketBase = OpenLibrary((CONST_STRPTR) "bsdsocket.library", 4);
  if (!SocketBase) {
    snprintf(error, cap, "No TCP/IP stack is running");
    return 0;
  }
  /* The library sets the C library's errno only once told where it is
   * (the SDK's socket autoinit would do this; this base is opened by
   * hand). Without it curl read a stale errno after its non-blocking
   * connect instead of EINPROGRESS and gave up: "Couldn't connect to
   * server" on a working network. */
  if (SocketBaseTags(SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(errno))), (IPTR)&errno,
                     TAG_DONE)) {
    snprintf(error, cap, "TCP/IP stack refused the errno setup");
    network_close();
    return 0;
  }
#endif
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    snprintf(error, cap, "Cannot initialize HTTPS");
    network_close();
    return 0;
  }
  /* Certificate authorities: the bundle shipped with the package (current when
   * the package was made), then CURL_CA_BUNDLE, then the system's. A set
   * CURL_CA_BUNDLE used to replace the other two, so one stale file broke
   * every request. A failed verification with one is tried with the next;
   * verification itself is never turned off. */
  const char *candidates[3];
#ifdef __AROS__
  const char *system = "ENV:SYS/Certificates/ca-bundle.crt";
#else
  const char *system = NULL;
#endif
  int count = zk_https_ca_order(zk_https_package_ca,
                                    getenv("CURL_CA_BUNDLE"), system,
                                    candidates);
  if (!count)
    candidates[count++] = NULL;
  struct ZkHttpsBuffer b = {NULL, 0, limit};
  CURLcode rc = CURLE_OK;
  long status = 0, verify = 0;
  const char *used = NULL;
  char detail[CURL_ERROR_SIZE];
  for (int i = 0; i < count; i++) {
    CURL *c = curl_easy_init();
    if (!c) {
      rc = CURLE_OUT_OF_MEMORY;
      break;
    }
    used = candidates[i];
    detail[0] = 0;
    free(b.data);
    b.data = NULL;
    b.size = 0;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
#ifdef __AROS__
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, zk_https_progress);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
#endif
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, zk_https_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_USERAGENT, zk_https_user_agent[0] ? zk_https_user_agent : "AROS-widget/1");
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detail);
    /* ZK_HTTPS_DEBUG set: curl's own trace on the error stream, for a
     * provider started by hand when the network does not answer. */
    if (getenv("ZK_HTTPS_DEBUG"))
      curl_easy_setopt(c, CURLOPT_VERBOSE, 1L);
    if (used)
      curl_easy_setopt(c, CURLOPT_CAINFO, used);
    rc = curl_easy_perform(c);
    status = verify = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(c, CURLINFO_SSL_VERIFYRESULT, &verify);
    curl_easy_cleanup(c);
    zk_https_log("GET %.80s ca=%s curl=%d (%s) verify=%ld http=%ld%s%s", url,
                     used ? used : "(curl default)", (int)rc,
                     curl_easy_strerror(rc), verify, status,
                     detail[0] ? " detail=" : "", detail);
    if (rc != CURLE_PEER_FAILED_VERIFICATION && rc != CURLE_SSL_CACERT_BADFILE &&
        rc != CURLE_SSL_CACERT)
      break;
  }
  curl_global_cleanup();
  network_close();
  if (rc != CURLE_OK || status != 200 || !b.size) {
    time_t now = time(NULL);
    /* The date is a hint, not a diagnosis: a clock before this release or
     * more than five years after it is suspicious; OpenSSL's own "not yet
     * valid" and "expired" (9, 10) name it. */
    int odd_date = now < ZK_HTTPS_DATE_FLOOR || now > ZK_HTTPS_DATE_FLOOR + 5L * 366 * 86400;
    struct tm *t = gmtime(&now);
    char date[96] = "";
    if (odd_date || verify == 9 || verify == 10)
      snprintf(date, sizeof date, " The system date is %04d-%02d-%02d%s.",
               t ? (t->tm_year + 1900) % 10000 : 0, t ? (t->tm_mon + 1) % 100 : 0,
               t ? t->tm_mday % 100 : 0,
               odd_date ? ": check Prefs/Time" : "");
    if (rc == CURLE_SSL_CACERT_BADFILE)
      snprintf(error, cap,
               "Secure connection not possible: no readable CA certificates in %s",
               used ? used : "the default place");
    else if (rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CACERT) {
      const char *why = verify == 9    ? "not valid yet"
                        : verify == 10 ? "expired"
                        : verify == 20 || verify == 19 || verify == 2
                            ? "issued by an unknown authority"
                            : "not trusted";
      snprintf(error, cap,
               "Secure connection refused: the server certificate is %s "
               "(verify code %ld).%s",
               why, verify, date);
    } else if (rc != CURLE_OK)
      snprintf(error, cap, "Network: %s", curl_easy_strerror(rc));
    else
      snprintf(error, cap, "The server answered HTTP %ld", status);
    /* The failure itself comes first (a name that does not resolve is
     * not a certificate problem); that the bundled file was never tried
     * is added, since only the log showed it before. */
    if (rc != CURLE_OK)
      zk_https_bundle_note(error, cap, zk_https_package_ca);
    zk_https_log("result: %s", error);
    free(b.data);
    return 0;
  }
  *out = b.data;
  *len = b.size;
  return 1;
}
