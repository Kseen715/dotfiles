/* lib/fetch.h -- downloads: fetch a URL through whatever transport this system
 * has, and the version lookups built on that.
 *
 * ONE HEADER, and the transport is not part of the contract. POSIX runs
 * curl or wget the way lib/net.sh ran them, installing one when the box has
 * neither; Windows calls WinINet, which is already there and already knows the
 * machine's proxy. A caller asks for a URL and gets bytes.
 *
 * Two layers, and the difference is worth keeping straight:
 *
 *   osr_fetch_*   the verbs a module or builder uses. They log, they respect
 *                 the theme-only flag, and they fail the way this tree fails.
 *   osr_download / osr_fetch_to_buffer / osr_final_url
 *                 the raw transport underneath, answering in OSR_NET_*. A
 *                 caller wants these only when it needs the status code
 *                 itself.
 *
 * The parsers are pure string handling with no I/O and no OS dependency, which
 * is why they are asserted on whichever host runs the suite.
 */
#ifndef OSR_FETCH_H
#define OSR_FETCH_H

#include <sys/types.h>

#include "common.h"

/* --- the raw transport, and the parsers over it -------------------------- */

#define OSR_NET_OK          0
#define OSR_NET_ERR       (-1)
#define OSR_NET_UNSUPPORTED (-2)  /* platform has no fetch backend yet */

/* --- pure parsers: no I/O, portable, unit-testable everywhere ------------ */

/* osr_url_filename -- last path segment of url, query string stripped.
 * Port of the _dl_name derivation in lib/net.sh's osr_download().
 */
void osr_url_filename(const char *url, char *out, unsigned long out_sz);

/* osr_parse_content_length -- scan a raw HTTP header block (CRLF-separated,
 * as HttpQueryInfo(HTTP_QUERY_RAW_HEADERS_CRLF) or a plain HEAD response
 * returns it) for the last Content-Length value. Returns -1 if absent.
 * Port of _osr_remote_size's sed pattern in lib/net.sh.
 */
long osr_parse_content_length(const char *header_block);

/* osr_parse_location -- last Location: value in a raw header block, or ""
 * when none is present. Port of osr_final_url's sed pattern in lib/net.sh.
 */
void osr_parse_location(const char *header_block, char *out, unsigned long out_sz);

/* --- network I/O: implemented per platform ------------------------------- */

/* osr_net_available -- 1 if a fetch backend exists on this build, else 0. */
int osr_net_available(void);

/* osr_download -- fetch url to dest_path. Returns OSR_NET_*. */
int osr_download(const char *url, const char *dest_path);

/* osr_fetch_to_buffer -- fetch url into a malloc'd buffer (caller frees).
 * *out_buf is NULL and *out_len is 0 on failure.
 */
int osr_fetch_to_buffer(const char *url, char **out_buf, unsigned long *out_len);

/* osr_remote_size -- Content-Length of url via HEAD, or -1 if unknown. */
long osr_remote_size(const char *url);

/* osr_final_url -- where url resolves after redirects. Returns OSR_NET_*. */
int osr_final_url(const char *url, char *out, unsigned long out_sz);

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

/* osr_fetch_pipe_user / osr_fetch_pipe_root -- `<fetch url> | <argv>` with the
 * command run as the riced account or as root: an upstream installer script that
 * is piped into an interpreter rather than saved first. Returns 1 when the
 * command exited 0 -- the pipeline's `$?`, which is the right-hand side's. */
int osr_fetch_pipe_user(const char *url, char *const argv[]);
int osr_fetch_pipe_root(const char *url, char *const argv[]);

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

/* osr_github_latest_quiet -- the same lookup with the "could not resolve"
 * warning suppressed, for a builder that falls back to another route rather
 * than stopping when the API is unreachable. */
int osr_github_latest_quiet(Str *out, const char *repo);

/* osr_json_string_field -- the first `"<key>": "<value>"` in a JSON blob. The
 * whole of the JSON "parsing" the shell tier ever did, and enough for the vendor
 * release feeds the builders read. */
int osr_json_string_field(Str *out, const char *json, const char *key);

#endif /* OSR_FETCH_H */
