/* lib/tls.h -- HTTPS for the systems whose own TLS stack is too old to talk
 * to anything.
 *
 * Windows XP's schannel stops at TLS 1.0. TLS 1.1 and 1.2 were never
 * backported to it, so WinINet -- which is what lib/fetch.c uses everywhere
 * else, and which is the right answer on any current Windows because it knows
 * the machine's proxy and its root store -- cannot complete a handshake with
 * a host worth fetching from. Nothing on the box can be configured around
 * that; the fix is to bring a TLS stack along.
 *
 * So this unit is a small HTTPS client: a socket from ws2_32, TLS 1.2 from
 * the vendored BearSSL (thirdparty/bearssl.h, see thirdparty/VENDOR.md), and
 * the trust anchors decoded at first use from the Mozilla CA bundle compiled
 * in beside it. lib/fetch.c routes to it where the system transport cannot do
 * the job -- see osr_tls_needed and osr_tls_available -- and keeps the system
 * transport otherwise.
 *
 * It is not Windows-only: nob builds it for any target asked for it
 * (NOB_TLS=1), and on POSIX it is what a box with neither curl nor wget
 * falls back to.
 *
 * The URL handling below is pure string work with no I/O, so it is asserted
 * on whichever host runs the suite, the same way fetch.c's header parsers
 * are.
 */
#ifndef OSR_TLS_H
#define OSR_TLS_H

/* --- pure parsers -------------------------------------------------------- */

/* osr_url_split -- break url into its parts. port is filled with the default
 * for the scheme (443/80) when the URL names none, and path with "/" when it
 * has none. Returns 1 on a URL this client can act on, 0 otherwise.
 */
int osr_url_split(const char *url,
                  char *scheme, unsigned long scheme_sz,
                  char *host, unsigned long host_sz,
                  int *port,
                  char *path, unsigned long path_sz);

/* osr_url_resolve -- a Location: value against the URL it came from, which
 * may be absolute ("https://host/x"), root-relative ("/x") or neither
 * ("x"). Returns 1 when out holds a usable absolute URL.
 */
int osr_url_resolve(const char *base, const char *location,
                    char *out, unsigned long out_sz);

/* --- the transport ------------------------------------------------------- */

/* osr_tls_sink -- where a response body goes. Called with each block as it
 * arrives; return 0 to abort the transfer. */
typedef int (*osr_tls_sink)(void *ctx, const char *data, unsigned long len);

/* osr_tls_available -- 1 when this build carries the client below at all
 * (nob defined OSR_HAVE_BEARSSL), 0 when the file is only its parsers. */
int osr_tls_available(void);

/* osr_tls_needed -- 1 when this system's own HTTPS cannot be trusted to
 * connect and the client below should be used instead: Windows older than
 * Vista, or $OSR_TLS=bearssl for asking for it anywhere. 0 on a current
 * Windows, and on POSIX unless forced -- there the "no curl, no wget" case is
 * fetch.c's to spot, since only it knows what the box has. */
int osr_tls_needed(void);

/* osr_tls_get -- GET url, following up to 5 redirects, and hand the body to
 * sink. sink NULL asks for the headers only (a HEAD), which is what a size
 * or final-URL lookup wants. final_url and content_length are optional
 * outputs describing the response that was finally read; content_length is
 * -1 when the server did not say. Returns OSR_NET_* (see fetch.h).
 */
int osr_tls_get(const char *url, osr_tls_sink sink, void *ctx,
                char *final_url, unsigned long final_sz, long *content_length);

#endif /* OSR_TLS_H */
