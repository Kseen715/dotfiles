/* lib/fetch.c -- downloads: fetch a URL to a buffer, to a file, or into
 * another program's stdin, and resolve what a redirect points at.
 *
 * ONE UNIT, TWO I/O HALVES. POSIX shells out to curl or wget, picking whichever
 * the box has and installing one when it has neither -- which is what
 * lib/net.sh did, and the reason the argv it builds matters more than the
 * bytes it moves. Windows calls WinINet, because there is no curl to assume
 * and the system HTTP stack is already there, already knows the machine's
 * proxy, and needs no package.
 *
 * THE TWO PARSERS ARE SHARED and sit above the split: pulling a filename out
 * of a URL and a Location out of a header block are string problems, not I/O
 * problems, and they are asserted on whichever host runs the suite
 * (test/unit_c/net_parse_test.c). They were the only part of the old lib/net.c
 * that compiled off Windows, which is a good sign about where the seam is.
 *
 * OSR_NET_* is the status vocabulary both halves answer in, so a caller
 * distinguishes "no network" from "that URL 404s" without asking which
 * transport ran.
 *
 * C89 + POSIX, and C89 + Win32.
 */

#include "fetch.h"
#include "common.h"
#include "cmds.h"
#include "module.h"

/* --- the two parsers, shared -------------------------------------------- */

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

/* --- the JSON field and the version lookup over it, shared -------------
 *
 * Neither has any I/O of its own: both are string work over whatever
 * osr_fetch_buffer returned, so which transport fetched it does not reach
 * here. github_latest is what every builder resolves its version with.
 * ---------------------------------------------------------------------- */

/* osr_json_string_field -- the first "<key>": "<value>" in a JSON blob, which
 * is all github_latest's sed pattern ever extracted, and all a vendor release
 * feed's version needs either. */
int osr_json_string_field(Str *out, const char *json, const char *key) {
    Str pat;
    const char *p;
    int found = 0;

    str_init(&pat);
    str_addc(&pat, '"');
    str_addz(&pat, key);
    str_addc(&pat, '"');
    p = json;
    while ((p = strstr(p, str_text(&pat))) != NULL) {
        p += pat.len;
        while (*p != '\0' && is_space(*p)) p++;
        if (*p != ':') continue;
        p++;
        while (*p != '\0' && is_space(*p)) p++;
        if (*p != '"') continue;
        p++;
        while (*p != '\0' && *p != '"') str_addc(out, *p++);
        found = 1;
        break;
    }
    str_free(&pat);
    return found;
}

/* github_tag -- the body both entry points share. quiet drops the "could not
 * resolve" warning, which is what a caller with a fallback wants: lib/build.sh
 * spelled that `github_latest ... 2>/dev/null || _tag=""`. */
static int github_tag(Str *out, const char *repo, int quiet) {
    Str url, json;

    if (osr_theme_only()) return !osr_theme_only_skip("github_latest");

    str_init(&url);
    str_addz(&url, "https://api.github.com/repos/");
    str_addz(&url, repo);
    str_addz(&url, "/releases/latest");
    str_init(&json);
    osr_fetch_buffer(&json, str_text(&url));
    if (osr_json_string_field(out, str_text(&json), "tag_name")) {
        str_free(&json);
        str_free(&url);
        return 1;
    }
    /* A repo with no published release 404s on releases/latest; its tags
     * still answer, and the first one is the newest. */
    str_reset(&json);
    str_reset(&url);
    str_addz(&url, "https://api.github.com/repos/");
    str_addz(&url, repo);
    str_addz(&url, "/tags");
    osr_fetch_buffer(&json, str_text(&url));
    if (osr_json_string_field(out, str_text(&json), "name")) {
        str_free(&json);
        str_free(&url);
        return 1;
    }
    str_free(&json);
    str_free(&url);
    if (!quiet) osr_warnf("github_latest: could not resolve a tag for %s", repo);
    return 0;
}

int osr_github_latest(Str *out, const char *repo) { return github_tag(out, repo, 0); }
int osr_github_latest_quiet(Str *out, const char *repo) { return github_tag(out, repo, 1); }

#ifndef _WIN32

#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "common.h"
#include "module.h"
#include "fetch.h"
#include "fetch.h"

/* OSR_PROGRESS_MIN_BYTES -- below this, a download prints nothing. A meter for
 * a 30 KB tarball is noise; the number is roughly "big enough that the
 * terminal looks hung without one". */
#define PROGRESS_MIN_DEFAULT 1048576L      /* 1 MiB */

const char *osr_fetch_backend(void) {
    if (osr_have_cmd("curl")) return "curl";
    if (osr_have_cmd("wget")) return "wget";
    if (osr_have_cmd("busybox")) {
        /* `busybox wget --help` is how net.sh asks whether this busybox was
         * built with the applet at all. */
        Str out;
        int ok;
        char *probe[4];
        probe[0] = (char *)"busybox"; probe[1] = (char *)"wget";
        probe[2] = (char *)"--help"; probe[3] = NULL;
        str_init(&out);
        ok = osr_run_capture(probe, &out);
        str_free(&out);
        if (ok) return "busybox-wget";
    }
    return "";
}

const char *osr_fetch_ensure(void) {
    const char *d = osr_fetch_backend();
    const char *curl[2];

    if (*d != '\0') return d;
    /* A side-effect install: its output must not pollute a fetch stream, so
     * lib/pkg.c's installer writes where sh sent it -- stderr. */
    curl[0] = "curl";
    curl[1] = NULL;
    osr_pkg_install(curl);
    return osr_fetch_backend();
}

/* fetch_argv -- the backend's "stream this URL to stdout" invocation. */
static size_t fetch_argv(const char *backend, const char *url, char *argv[5]) {
    size_t n = 0;
    if (strcmp(backend, "curl") == 0) {
        argv[n++] = (char *)"curl"; argv[n++] = (char *)"-fsSL"; argv[n++] = (char *)url;
    } else if (strcmp(backend, "wget") == 0) {
        argv[n++] = (char *)"wget"; argv[n++] = (char *)"-qO-"; argv[n++] = (char *)url;
    } else {
        argv[n++] = (char *)"busybox"; argv[n++] = (char *)"wget";
        argv[n++] = (char *)"-qO-"; argv[n++] = (char *)url;
    }
    argv[n] = NULL;
    return n;
}

pid_t osr_fetch_child(const char *backend, const char *url, int write_fd) {
    pid_t pid;
    char *argv[5];

    fetch_argv(backend, url, argv);
    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        dup2(write_fd, 1);
        close(write_fd);
        execvp(argv[0], argv);
        _exit(127);
    }
    return pid;
}

int osr_fetch_stdout(const char *url) {
    const char *backend;
    char *argv[5];

    if (osr_theme_only()) return !osr_theme_only_skip("osr_fetch_stdout");
    backend = osr_fetch_ensure();

    if (*backend == '\0') {
        osr_warn("no downloader found (need curl, wget, or busybox)");
        return 0;
    }
    fetch_argv(backend, url, argv);
    return osr_run(argv) == 0;
}

/* fetch_pipe -- `<fetch url> | [as_user|as_root] <argv>`: a payload that is
 * piped into a command rather than saved. The one shape an upstream installer
 * script needs (`curl ... | sh -s -- -y`), and the reason osr_fetch_child
 * exists. Returns 1 when the RIGHT-hand side exited 0, which is what `$?` after
 * a pipeline gave the shell tier. */
static int fetch_pipe(const char *url, char *const argv[], int as_root) {
    const char *backend;
    int fds[2];
    pid_t dl;
    int rc, status;

    if (osr_theme_only()) return osr_theme_only_skip("osr_fetch_stdout | ...");
    backend = osr_fetch_ensure();
    if (*backend == '\0') {
        osr_warn("no downloader found (need curl, wget, or busybox)");
        return 0;
    }
    if (pipe(fds) != 0) return 0;
    dl = osr_fetch_child(backend, url, fds[1]);
    close(fds[1]);
    if (dl < 0) { close(fds[0]); return 0; }
    rc = as_root ? osr_run_root_in(argv, fds[0]) : osr_run_user_in(argv, fds[0]);
    close(fds[0]);
    waitpid(dl, &status, 0);
    return rc == 0;
}

int osr_fetch_pipe_user(const char *url, char *const argv[]) {
    return fetch_pipe(url, argv, 0);
}

int osr_fetch_pipe_root(const char *url, char *const argv[]) {
    return fetch_pipe(url, argv, 1);
}

int osr_fetch_buffer(Str *out, const char *url) {
    const char *backend;
    char *argv[5];

    if (osr_theme_only()) return !osr_theme_only_skip("osr_fetch_stdout");
    backend = osr_fetch_ensure();

    if (*backend == '\0') {
        osr_warn("no downloader found (need curl, wget, or busybox)");
        return 0;
    }
    fetch_argv(backend, url, argv);
    return osr_run_capture(argv, out);
}

/* download_argv -- the backend's "write this URL to a FILE" invocation,
 * _osr_download_run in lib/net.sh. */
static size_t download_argv(const char *backend, const char *url,
                            const char *dest, char *argv[6]) {
    size_t n = 0;
    if (strcmp(backend, "curl") == 0) {
        argv[n++] = (char *)"curl"; argv[n++] = (char *)"-fsSL";
        argv[n++] = (char *)"-o"; argv[n++] = (char *)dest; argv[n++] = (char *)url;
    } else if (strcmp(backend, "wget") == 0) {
        argv[n++] = (char *)"wget"; argv[n++] = (char *)"-qO";
        argv[n++] = (char *)dest; argv[n++] = (char *)url;
    } else {
        argv[n++] = (char *)"busybox"; argv[n++] = (char *)"wget";
        argv[n++] = (char *)"-qO"; argv[n++] = (char *)dest; argv[n++] = (char *)url;
    }
    argv[n] = NULL;
    return n;
}

/* head_block -- the response headers of a HEAD, one block per redirect hop,
 * CRs stripped. Empty when the downloader has no header-only mode (busybox
 * wget) or the host rejects a HEAD. */
static void head_block(Str *out, const char *url) {
    const char *backend = osr_fetch_backend();
    Str raw;
    char *argv[8];
    size_t n = 0, i;

    if (strcmp(backend, "curl") == 0) {
        argv[n++] = (char *)"curl"; argv[n++] = (char *)"-fsSLI";
        argv[n++] = (char *)"--max-time"; argv[n++] = (char *)"20";
        argv[n++] = (char *)url;
    } else if (strcmp(backend, "wget") == 0) {
        /* wget prints the headers on STDERR under --spider -S, which is why
         * this one is captured with stderr folded in. */
        argv[n++] = (char *)"wget"; argv[n++] = (char *)"--spider";
        argv[n++] = (char *)"-S"; argv[n++] = (char *)"--timeout=20";
        argv[n++] = (char *)"-q"; argv[n++] = (char *)"-O";
        argv[n++] = (char *)"/dev/null"; argv[n++] = (char *)url;
    } else {
        return;
    }
    argv[n] = NULL;

    str_init(&raw);
    if (strcmp(backend, "wget") == 0) {
        Str both;
        str_init(&both);
        osr_run_capture_err(argv, &both);
        str_add(&raw, str_text(&both), both.len);
        str_free(&both);
    } else {
        osr_run_capture(argv, &raw);
    }
    for (i = 0; i < raw.len; i++) {
        if (raw.p[i] != '\r') str_addc(out, raw.p[i]);
    }
    str_free(&raw);
}

long osr_fetch_remote_size(const char *url) {
    Str head;
    long size;

    str_init(&head);
    head_block(&head, url);
    /* The LAST Content-Length wins: a redirect chain prints one header block
     * per hop, and only the final hop describes the payload. */
    size = osr_parse_content_length(str_text(&head));
    str_free(&head);
    return size;
}

int osr_fetch_final_url(Str *out, const char *url) {
    Str head;
    char buf[2048];

    str_init(&head);
    head_block(&head, url);
    buf[0] = '\0';
    osr_parse_location(str_text(&head), buf, sizeof(buf));
    str_free(&head);
    if (buf[0] == '\0') return 0;
    str_addz(out, buf);
    return 1;
}

/* file_size -- bytes in a file, 0 when it does not exist yet. */
static long file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (long)st.st_size;
}

/* fit_left -- text truncated to width, tail-cropped with a "..." marker so the
 * byte counts that follow it on the line are never the part that falls off the
 * right edge (§3: ASCII, no unicode ellipsis). A width too small for even the
 * marker yields nothing, which is the honest answer on a very narrow terminal
 * -- the numbers are what matter. */
static void fit_left(Str *out, const char *text, long width) {
    size_t len = strlen(text);

    if (width < 4) return;
    if ((long)len <= width) {
        str_addz(out, text);
    } else {
        str_add(out, text, (size_t)(width - 3));
        str_addz(out, "...");
    }
}

/* progress_line -- the one line a poll prints, built exactly as net.sh's
 * printf pair built it: the fitted name, then the byte counts. */
static void progress_line(Str *out, const char *name, long now, long total, long pct) {
    Str num;
    long cols = env_long("OSR_COLS", term_cols());

    str_init(&num);
    str_addl(&num, now / 1048576);
    str_addz(&num, " MiB / ");
    str_addl(&num, total / 1048576);
    str_addz(&num, " MiB (");
    str_addl(&num, pct);
    str_addz(&num, "%)");

    str_addz(out, "  ");
    fit_left(out, name, cols - (long)num.len - 3);
    str_addc(out, ' ');
    str_add(out, str_text(&num), num.len);
    str_addc(out, '\n');
    str_free(&num);
}

int osr_fetch_download(const char *url, const char *dest, long expected) {
    const char *backend;
    char *argv[6];
    long total = expected;
    long min_bytes = env_long("OSR_PROGRESS_MIN_BYTES", PROGRESS_MIN_DEFAULT);
    long poll_secs = env_long("OSR_DOWNLOAD_POLL", 3);
    long last = -1;
    char name[512];
    pid_t pid;
    int status;
    int reaped = 0;

    if (osr_theme_only()) return !osr_theme_only_skip("osr_download");
    backend = osr_fetch_ensure();

    if (*backend == '\0') {
        osr_warn("no downloader found (need curl, wget, or busybox)");
        return 0;
    }
    download_argv(backend, url, dest, argv);
    if (total <= 0) total = osr_fetch_remote_size(url);
    if (total < 0) total = 0;
    if (total < min_bytes) return osr_run(argv) == 0;

    /* Each line names what is being fetched: several steps download something,
     * and a bare percentage in the live window says nothing about which. */
    osr_url_filename(url, name, sizeof(name));

    fflush(stdout);
    pid = fork();
    if (pid < 0) return osr_run(argv) == 0;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    /* Progress is printed as ordinary NEWLINE-terminated lines, polled off the
     * growing file rather than taken from the downloader: no backend agrees on
     * a progress format, and all of them redraw with \r on one line -- which
     * the run_step live window, being a `tail` over a logfile, renders as one
     * endless line. */
    for (;;) {
        long now, pct;
        Str line;
        pid_t done;

        /* `kill -0 "$_dl_pid"` in sh: sh's own SIGCHLD reaping is what makes
         * that probe go false, so the C form has to reap too -- a plain
         * kill(pid, 0) succeeds forever on an unreaped zombie and the loop
         * would never end. */
        done = waitpid(pid, &status, WNOHANG);
        if (done == pid || done < 0) { reaped = 1; break; }
        sleep((unsigned int)(poll_secs > 0 ? poll_secs : 1));
        now = file_size(dest);
        pct = total > 0 ? now * 100 / total : 100;
        if (pct > 100) pct = 100;
        if (pct == last) continue;
        str_init(&line);
        progress_line(&line, name, now, total, pct);
        out_flush(&line);
        str_free(&line);
        last = pct;
    }
    if (!reaped && waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int net_usage(void) {
    fputs("usage: osr net <subcommand> [args]\n\n", stderr);
    fputs("  backend                     which downloader this box has\n", stderr);
    fputs("  download <url> <dest> [n]   fetch to a file, with progress when big\n", stderr);
    fputs("  fetch <url>                 stream the payload to stdout\n", stderr);
    fputs("  size <url>                  Content-Length a HEAD reports\n", stderr);
    fputs("  final-url <url>             where a redirecting URL lands\n", stderr);
    fputs("  github-latest <owner/repo>  the newest release tag\n", stderr);
    return 2;
}

int osr_net_main(int argc, char **argv) {
    if (argc < 2) return net_usage();

    if (strcmp(argv[1], "backend") == 0 && argc == 2) {
        const char *b = osr_fetch_backend();
        printf("%s\n", b);
        return *b != '\0' ? 0 : 1;
    }
    if (strcmp(argv[1], "download") == 0 && (argc == 4 || argc == 5)) {
        long expected = argc == 5 ? atol(argv[4]) : 0;
        return osr_fetch_download(argv[2], argv[3], expected) ? 0 : 1;
    }
    if (strcmp(argv[1], "fetch") == 0 && argc == 3) {
        return osr_fetch_stdout(argv[2]) ? 0 : 1;
    }
    if (strcmp(argv[1], "size") == 0 && argc == 3) {
        long n = osr_fetch_remote_size(argv[2]);
        if (n < 0) return 1;
        printf("%ld\n", n);
        return 0;
    }
    if (strcmp(argv[1], "final-url") == 0 && argc == 3) {
        Str out;
        int ok;
        str_init(&out);
        ok = osr_fetch_final_url(&out, argv[2]);
        if (ok) printf("%s\n", str_text(&out));
        str_free(&out);
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "github-latest") == 0 && argc == 3) {
        Str out;
        int ok;
        str_init(&out);
        ok = osr_github_latest(&out, argv[2]);
        if (ok) printf("%s", str_text(&out));
        str_free(&out);
        return ok ? 0 : 1;
    }
    return net_usage();
}

#else /* _WIN32 */

/* --- the WinINet half ---------------------------------------------------- */

#include <fcntl.h>
#include <io.h>


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

/* --- the fetch.h surface, over WinINet -------------------------------------
 *
 * The same seven entry points the POSIX half publishes, so that a builder or a
 * module reads the same on either system. What has no meaning here is said
 * plainly rather than faked: there is no downloader to CHOOSE and none to
 * install, because the transport is part of the operating system.
 * ------------------------------------------------------------------------ */

const char *osr_fetch_backend(void) { return "wininet"; }
const char *osr_fetch_ensure(void)  { return "wininet"; }

int osr_fetch_buffer(Str *out, const char *url) {
    char *buf = NULL;
    unsigned long len = 0;

    if (osr_fetch_to_buffer(url, &buf, &len) != OSR_NET_OK || buf == NULL) return 0;
    str_add(out, buf, (size_t)len);
    free(buf);
    return 1;
}

int osr_fetch_stdout(const char *url) {
    Str body;
    int ok;

    str_init(&body);
    ok = osr_fetch_buffer(&body, url);
    if (ok) out_flush(&body);
    str_free(&body);
    return ok;
}

/* osr_fetch_download -- to a file, with the size check the POSIX half makes:
 * a truncated download that still exits 0 is the failure this catches, and a
 * proxy or a captive portal produces exactly that. expected <= 0 means the
 * caller does not know the size and only "did it arrive" is asked. */
int osr_fetch_download(const char *url, const char *dest, long expected) {
    if (osr_theme_only()) return !osr_theme_only_skip("osr_download");
    if (osr_download(url, dest) != OSR_NET_OK) {
        osr_warnf("download failed: %s", url);
        return 0;
    }
    if (expected > 0) {
        FILE *fp = fopen(dest, "rb");
        long got = -1;
        if (fp != NULL) {
            fseek(fp, 0, SEEK_END);
            got = ftell(fp);
            fclose(fp);
        }
        if (got != expected) {
            osr_warnf("download is %ld bytes, expected %ld: %s", got, expected, url);
            remove(dest);
            return 0;
        }
    }
    return 1;
}

int osr_fetch_final_url(Str *out, const char *url) {
    char resolved[2048];

    if (osr_final_url(url, resolved, sizeof(resolved)) != OSR_NET_OK) return 0;
    str_reset(out);
    str_addz(out, resolved);
    return 1;
}

/* osr_fetch_pipe_user / osr_fetch_pipe_root -- `<fetch> | <program>`, which on
 * POSIX is a real pipe between two processes.
 *
 * There is no fork here to hold the writing end, so the body is fetched to a
 * temp file and the file is fed to the program's stdin. The program sees the
 * same bytes on the same descriptor; what it does not see is a stream that
 * arrives while it runs, which no caller in this tree depends on -- every one
 * of them is `<installer script> | <shell>`, and a shell reads its script to
 * the end before acting anyway.
 */
static int fetch_pipe(const char *url, char *const argv[], int as_root) {
    Str tmp;
    int rc = 0;
    int fd;

    str_init(&tmp);
    str_addz(&tmp, osr_tmpdir());
    str_addz(&tmp, "/osr-pipe-");
    str_addl(&tmp, osr_pid());

    if (!osr_fetch_download(url, str_text(&tmp), 0)) {
        str_free(&tmp);
        return 0;
    }

    fd = _open(str_text(&tmp), _O_RDONLY | _O_BINARY);
    if (fd < 0) {
        osr_warnf("cannot read what was downloaded from %s", url);
        remove(str_text(&tmp));
        str_free(&tmp);
        return 0;
    }
    rc = as_root ? osr_run_root_in(argv, fd) : osr_run_user_in(argv, fd);
    _close(fd);

    remove(str_text(&tmp));
    str_free(&tmp);
    return rc == 0;
}

int osr_fetch_pipe_user(const char *url, char *const argv[]) {
    return fetch_pipe(url, argv, 0);
}

int osr_fetch_pipe_root(const char *url, char *const argv[]) {
    return fetch_pipe(url, argv, 1);
}

int osr_net_main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "get") == 0) {
        return osr_fetch_stdout(argv[2]) ? 0 : 1;
    }
    if (argc == 4 && strcmp(argv[1], "download") == 0) {
        return osr_fetch_download(argv[2], argv[3], 0) ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[1], "final-url") == 0) {
        Str out;
        int ok;
        str_init(&out);
        ok = osr_fetch_final_url(&out, argv[2]);
        if (ok) { str_addc(&out, '\n'); out_flush(&out); }
        str_free(&out);
        return ok ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[1], "github-latest") == 0) {
        Str out;
        int ok;
        str_init(&out);
        ok = osr_github_latest(&out, argv[2]);
        if (ok) { str_addc(&out, '\n'); out_flush(&out); }
        str_free(&out);
        return ok ? 0 : 1;
    }
    if (argc == 2 && strcmp(argv[1], "backend") == 0) {
        printf("%s\n", osr_fetch_backend());
        return 0;
    }

    fputs("usage: osr net <subcommand> [args]\n\n", stderr);
    fputs("  get <url>                 print the body\n", stderr);
    fputs("  download <url> <dest>     write it to a file\n", stderr);
    fputs("  final-url <url>           follow the redirects, print where it lands\n", stderr);
    fputs("  github-latest <owner/repo>  the newest release tag\n", stderr);
    fputs("  backend                   which transport this build uses\n", stderr);
    return 2;
}

#endif /* _WIN32 */
