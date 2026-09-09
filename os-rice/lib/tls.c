/* lib/tls.c -- see lib/tls.h.
 *
 * C89, like the rest of this tree, and compiled as such: the vendored
 * BearSSL header this unit includes carries no `inline` at all, the
 * amalgamation script having rewritten every one to plain `static`.
 *
 * The client below is compiled only where the target needs it -- nob.c
 * defines OSR_HAVE_BEARSSL for the legacy-Windows tier and for a build that
 * asks for it (NOB_TLS=1). Everywhere else this file is just its parsers and
 * a stub that reports the transport as unavailable, which is what a build
 * whose system TLS is fine should be routing around anyway; the 63k lines of
 * thirdparty/bearssl.h are then not compiled at all.
 */
/* Feature macro before any header, as lib/fetch.c does: -std=c89 sets
 * __STRICT_ANSI__, under which glibc hides getaddrinfo and struct addrinfo. */
#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#endif

#include "tls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "fetch.h"

/* --- pure parsers: no I/O, portable, unit-testable everywhere ------------ */

static void copy_bounded(char *out, unsigned long out_sz, const char *src, unsigned long len) {
    if (out_sz == 0) return;
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, src, (size_t)len);
    out[len] = '\0';
}

int osr_url_split(const char *url,
                  char *scheme, unsigned long scheme_sz,
                  char *host, unsigned long host_sz,
                  int *port,
                  char *path, unsigned long path_sz) {
    const char *sep;
    const char *host_start;
    const char *host_end;
    const char *colon;
    const char *slash;

    if (url == NULL) return 0;
    sep = strstr(url, "://");
    if (sep == NULL) return 0;

    copy_bounded(scheme, scheme_sz, url, (unsigned long)(sep - url));
    host_start = sep + 3;

    /* userinfo@ is not something this tree's URLs carry, and honouring it
     * would mean carrying credentials through a redirect chain. Refuse
     * rather than silently reading the wrong host out of the string. */
    if (memchr(host_start, '@', strcspn(host_start, "/")) != NULL) return 0;

    slash = strchr(host_start, '/');
    host_end = (slash != NULL) ? slash : host_start + strlen(host_start);

    colon = memchr(host_start, ':', (size_t)(host_end - host_start));
    if (colon != NULL) {
        *port = atoi(colon + 1);
        if (*port <= 0 || *port > 65535) return 0;
        copy_bounded(host, host_sz, host_start, (unsigned long)(colon - host_start));
    } else {
        *port = (strcmp(scheme, "http") == 0) ? 80 : 443;
        copy_bounded(host, host_sz, host_start, (unsigned long)(host_end - host_start));
    }
    if (host[0] == '\0') return 0;

    if (slash != NULL) copy_bounded(path, path_sz, slash, (unsigned long)strlen(slash));
    else copy_bounded(path, path_sz, "/", 1);

    return 1;
}

int osr_url_resolve(const char *base, const char *location,
                    char *out, unsigned long out_sz) {
    char scheme[16];
    char host[256];
    char path[2048];
    int port;
    unsigned long n;

    if (location == NULL || location[0] == '\0' || out_sz == 0) return 0;
    out[0] = '\0';

    if (strstr(location, "://") != NULL) {
        copy_bounded(out, out_sz, location, (unsigned long)strlen(location));
        return 1;
    }
    if (!osr_url_split(base, scheme, sizeof(scheme), host, sizeof(host),
                       &port, path, sizeof(path))) {
        return 0;
    }

    n = (unsigned long)strlen(scheme) + 3 + (unsigned long)strlen(host) + 16;
    if (n >= out_sz) return 0;
    sprintf(out, "%s://%s", scheme, host);
    if (port != (strcmp(scheme, "http") == 0 ? 80 : 443)) {
        sprintf(out + strlen(out), ":%d", port);
    }

    if (location[0] == '/') {
        if (strlen(out) + strlen(location) >= out_sz) return 0;
        strcat(out, location);
        return 1;
    }

    /* A relative reference resolves against the base's directory, which is
     * everything up to and including its last '/'. */
    {
        char *last = strrchr(path, '/');
        unsigned long keep = (last != NULL) ? (unsigned long)(last - path) + 1 : 1;
        if (strlen(out) + keep + strlen(location) >= out_sz) return 0;
        strncat(out, path, (size_t)keep);
        strcat(out, location);
    }
    return 1;
}

#ifdef OSR_HAVE_BEARSSL

/* --- the client ---------------------------------------------------------- */

#ifdef _WIN32
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
/* BSD sockets say the same three things with other names. */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define closesocket(s) close(s)
#endif

#include "../thirdparty/bearssl.h"

/* The CA bundle, turned into an array by nob (build/cacert_pem.c out of
 * thirdparty/cacert.pem). Declared rather than included so that this unit
 * does not have to exist twice for the POSIX build. */
extern const unsigned char osr_cacert_pem[];
extern const unsigned long osr_cacert_pem_len;

#define MAX_REDIRECTS 5

int osr_tls_available(void) { return 1; }

/* osr_tls_needed -- is this system's own HTTPS unusable, so that the client
 * below has to carry the request?
 *
 * Windows answers from the version: pre-Vista schannel stops at TLS 1.0.
 * POSIX has no such version test -- whether the box has a working downloader
 * at all is a fetch.c question, and that is where it is asked (see
 * use_own_tls there) -- so here only OSR_TLS=bearssl forces it. */
int osr_tls_needed(void) {
    const char *forced = env_str("OSR_TLS", "");
#ifdef _WIN32
    OSVERSIONINFOA vi;
#endif

    if (strcmp(forced, "bearssl") == 0) return 1;
    if (strcmp(forced, "system") == 0) return 0;
#ifndef _WIN32
    return 0;
#else
    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
    /* GetVersionExA is deprecated on 8.1 and later, where it reports 6.2 for
     * anything newer -- which is fine, because the only question asked here
     * is "older than Vista", and every version that lies about itself is
     * well past that. */
    if (!GetVersionExA(&vi)) return 0;
    return vi.dwMajorVersion < 6;
#endif
}

/* --- trust anchors, decoded from the bundle at first use ----------------- */

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} Blob;

static int blob_add(Blob *b, const void *data, size_t len) {
    if (b->len + len > b->cap) {
        size_t cap = (b->cap == 0) ? 1024 : b->cap;
        unsigned char *grown;
        while (cap < b->len + len) cap *= 2;
        grown = (unsigned char *)realloc(b->data, cap);
        if (grown == NULL) return 0;
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    return 1;
}

static void blob_sink(void *ctx, const void *data, size_t len) {
    blob_add((Blob *)ctx, data, len);
}

static br_x509_trust_anchor *g_anchors = NULL;
static size_t g_anchor_count = 0;
static int g_anchors_tried = 0;

/* add_anchor -- one DER certificate from the bundle into the anchor array.
 * A root this build's BearSSL cannot represent (an unsupported key type) is
 * dropped rather than failing the load: the bundle is a hundred-odd roots and
 * the ones that matter are RSA and EC. */
static int add_anchor(const unsigned char *der, size_t der_len) {
    br_x509_decoder_context dc;
    Blob dn;
    br_x509_pkey *pk;
    br_x509_trust_anchor ta;
    br_x509_trust_anchor *grown;

    memset(&dn, 0, sizeof(dn));
    br_x509_decoder_init(&dc, blob_sink, &dn);
    br_x509_decoder_push(&dc, der, der_len);
    pk = br_x509_decoder_get_pkey(&dc);
    if (pk == NULL) { free(dn.data); return 0; }

    memset(&ta, 0, sizeof(ta));
    ta.dn.data = dn.data;
    ta.dn.len = dn.len;
    ta.flags = br_x509_decoder_isCA(&dc) ? BR_X509_TA_CA : 0;

    switch (pk->key_type) {
    case BR_KEYTYPE_RSA:
        ta.pkey.key_type = BR_KEYTYPE_RSA;
        ta.pkey.key.rsa.nlen = pk->key.rsa.nlen;
        ta.pkey.key.rsa.elen = pk->key.rsa.elen;
        ta.pkey.key.rsa.n = (unsigned char *)malloc(pk->key.rsa.nlen);
        ta.pkey.key.rsa.e = (unsigned char *)malloc(pk->key.rsa.elen);
        if (ta.pkey.key.rsa.n == NULL || ta.pkey.key.rsa.e == NULL) {
            free(ta.pkey.key.rsa.n);
            free(ta.pkey.key.rsa.e);
            free(dn.data);
            return 0;
        }
        memcpy(ta.pkey.key.rsa.n, pk->key.rsa.n, pk->key.rsa.nlen);
        memcpy(ta.pkey.key.rsa.e, pk->key.rsa.e, pk->key.rsa.elen);
        break;
    case BR_KEYTYPE_EC:
        ta.pkey.key_type = BR_KEYTYPE_EC;
        ta.pkey.key.ec.curve = pk->key.ec.curve;
        ta.pkey.key.ec.qlen = pk->key.ec.qlen;
        ta.pkey.key.ec.q = (unsigned char *)malloc(pk->key.ec.qlen);
        if (ta.pkey.key.ec.q == NULL) { free(dn.data); return 0; }
        memcpy(ta.pkey.key.ec.q, pk->key.ec.q, pk->key.ec.qlen);
        break;
    default:
        free(dn.data);
        return 0;
    }

    grown = (br_x509_trust_anchor *)realloc(g_anchors,
                (g_anchor_count + 1) * sizeof(br_x509_trust_anchor));
    if (grown == NULL) { free(dn.data); return 0; }
    g_anchors = grown;
    g_anchors[g_anchor_count++] = ta;
    return 1;
}

/* load_anchors -- decode the whole compiled-in bundle once. ~120 roots, a
 * few milliseconds even on the hardware this exists for, and only paid by a
 * process that actually fetches something. */
static int load_anchors(void) {
    br_pem_decoder_context pc;
    Blob der;
    size_t off;
    int in_cert;

    if (g_anchors_tried) return g_anchor_count > 0;
    g_anchors_tried = 1;

    memset(&der, 0, sizeof(der));
    br_pem_decoder_init(&pc);
    in_cert = 0;
    off = 0;
    while (off < (size_t)osr_cacert_pem_len) {
        size_t pushed = br_pem_decoder_push(&pc, osr_cacert_pem + off,
                                            (size_t)osr_cacert_pem_len - off);
        off += pushed;
        switch (br_pem_decoder_event(&pc)) {
        case BR_PEM_BEGIN_OBJ:
            in_cert = (strcmp(br_pem_decoder_name(&pc), "CERTIFICATE") == 0);
            der.len = 0;
            if (in_cert) br_pem_decoder_setdest(&pc, blob_sink, &der);
            else br_pem_decoder_setdest(&pc, NULL, NULL);
            break;
        case BR_PEM_END_OBJ:
            if (in_cert && der.len > 0) add_anchor(der.data, der.len);
            in_cert = 0;
            break;
        case BR_PEM_ERROR:
            free(der.data);
            osr_warnf("CA bundle is not readable PEM -- HTTPS will not work");
            return 0;
        default:
            break;
        }
    }
    free(der.data);
    if (g_anchor_count == 0) osr_warnf("CA bundle held no usable roots");
    return g_anchor_count > 0;
}

/* --- socket -------------------------------------------------------------- */

static int winsock_up(void) {
#ifdef _WIN32
    static int started = 0;
    WSADATA wsa;

    if (started) return 1;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
    started = 1;
#endif
    return 1;   /* nothing to start on POSIX */
}

/* sock_deadline -- a receive/send timeout on the socket.
 *
 * Without one, a host that accepts the connection and then says nothing
 * leaves br_sslio_read() blocked forever, and the program with it -- which is
 * how an offline box with a captive portal, or a firewall that drops rather
 * than rejects, actually behaves. The system transports this one stands in
 * for all have such a timeout (curl's, WinINet's); this is that.
 * ponytail: it does not bound connect(), which the OS times out on its own
 * after a minute or two; make it non-blocking with a select() if that wait
 * ever matters. */
#define TLS_TIMEOUT_SECS 30

static void sock_deadline(SOCKET sock) {
#ifdef _WIN32
    DWORD ms = TLS_TIMEOUT_SECS * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof(ms));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms));
#else
    struct timeval tv;
    tv.tv_sec = TLS_TIMEOUT_SECS;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static SOCKET tcp_connect(const char *host, int port) {
    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *p;
    char service[16];
    SOCKET sock;

    if (!winsock_up()) return INVALID_SOCKET;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    sprintf(service, "%d", port);

    res = NULL;
    if (getaddrinfo(host, service, &hints, &res) != 0 || res == NULL) return INVALID_SOCKET;

    sock = INVALID_SOCKET;
    for (p = res; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == INVALID_SOCKET) continue;
        if (connect(sock, p->ai_addr, (int)p->ai_addrlen) == 0) break;
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (sock != INVALID_SOCKET) sock_deadline(sock);
    return sock;
}

static int sock_read(void *ctx, unsigned char *buf, size_t len) {
    SOCKET sock = *(SOCKET *)ctx;
    int got = (int)recv(sock, (char *)buf, len, 0);
    /* A clean close is an end of stream to BearSSL's I/O layer just as an
     * error is: both mean nothing more will arrive. */
    if (got <= 0) return -1;
    return got;
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len) {
    SOCKET sock = *(SOCKET *)ctx;
    int sent = (int)send(sock, (const char *)buf, len, 0);
    if (sent <= 0) return -1;
    return sent;
}

/* --- one request --------------------------------------------------------- */

typedef struct {
    int status;
    long content_length;
    char location[2048];
} Response;

/* http_request -- the request line and the headers.
 *
 * HTTP/1.0, deliberately: a 1.0 client may not be sent a chunked body, so
 * the response is either Content-Length bytes or everything up to the close,
 * and this unit needs no chunked decoder.
 * ponytail: costs keep-alive, which a one-shot download does not want anyway;
 * move to 1.1 and add a de-chunker if a server ever needs it.
 */
static void http_request(char *out, size_t out_sz, const char *method,
                         const char *host, const char *path) {
    sprintf(out, "%s %.1500s HTTP/1.0\r\n"
                 "Host: %.250s\r\n"
                 "User-Agent: os-rice/1.0\r\n"
                 "Accept: */*\r\n"
                 "Connection: close\r\n\r\n",
            method, path, host);
    (void)out_sz;   /* the %.Ns widths above are what bounds this, not out_sz */
}

static void parse_response_head(const char *head, Response *rsp) {
    const char *sp;

    rsp->status = 0;
    rsp->content_length = osr_parse_content_length(head);
    osr_parse_location(head, rsp->location, sizeof(rsp->location));

    sp = strchr(head, ' ');
    if (sp != NULL) rsp->status = atoi(sp + 1);
}

/* https_once -- connect, send, read one response. Body goes to sink (NULL
 * for a HEAD, which asks for nothing after the headers). */
static int https_once(const char *host, int port, const char *path,
                      const char *method, osr_tls_sink sink, void *ctx,
                      Response *rsp) {
    static unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context ioc;
    SOCKET sock;
    char request[2048];
    char head[16384];
    size_t head_len;
    int rc;

    if (!load_anchors()) return OSR_NET_ERR;

    sock = tcp_connect(host, port);
    if (sock == INVALID_SOCKET) {
        osr_warnf("cannot reach %s:%d", host, port);
        return OSR_NET_ERR;
    }

    br_ssl_client_init_full(&sc, &xc, g_anchors, g_anchor_count);
    br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof(iobuf), 1);
    br_ssl_client_reset(&sc, host, 0);
    br_sslio_init(&ioc, &sc.eng, sock_read, &sock, sock_write, &sock);

    http_request(request, sizeof(request), method, host, path);
    if (br_sslio_write_all(&ioc, request, strlen(request)) != 0 ||
        br_sslio_flush(&ioc) != 0) {
        osr_warnf("TLS handshake with %s failed (error %d)",
                  host, br_ssl_engine_last_error(&sc.eng));
        closesocket(sock);
        return OSR_NET_ERR;
    }

    /* Headers, one byte at a time: the engine buffers, so this is a memcpy
     * per byte over a kilobyte or two, and it stops exactly at the blank
     * line without reading a byte of the body it would then have to hold. */
    head_len = 0;
    rc = OSR_NET_ERR;
    for (;;) {
        unsigned char c;
        if (br_sslio_read(&ioc, &c, 1) < 0) break;
        if (head_len + 1 >= sizeof(head)) break;
        head[head_len++] = (char)c;
        if (head_len >= 4 && memcmp(head + head_len - 4, "\r\n\r\n", 4) == 0) {
            rc = OSR_NET_OK;
            break;
        }
    }
    head[head_len] = '\0';

    if (rc != OSR_NET_OK) {
        osr_warnf("no HTTP response from %s (TLS error %d)",
                  host, br_ssl_engine_last_error(&sc.eng));
        closesocket(sock);
        return OSR_NET_ERR;
    }
    parse_response_head(head, rsp);

    if (sink != NULL && rsp->status >= 200 && rsp->status < 300) {
        char body[8192];
        for (;;) {
            int got = br_sslio_read(&ioc, body, sizeof(body));
            if (got <= 0) break;
            if (!sink(ctx, body, (unsigned long)got)) { rc = OSR_NET_ERR; break; }
        }
        /* A server that closes the connection without a close_notify is
         * ordinary on the public web; BR_ERR_IO here means the body ended,
         * not that it was tampered with -- the size check in
         * osr_fetch_download is what catches a truncated file. */
    }

    br_sslio_close(&ioc);
    closesocket(sock);
    return rc;
}

int osr_tls_get(const char *url, osr_tls_sink sink, void *ctx,
                char *final_url, unsigned long final_sz, long *content_length) {
    char current[2048];
    int hop;

    if (final_url != NULL && final_sz > 0) final_url[0] = '\0';
    if (content_length != NULL) *content_length = -1;
    if (url == NULL) return OSR_NET_ERR;

    copy_bounded(current, sizeof(current), url, (unsigned long)strlen(url));

    for (hop = 0; hop <= MAX_REDIRECTS; hop++) {
        char scheme[16];
        char host[256];
        char path[2048];
        int port;
        Response rsp;
        int rc;

        if (!osr_url_split(current, scheme, sizeof(scheme), host, sizeof(host),
                           &port, path, sizeof(path))) {
            osr_warnf("cannot parse URL: %s", current);
            return OSR_NET_ERR;
        }
        if (strcmp(scheme, "https") != 0) {
            osr_warnf("this transport speaks HTTPS only, not %s: %s", scheme, current);
            return OSR_NET_ERR;
        }

        memset(&rsp, 0, sizeof(rsp));
        rc = https_once(host, port, path, sink == NULL ? "HEAD" : "GET", sink, ctx, &rsp);
        if (rc != OSR_NET_OK) return rc;

        if (rsp.status >= 300 && rsp.status < 400 && rsp.location[0] != '\0') {
            char next[2048];
            if (!osr_url_resolve(current, rsp.location, next, sizeof(next))) {
                osr_warnf("cannot follow redirect to: %s", rsp.location);
                return OSR_NET_ERR;
            }
            memcpy(current, next, sizeof(current) < sizeof(next) ? sizeof(current) : sizeof(next));
            current[sizeof(current) - 1] = '\0';
            continue;
        }

        if (final_url != NULL) copy_bounded(final_url, final_sz, current, (unsigned long)strlen(current));
        if (content_length != NULL) *content_length = rsp.content_length;
        if (rsp.status < 200 || rsp.status >= 300) {
            osr_warnf("HTTP %d for %s", rsp.status, current);
            return OSR_NET_ERR;
        }
        return OSR_NET_OK;
    }

    osr_warnf("too many redirects starting at %s", url);
    return OSR_NET_ERR;
}

#else /* !OSR_HAVE_BEARSSL */

/* POSIX runs curl or wget, and modern Windows has WinINet: both brought a
 * current TLS with them, so neither needs this one built in. Only the parsers
 * above are built here, and the suite asserts them on whichever host runs it. */

int osr_tls_available(void) { return 0; }
int osr_tls_needed(void) { return 0; }

int osr_tls_get(const char *url, osr_tls_sink sink, void *ctx,
                char *final_url, unsigned long final_sz, long *content_length) {
    (void)url; (void)sink; (void)ctx;
    if (final_url != NULL && final_sz > 0) final_url[0] = '\0';
    if (content_length != NULL) *content_length = -1;
    return OSR_NET_UNSUPPORTED;
}

#endif /* OSR_HAVE_BEARSSL */
