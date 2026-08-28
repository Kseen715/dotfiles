/* lib/fetch.c -- downloading and version resolution, the C port of lib/net.sh.
 *
 * One place for "fetch a URL" and "what is the latest GitHub tag", so the
 * providers in lib/pkg.c and the builders that follow it do not each re-hand-
 * roll curl/wget (G4).
 *
 * This is the SHELL TIER's net layer, not a native one: it drives whatever
 * downloader the box has (curl, wget, busybox wget), exactly as lib/net.sh
 * did, because that is what "byte-for-byte identical" means for a unit whose
 * whole output is the command it runs. lib/net.c's #else branch stays the
 * landing spot for a future native (socket/TLS) backend; nothing here claims
 * to be one.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "common.h"
#include "module.h"
#include "net.h"
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
    const char *backend = osr_fetch_ensure();
    char *argv[5];

    if (*backend == '\0') {
        osr_warn("no downloader found (need curl, wget, or busybox)");
        return 0;
    }
    fetch_argv(backend, url, argv);
    return osr_run(argv) == 0;
}

int osr_fetch_buffer(Str *out, const char *url) {
    const char *backend = osr_fetch_ensure();
    char *argv[5];

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
    const char *backend = osr_fetch_ensure();
    char *argv[6];
    long total = expected;
    long min_bytes = env_long("OSR_PROGRESS_MIN_BYTES", PROGRESS_MIN_DEFAULT);
    long poll_secs = env_long("OSR_DOWNLOAD_POLL", 3);
    long last = -1;
    char name[512];
    pid_t pid;
    int status;
    int reaped = 0;

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

/* json_string_field -- the first "<key>": "<value>" in a JSON blob, which is
 * all github_latest's sed pattern ever extracted. */
static int json_string_field(Str *out, const char *json, const char *key) {
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

    str_init(&url);
    str_addz(&url, "https://api.github.com/repos/");
    str_addz(&url, repo);
    str_addz(&url, "/releases/latest");
    str_init(&json);
    osr_fetch_buffer(&json, str_text(&url));
    if (json_string_field(out, str_text(&json), "tag_name")) {
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
    if (json_string_field(out, str_text(&json), "name")) {
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
