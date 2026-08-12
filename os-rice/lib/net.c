/* lib/net.c -- C port of lib/net.sh. See lib/net.h for the split between
 * portable parsing and per-platform I/O.
 *
 * C89 throughout (no // comments, no mid-block declarations, no C99-only
 * stdlib) so this keeps compiling under the pinned XP-era mingw-w64 toolchain
 * PLAN_UNIVERSAL.md Task 0.1 plans to stand up later -- that toolchain work
 * itself is not started yet (long-away-planned), but nothing here should need
 * rewriting when it lands.
 */
#include "net.h"

#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * pure parsers -- no OS dependency, exercised directly by
 * test/unit_c/net_parse_test.c without a network or a Windows build.
 * ---------------------------------------------------------------------- */

void osr_url_filename(const char *url, char *out, unsigned long out_sz) {
    const char *slash;
    const char *q;
    unsigned long len;

    if (out == NULL || out_sz == 0) return;
    if (url == NULL) { out[0] = '\0'; return; }

    slash = strrchr(url, '/');
    slash = (slash != NULL) ? slash + 1 : url;
    q = strchr(slash, '?');
    len = (q != NULL) ? (unsigned long)(q - slash) : (unsigned long)strlen(slash);
    if (len >= out_sz) len = out_sz - 1;

    memcpy(out, slash, len);
    out[len] = '\0';
}

static int ci_starts_with(const char *s, const char *prefix) {
    unsigned long i;
    for (i = 0; prefix[i] != '\0'; i++) {
        char a = s[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

/* find_last_header_value -- last occurrence wins, same rule lib/net.sh's
 * `tail -n 1` applies to a redirect chain's repeated header blocks.
 */
static const char *find_last_header_value(const char *header_block, const char *prefix, unsigned long *val_len) {
    const char *p;
    const char *result;
    unsigned long result_len;
    unsigned long prefix_len;

    *val_len = 0;
    if (header_block == NULL) return NULL;

    result = NULL;
    result_len = 0;
    prefix_len = (unsigned long)strlen(prefix);
    p = header_block;

    while (*p != '\0') {
        const char *line_start = p;
        const char *line_end;
        unsigned long line_len;

        line_end = strchr(p, '\n');
        if (line_end == NULL) line_end = p + strlen(p);
        line_len = (unsigned long)(line_end - line_start);
        if (line_len > 0 && line_start[line_len - 1] == '\r') line_len--;

        if (line_len >= prefix_len && ci_starts_with(line_start, prefix)) {
            const char *v = line_start + prefix_len;
            const char *v_end = line_start + line_len;
            while (v < v_end && (*v == ' ' || *v == '\t')) v++;
            result = v;
            result_len = (unsigned long)(v_end - v);
        }

        p = (*line_end == '\0') ? line_end : line_end + 1;
    }

    *val_len = result_len;
    return result;
}

long osr_parse_content_length(const char *header_block) {
    unsigned long val_len;
    const char *v;
    char buf[32];
    unsigned long i;
    long result;

    v = find_last_header_value(header_block, "content-length:", &val_len);
    if (v == NULL || val_len == 0) return -1;

    if (val_len >= sizeof(buf)) val_len = sizeof(buf) - 1;
    for (i = 0; i < val_len; i++) buf[i] = v[i];
    buf[val_len] = '\0';

    result = atol(buf);
    return result < 0 ? -1 : result;
}

void osr_parse_location(const char *header_block, char *out, unsigned long out_sz) {
    unsigned long val_len;
    const char *v;
    unsigned long end;
    unsigned long i;

    if (out == NULL || out_sz == 0) return;
    out[0] = '\0';

    v = find_last_header_value(header_block, "location:", &val_len);
    if (v == NULL) return;

    end = val_len;
    for (i = 0; i < val_len; i++) {
        if (v[i] == ' ' || v[i] == '\t') { end = i; break; }
    }
    if (end >= out_sz) end = out_sz - 1;

    memcpy(out, v, end);
    out[end] = '\0';
}

/* -------------------------------------------------------------------------
 * network I/O -- one implementation per platform, selected at compile time
 * (each target is already a separate build, per PLAN_UNIVERSAL.md's
 * "Architecture" section: no runtime OS branch where compile-time works).
 * ---------------------------------------------------------------------- */

#ifdef _WIN32

#ifndef WINVER
#define WINVER 0x0501       /* Windows XP floor -- see PLAN_UNIVERSAL.md */
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <wininet.h>
#include <stdio.h>

static HINTERNET wininet_open_session(void) {
    return InternetOpenA("os-rice/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
}

int osr_net_available(void) {
    return 1;
}

int osr_download(const char *url, const char *dest_path) {
    HINTERNET hSession;
    HINTERNET hUrl;
    FILE *fp;
    char buf[8192];
    DWORD read_bytes;
    int rc;

    if (url == NULL || dest_path == NULL) return OSR_NET_ERR;

    hSession = wininet_open_session();
    if (hSession == NULL) return OSR_NET_ERR;

    hUrl = InternetOpenUrlA(hSession, url, NULL, 0,
                             INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD, 0);
    if (hUrl == NULL) { InternetCloseHandle(hSession); return OSR_NET_ERR; }

    fp = fopen(dest_path, "wb");
    if (fp == NULL) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hSession);
        return OSR_NET_ERR;
    }

    rc = OSR_NET_OK;
    for (;;) {
        if (!InternetReadFile(hUrl, buf, sizeof(buf), &read_bytes)) { rc = OSR_NET_ERR; break; }
        if (read_bytes == 0) break;
        if (fwrite(buf, 1, read_bytes, fp) != read_bytes) { rc = OSR_NET_ERR; break; }
    }

    fclose(fp);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hSession);
    return rc;
}

int osr_fetch_to_buffer(const char *url, char **out_buf, unsigned long *out_len) {
    HINTERNET hSession;
    HINTERNET hUrl;
    char chunk[8192];
    DWORD read_bytes;
    char *buf;
    unsigned long cap;
    unsigned long len;

    *out_buf = NULL;
    *out_len = 0;
    if (url == NULL) return OSR_NET_ERR;

    hSession = wininet_open_session();
    if (hSession == NULL) return OSR_NET_ERR;

    hUrl = InternetOpenUrlA(hSession, url, NULL, 0,
                             INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD, 0);
    if (hUrl == NULL) { InternetCloseHandle(hSession); return OSR_NET_ERR; }

    buf = NULL;
    cap = 0;
    len = 0;

    for (;;) {
        if (!InternetReadFile(hUrl, chunk, sizeof(chunk), &read_bytes)) {
            free(buf);
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hSession);
            return OSR_NET_ERR;
        }
        if (read_bytes == 0) break;

        if (len + read_bytes + 1 > cap) {
            unsigned long new_cap = (cap == 0) ? 16384UL : cap * 2;
            char *new_buf;
            while (new_cap < len + read_bytes + 1) new_cap *= 2;
            new_buf = (char *)realloc(buf, new_cap);
            if (new_buf == NULL) {
                free(buf);
                InternetCloseHandle(hUrl);
                InternetCloseHandle(hSession);
                return OSR_NET_ERR;
            }
            buf = new_buf;
            cap = new_cap;
        }
        memcpy(buf + len, chunk, read_bytes);
        len += read_bytes;
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hSession);

    if (buf == NULL) {
        buf = (char *)malloc(1);
        if (buf == NULL) return OSR_NET_ERR;
    }
    buf[len] = '\0';
    *out_buf = buf;
    *out_len = len;
    return OSR_NET_OK;
}

/* wininet_head_open -- crack url, connect, send a HEAD request. Leaves
 * hSession/hConnect open (caller closes all three) so both callers below
 * can query the still-live hRequest afterward.
 */
static HINTERNET wininet_head_open(const char *url, HINTERNET *hSession_out, HINTERNET *hConnect_out) {
    HINTERNET hSession;
    HINTERNET hConnect;
    HINTERNET hRequest;
    URL_COMPONENTSA uc;
    char host[256];
    char path[2048];
    DWORD flags;

    *hSession_out = NULL;
    *hConnect_out = NULL;

    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = sizeof(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = sizeof(path);

    if (!InternetCrackUrlA(url, 0, 0, &uc)) return NULL;

    hSession = wininet_open_session();
    if (hSession == NULL) return NULL;

    hConnect = InternetConnectA(hSession, host, uc.nPort, NULL, NULL,
                                 INTERNET_SERVICE_HTTP, 0, 0);
    if (hConnect == NULL) { InternetCloseHandle(hSession); return NULL; }

    flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_FLAG_SECURE : 0;
    hRequest = HttpOpenRequestA(hConnect, "HEAD", path, NULL, NULL, NULL, flags, 0);
    if (hRequest == NULL) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hSession);
        return NULL;
    }

    if (!HttpSendRequestA(hRequest, NULL, 0, NULL, 0)) {
        InternetCloseHandle(hRequest);
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hSession);
        return NULL;
    }

    *hSession_out = hSession;
    *hConnect_out = hConnect;
    return hRequest;
}

long osr_remote_size(const char *url) {
    HINTERNET hSession;
    HINTERNET hConnect;
    HINTERNET hRequest;
    char headers[8192];
    DWORD dw_len;
    long result;

    if (url == NULL) return -1;
    hRequest = wininet_head_open(url, &hSession, &hConnect);
    if (hRequest == NULL) return -1;

    result = -1;
    dw_len = (DWORD)sizeof(headers);
    if (HttpQueryInfoA(hRequest, HTTP_QUERY_RAW_HEADERS_CRLF, headers, &dw_len, NULL)) {
        result = osr_parse_content_length(headers);
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hSession);
    return result;
}

int osr_final_url(const char *url, char *out, unsigned long out_sz) {
    HINTERNET hSession;
    HINTERNET hConnect;
    HINTERNET hRequest;
    DWORD dw_len;
    int rc;

    if (out == NULL || out_sz == 0) return OSR_NET_ERR;
    out[0] = '\0';
    if (url == NULL) return OSR_NET_ERR;

    hRequest = wininet_head_open(url, &hSession, &hConnect);
    if (hRequest == NULL) return OSR_NET_ERR;

    rc = OSR_NET_ERR;
    dw_len = (DWORD)out_sz;
    if (InternetQueryOptionA(hRequest, INTERNET_OPTION_URL, out, &dw_len)) {
        rc = OSR_NET_OK;
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hSession);
    return rc;
}

#else /* !_WIN32 */

/* No native fetch backend on this platform yet. lib/net.sh (POSIX sh,
 * curl/wget/busybox-wget) is the real implementation to consult here --
 * this branch is the landing spot for a future native port, not a claim
 * that one exists (PLAN_UNIVERSAL.md's os_linux.c branch, not built yet).
 */

int osr_net_available(void) { return 0; }

int osr_download(const char *url, const char *dest_path) {
    (void)url;
    (void)dest_path;
    return OSR_NET_UNSUPPORTED;
}

int osr_fetch_to_buffer(const char *url, char **out_buf, unsigned long *out_len) {
    (void)url;
    *out_buf = NULL;
    *out_len = 0;
    return OSR_NET_UNSUPPORTED;
}

long osr_remote_size(const char *url) {
    (void)url;
    return -1;
}

int osr_final_url(const char *url, char *out, unsigned long out_sz) {
    (void)url;
    if (out != NULL && out_sz > 0) out[0] = '\0';
    return OSR_NET_UNSUPPORTED;
}

#endif /* _WIN32 */
