/* lib/net.h -- downloading + header parsing, C port of lib/net.sh.
 *
 * C89. The parsing half (osr_url_filename / osr_parse_content_length /
 * osr_parse_location) is plain string handling with no OS dependency, kept
 * that way on purpose so it is shared, tested code the day a native Linux
 * fetch backend lands next to os_linux's branch of osr_download() below --
 * see the #else stub in net.c.
 *
 * The I/O half (osr_download / osr_fetch_to_buffer / osr_remote_size /
 * osr_final_url) is implemented today only for _WIN32 (WinInet, available
 * since Win95 OSR2 -- well within the 0x0501/XP floor PLAN_UNIVERSAL.md
 * targets, see that file's toolchain matrix).
 */
#ifndef OSR_NET_H
#define OSR_NET_H

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

#endif /* OSR_NET_H */
