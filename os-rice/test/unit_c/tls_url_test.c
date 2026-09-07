/* test/unit_c/tls_url_test.c -- lib/tls.c's URL handling.
 * Platform-independent: exercises only the code above tls.c's #ifdef _WIN32,
 * which is what decides where a redirect chain actually goes.
 */
#include "../c_test.h"
#include "../../lib/tls.h"

static void test_split(void) {
    char scheme[16];
    char host[256];
    char path[2048];
    int port;
    int ok;

    ok = osr_url_split("https://example.com/dl/foo.tar.gz",
                       scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path));
    osr_t_eq_int("split: accepts an ordinary URL", ok, 1);
    osr_t_eq_str("split: scheme", scheme, "https");
    osr_t_eq_str("split: host", host, "example.com");
    osr_t_eq_int("split: default HTTPS port", port, 443);
    osr_t_eq_str("split: path", path, "/dl/foo.tar.gz");

    ok = osr_url_split("http://example.com", scheme, sizeof(scheme),
                       host, sizeof(host), &port, path, sizeof(path));
    osr_t_eq_int("split: accepts a bare host", ok, 1);
    osr_t_eq_int("split: default HTTP port", port, 80);
    osr_t_eq_str("split: missing path becomes /", path, "/");

    ok = osr_url_split("https://example.com:8443/x", scheme, sizeof(scheme),
                       host, sizeof(host), &port, path, sizeof(path));
    osr_t_eq_int("split: explicit port", ok, 1);
    osr_t_eq_str("split: host without the port", host, "example.com");
    osr_t_eq_int("split: port as given", port, 8443);

    ok = osr_url_split("example.com/x", scheme, sizeof(scheme),
                       host, sizeof(host), &port, path, sizeof(path));
    osr_t_eq_int("split: rejects a URL with no scheme", ok, 0);

    /* Credentials in a URL would be carried across a redirect to whatever
     * host the server names, so they are refused rather than parsed. */
    ok = osr_url_split("https://user:pw@example.com/x", scheme, sizeof(scheme),
                       host, sizeof(host), &port, path, sizeof(path));
    osr_t_eq_int("split: rejects userinfo", ok, 0);

    ok = osr_url_split("https:///x", scheme, sizeof(scheme),
                       host, sizeof(host), &port, path, sizeof(path));
    osr_t_eq_int("split: rejects an empty host", ok, 0);
}

static void test_resolve(void) {
    char out[512];
    int ok;

    ok = osr_url_resolve("https://a.example/dl/x.tar", "https://b.example/y.tar",
                         out, sizeof(out));
    osr_t_eq_int("resolve: absolute Location", ok, 1);
    osr_t_eq_str("resolve: absolute Location is taken as-is", out, "https://b.example/y.tar");

    ok = osr_url_resolve("https://a.example/dl/x.tar", "/other/y.tar", out, sizeof(out));
    osr_t_eq_int("resolve: root-relative Location", ok, 1);
    osr_t_eq_str("resolve: root-relative keeps the host", out, "https://a.example/other/y.tar");

    ok = osr_url_resolve("https://a.example/dl/x.tar", "y.tar", out, sizeof(out));
    osr_t_eq_int("resolve: relative Location", ok, 1);
    osr_t_eq_str("resolve: relative resolves against the directory",
                 out, "https://a.example/dl/y.tar");

    ok = osr_url_resolve("https://a.example:8443/dl/x", "/y", out, sizeof(out));
    osr_t_eq_int("resolve: non-default port", ok, 1);
    osr_t_eq_str("resolve: non-default port is kept", out, "https://a.example:8443/y");

    ok = osr_url_resolve("https://a.example/x", "", out, sizeof(out));
    osr_t_eq_int("resolve: empty Location fails", ok, 0);

    ok = osr_url_resolve("https://a.example/dl/x", "/y", out, 12);
    osr_t_eq_int("resolve: refuses to truncate into a short buffer", ok, 0);
}

int main(void) {
    OSR_T_INIT();
    test_split();
    test_resolve();
    return osr_t_finish();
}
