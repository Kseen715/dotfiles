/* lib/fetch.h -- the C port of lib/net.sh: fetching a URL through whichever
 * downloader the box has, and the version lookups built on it.
 *
 * Distinct from lib/net.h on purpose. net.h is the NATIVE fetch layer (WinInet
 * today, sockets one day) plus the pure header parsers; this is the shell
 * tier's layer, whose semantics are "run curl/wget the way lib/net.sh ran it".
 * The parsers are shared: this file uses net.h's.
 */
#ifndef OSR_FETCH_H
#define OSR_FETCH_H

#include <sys/types.h>

#include "common.h"

/* osr_fetch_backend -- "curl", "wget", "busybox-wget", or "" (osr_downloader). */
const char *osr_fetch_backend(void);

/* osr_fetch_ensure -- the same, installing curl first when the box has none,
 * so a script:/tarball: row works on a minimal image. */
const char *osr_fetch_ensure(void);

/* osr_fetch_child -- fork the downloader with its stdout on write_fd, for a
 * payload that is piped rather than saved (`... | sh`). Returns its pid. */
pid_t osr_fetch_child(const char *backend, const char *url, int write_fd);

/* osr_fetch_stdout -- stream a URL to this process's stdout. */
int osr_fetch_stdout(const char *url);

/* osr_fetch_buffer -- the same payload, collected into out. */
int osr_fetch_buffer(Str *out, const char *url);

/* osr_fetch_download -- url to dest_path, printing a progress LINE every poll
 * for anything at least OSR_PROGRESS_MIN_BYTES. expected is the size when the
 * caller already knows it, 0 to ask the server. Returns 1 on success. */
int osr_fetch_download(const char *url, const char *dest, long expected);

/* osr_fetch_remote_size -- Content-Length from a HEAD, -1 when unknown. */
long osr_fetch_remote_size(const char *url);

/* osr_fetch_final_url -- where a redirecting URL lands. 0 when nothing
 * redirects or no HEAD is possible. */
int osr_fetch_final_url(Str *out, const char *url);

/* osr_github_latest -- the newest release tag of owner/repo (github_latest). */
int osr_github_latest(Str *out, const char *repo);

#endif /* OSR_FETCH_H */
