/* test/unit_c/net_parse_test.c -- lib/net.c's portable parsers.
 * Platform-independent: exercises only the code outside net.c's #ifdef _WIN32.
 */
#include "../c_test.h"
#include "../../lib/net.h"

static void test_url_filename(void) {
    char out[64];

    osr_url_filename("https://example.com/dl/foo-1.2.3.tar.gz", out, sizeof(out));
    osr_t_eq_str("url_filename: plain path", out, "foo-1.2.3.tar.gz");

    osr_url_filename("https://example.com/dl/foo.zip?token=abc&x=1", out, sizeof(out));
    osr_t_eq_str("url_filename: strips query string", out, "foo.zip");

    osr_url_filename("noscheme-no-slash", out, sizeof(out));
    osr_t_eq_str("url_filename: no slash falls back to whole string", out, "noscheme-no-slash");

    osr_url_filename("https://example.com/trailing/", out, sizeof(out));
    osr_t_eq_str("url_filename: trailing slash yields empty name", out, "");
}

static void test_content_length(void) {
    long n;

    n = osr_parse_content_length("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 12345\r\n\r\n");
    osr_t_eq_int("content_length: CRLF headers", n, 12345);

    n = osr_parse_content_length("HTTP/1.1 200 OK\nContent-Length: 42\n\n");
    osr_t_eq_int("content_length: bare LF headers", n, 42);

    n = osr_parse_content_length("HTTP/1.1 200 OK\r\ncontent-length: 7\r\n\r\n");
    osr_t_eq_int("content_length: lowercase header name", n, 7);

    /* redirect chain: two header blocks concatenated, last one wins (matches
     * lib/net.sh's `tail -n 1` over a HEAD that followed redirects). */
    n = osr_parse_content_length(
        "HTTP/1.1 302 Found\r\nContent-Length: 0\r\nLocation: https://x/y\r\n\r\n"
        "HTTP/1.1 200 OK\r\nContent-Length: 999\r\n\r\n");
    osr_t_eq_int("content_length: last block in a redirect chain wins", n, 999);

    n = osr_parse_content_length("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
    osr_t_eq_int("content_length: absent header yields -1", n, -1);
}

static void test_location(void) {
    char out[256];

    osr_parse_location("HTTP/1.1 302 Found\r\nLocation: https://example.com/final\r\n\r\n", out, sizeof(out));
    osr_t_eq_str("location: simple redirect", out, "https://example.com/final");

    osr_parse_location(
        "HTTP/1.1 302 Found\r\nLocation: https://a/one\r\n\r\n"
        "HTTP/1.1 302 Found\r\nLocation: https://a/two\r\n\r\n",
        out, sizeof(out));
    osr_t_eq_str("location: last hop in a redirect chain wins", out, "https://a/two");

    osr_parse_location("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n", out, sizeof(out));
    osr_t_eq_str("location: absent header yields empty string", out, "");
}

int main(void) {
    OSR_T_INIT();
    test_url_filename();
    test_content_length();
    test_location();
    return osr_t_finish();
}
