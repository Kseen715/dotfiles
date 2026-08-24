/* lib/common.c -- see lib/common.h. C89 + POSIX. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#define _DARWIN_C_SOURCE 1

#include "common.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

#define OSR_COLS_FLOOR 20      /* below this, ui.sh falls back to 80 */
#define OSR_COLS_FALLBACK 80

/* --- Str ------------------------------------------------------------- */

void osr_die_oom(void) {
    fputs("osr: out of memory\n", stderr);
    exit(1);
}

void str_init(Str *s) {
    s->p = NULL;
    s->len = 0;
    s->cap = 0;
}

static void str_need(Str *s, size_t extra) {
    size_t want = s->len + extra + 1;
    char *np;
    if (want <= s->cap) return;
    if (s->cap == 0) s->cap = 256;
    while (s->cap < want) s->cap *= 2;
    np = (char *)realloc(s->p, s->cap);
    if (np == NULL) osr_die_oom();
    s->p = np;
}

void str_add(Str *s, const char *b, size_t n) {
    if (n == 0) return;
    str_need(s, n);
    memcpy(s->p + s->len, b, n);
    s->len += n;
    s->p[s->len] = '\0';
}

void str_addz(Str *s, const char *z) { str_add(s, z, strlen(z)); }

void str_addc(Str *s, char c) { str_add(s, &c, 1); }

void str_addl(Str *s, long n) {
    char buf[32];
    sprintf(buf, "%ld", n);
    str_addz(s, buf);
}

void str_reset(Str *s) {
    s->len = 0;
    if (s->p != NULL) s->p[0] = '\0';
}

void str_free(Str *s) {
    free(s->p);
    str_init(s);
}

const char *str_text(const Str *s) { return s->p != NULL ? s->p : ""; }

void str_trim_trailing(Str *s, char c) {
    while (s->len > 0 && s->p[s->len - 1] == c) s->p[--s->len] = '\0';
}

void str_add_squeezed(Str *s, const char *p, size_t len) {
    size_t i = 0;
    int pending = 0;   /* whitespace seen but not yet emitted */

    while (i < len && is_space(p[i])) i++;
    for (; i < len; i++) {
        if (is_space(p[i])) { pending = 1; continue; }
        /* Emitted lazily, so a trailing run adds nothing: `pending` only
         * becomes a space once another non-space byte follows it. */
        if (pending) { str_addc(s, ' '); pending = 0; }
        str_addc(s, p[i]);
    }
}

void out_flush(Str *s) {
    if (s->len > 0) fwrite(str_text(s), 1, s->len, stdout);
    fflush(stdout);
}

void err_flush(Str *s) {
    if (s->len > 0) fwrite(str_text(s), 1, s->len, stderr);
    fflush(stderr);
}

void sh_quote(Str *out, const char *value) {
    const char *p;
    str_addc(out, '\'');
    for (p = value; *p != '\0'; p++) {
        if (*p == '\'') str_addz(out, "'\\''");
        else str_addc(out, *p);
    }
    str_addc(out, '\'');
}

void sh_assign(Str *out, const char *name, const char *value) {
    str_addz(out, name);
    str_addc(out, '=');
    sh_quote(out, value);
    str_addc(out, '\n');
}

/* --- printf %b -------------------------------------------------------
 *
 * Octal is POSIX %b's `\0ddd` (up to three digits after the zero); an
 * unrecognized escape stays literal, backslash and all, which is what dash
 * and bash both do.
 */
int expand_b(Str *out, const char *s) {
    const char *p;
    for (p = s; *p != '\0'; p++) {
        if (*p != '\\') {
            str_addc(out, *p);
            continue;
        }
        p++;
        switch (*p) {
            case '\0': str_addc(out, '\\'); return 0; /* trailing lone backslash */
            case 'a':  str_addc(out, '\a'); break;
            case 'b':  str_addc(out, '\b'); break;
            case 'c':  return 1;
            case 'f':  str_addc(out, '\f'); break;
            case 'n':  str_addc(out, '\n'); break;
            case 'r':  str_addc(out, '\r'); break;
            case 't':  str_addc(out, '\t'); break;
            case 'v':  str_addc(out, '\v'); break;
            case '\\': str_addc(out, '\\'); break;
            case '0': {
                int val = 0;
                int n = 0;
                while (n < 3 && p[1] >= '0' && p[1] <= '7') {
                    val = val * 8 + (p[1] - '0');
                    p++;
                    n++;
                }
                str_addc(out, (char)val);
                break;
            }
            default:
                str_addc(out, '\\');
                str_addc(out, *p);
                break;
        }
    }
    return 0;
}

/* --- environment, terminal, palette ---------------------------------- */

const char *env_str(const char *name, const char *dflt) {
    const char *v = getenv(name);
    return (v != NULL && *v != '\0') ? v : dflt;
}

int env_is_set(const char *name) { return env_str(name, NULL) != NULL; }

long env_long(const char *name, long dflt) {
    const char *v = getenv(name);
    char *end;
    long n;
    if (v == NULL || *v == '\0') return dflt;
    n = strtol(v, &end, 10);
    if (*end != '\0') return dflt;
    return n;
}

const char *color(const char *name) { return env_str(name, ""); }

int fd_is_open(int fd) { return fcntl(fd, F_GETFD) != -1; }

int query_fd(void) { return fd_is_open(3) ? 3 : 1; }

const char *const osr_palette_names[OSR_PALETTE_COUNT] = {
    "OSR_RED", "OSR_GREEN", "OSR_YELLOW", "OSR_CYAN", "OSR_DIM", "OSR_NC"
};

const char *const *osr_palette_values(int fd) {
    static const char *const on[OSR_PALETTE_COUNT] = {
        "\\033[0;31m", "\\033[0;32m", "\\033[0;33m", "\\033[0;36m", "\\033[2m", "\\033[0m"
    };
    static const char *const off[OSR_PALETTE_COUNT] = { "", "", "", "", "", "" };
    return (isatty(fd) && !env_is_set("NO_COLOR")) ? on : off;
}

/* $COLUMNS first because that is what ncurses' tput itself honors before it
 * asks the terminal; the ioctl is what tput reports otherwise (verified
 * equal on a pty). */
long term_cols(void) {
    long cols = env_long("COLUMNS", 0);
    if (cols <= 0) {
        struct winsize ws;
        int fds[3];
        int i;
        fds[0] = query_fd();
        fds[1] = 1;
        fds[2] = 2;
        for (i = 0; i < 3; i++) {
            if (!isatty(fds[i])) continue;
            if (ioctl(fds[i], TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
                cols = (long)ws.ws_col;
                break;
            }
        }
    }
    if (cols <= 0) cols = OSR_COLS_FALLBACK;
    if (cols < OSR_COLS_FLOOR) cols = OSR_COLS_FALLBACK;
    return cols;
}

/* --- lib/log.sh's line shape ----------------------------------------- */

void tag_pad(Str *out, size_t tag_len) {
    size_t pad = tag_len;
    do {
        str_addc(out, ' ');
        pad++;
    } while (pad < OSR_TAG_WIDTH);
}

void log_line(Str *out, const char *color_env, const char *tag,
              const char *prefix, const char *msg) {
    size_t pad;
    if (expand_b(out, color(color_env))) return;
    str_addz(out, tag);
    /* %-8s: the padding sits INSIDE the colored run here, where lib/log.sh
     * put it. Trailing spaces carry no color, so it is only the byte order
     * that differs from tag_pad's. */
    for (pad = strlen(tag); pad < OSR_TAG_WIDTH; pad++) str_addc(out, ' ');
    if (expand_b(out, color("OSR_NC"))) return;
    if (prefix != NULL) str_addz(out, prefix);
    str_addz(out, msg);
    str_addc(out, '\n');
}

static void log_now(FILE *stream, const char *color_env, const char *tag, const char *msg) {
    Str out;
    str_init(&out);
    log_line(&out, color_env, tag, NULL, msg);
    if (stream == stdout) out_flush(&out);
    else err_flush(&out);
    str_free(&out);
}

void osr_info(const char *msg) { log_now(stdout, "OSR_CYAN", "[INFO]", msg); }
void osr_success_line(const char *msg) { log_now(stdout, "OSR_GREEN", "[DONE]", msg); }
void osr_warn(const char *msg) { log_now(stderr, "OSR_YELLOW", "[WARN]", msg); }
void osr_error_line(const char *msg) { log_now(stderr, "OSR_RED", "[ERROR]", msg); }

/* --- small file helpers ---------------------------------------------- */

char *slurp(const char *path, size_t *len) {
    FILE *fp;
    char *buf = NULL;
    size_t cap = 0;
    size_t used = 0;

    *len = 0;
    fp = fopen(path, "rb");
    if (fp == NULL) return NULL;
    /* Read to EOF in chunks rather than trusting a stat: /proc files report a
     * size of 0 and would come back empty, and /proc/meminfo is exactly one of
     * the things the detector reads. */
    for (;;) {
        size_t got;
        if (used + 4096 + 1 > cap) {
            char *bigger;
            cap = cap == 0 ? 8192 : cap * 2;
            bigger = (char *)realloc(buf, cap);
            if (bigger == NULL) { free(buf); fclose(fp); osr_die_oom(); }
            buf = bigger;
        }
        got = fread(buf + used, 1, 4096, fp);
        used += got;
        if (got < 4096) break;
    }
    fclose(fp);
    if (buf == NULL) {
        buf = (char *)malloc(1);
        if (buf == NULL) osr_die_oom();
    }
    buf[used] = '\0';
    *len = used;
    return buf;
}

/* file_exists -- `[ -f ]`: a regular file (or a symlink to one), not a
 * directory. */
int file_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode) ? 1 : 0;
}

int dir_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

int next_line(const char *buf, size_t buf_len, size_t *pos, Line *out) {
    size_t i;
    if (*pos >= buf_len) return 0;
    out->start = buf + *pos;
    for (i = *pos; i < buf_len && buf[i] != '\n'; i++) {
        /* scan */
    }
    out->len = i - *pos;
    out->had_newline = (i < buf_len);
    *pos = out->had_newline ? i + 1 : i;
    return 1;
}

int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

int osr_files_equal(const char *a, const char *b) {
    char *ba;
    char *bb;
    size_t la;
    size_t lb;
    int same;

    ba = slurp(a, &la);
    if (ba == NULL) return 0;
    bb = slurp(b, &lb);
    if (bb == NULL) { free(ba); return 0; }
    same = (la == lb) && (la == 0 || memcmp(ba, bb, la) == 0);
    free(ba);
    free(bb);
    return same;
}

/* --- running other programs ------------------------------------------ */

int osr_path_lookup(const char *name, Str *out) {
    const char *path = env_str("PATH", "");
    const char *p = path;
    Str candidate;
    int found = 0;

    if (strchr(name, '/') != NULL) {
        if (access(name, X_OK) != 0) return 0;
        if (out != NULL) str_addz(out, name);
        return 1;
    }
    str_init(&candidate);
    while (!found) {
        const char *colon = strchr(p, ':');
        size_t len = (colon != NULL) ? (size_t)(colon - p) : strlen(p);
        str_reset(&candidate);
        if (len == 0) str_addc(&candidate, '.');
        else str_add(&candidate, p, len);
        str_addc(&candidate, '/');
        str_addz(&candidate, name);
        if (access(str_text(&candidate), X_OK) == 0) {
            found = 1;
            if (out != NULL) str_addz(out, str_text(&candidate));
        }
        if (colon == NULL) break;
        p = colon + 1;
    }
    str_free(&candidate);
    return found;
}

int osr_run_quiet(char *const argv[]) {
    pid_t pid;
    int status;

    fflush(stdout);
    fflush(stderr);
    pid = fork();
    if (pid < 0) return 127;
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, 1);
            dup2(devnull, 2);
            close(devnull);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) return 127;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

void base_of(Str *out, const char *path) {
    size_t len = strlen(path);
    size_t end;
    size_t start;
    while (len > 0 && path[len - 1] == '/') len--;
    end = len;
    start = end;
    while (start > 0 && path[start - 1] != '/') start--;
    str_add(out, path + start, end - start);
}
