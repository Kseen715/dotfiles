/* lib/common.c -- see lib/common.h. C89 + POSIX, and C89 + Win32 where the
 * two systems answer the same question differently.
 *
 * This unit is compiled into BOTH cores. Everything above the platform
 * section at the bottom -- Str, the environment readings, printf %b, the log
 * line shape -- is plain C89 and byte-identical on either side; only the
 * handful of questions a kernel has to answer (is this descriptor open, how
 * wide is the terminal, does this name resolve on PATH, run it quietly, paint
 * a tag in color) have two implementations, and they sit together under one
 * #ifdef rather than being scattered through the file.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#define _DARWIN_C_SOURCE 1
#endif

#include "common.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
/* ENABLE_VIRTUAL_TERMINAL_PROCESSING, spelled out rather than included: the
 * mingw headers that ship with older toolchains predate it, and this core is
 * meant to build with whatever compiler the machine already has. The value is
 * the documented one and has never changed. */
#define OSR_ENABLE_VT 0x0004
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

const char *const osr_palette_names[OSR_PALETTE_COUNT] = {
    "OSR_RED", "OSR_GREEN", "OSR_YELLOW", "OSR_CYAN", "OSR_DIM", "OSR_NC"
};

const char *const *osr_palette_values(int fd) {
    static const char *const on[OSR_PALETTE_COUNT] = {
        "\\033[0;31m", "\\033[0;32m", "\\033[0;33m", "\\033[0;36m", "\\033[2m", "\\033[0m"
    };
    static const char *const off[OSR_PALETTE_COUNT] = { "", "", "", "", "", "" };
    return (osr_color_mode(fd) == OSR_COLOR_ANSI) ? on : off;
}

/* $COLUMNS first because that is what ncurses' tput itself honors before it
 * asks the terminal; asking the kernel (or the console) is what tput reports
 * otherwise (verified equal on a pty). The asking half is osr_term_cols_raw,
 * down in the platform section; the policy -- the environment override and
 * the two floors ui.sh applied -- is the same on both systems and lives here.
 */
long term_cols(void) {
    long cols = env_long("COLUMNS", 0);
    if (cols <= 0) cols = osr_term_cols_raw();
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

/* log_now -- one whole log line, printed now.
 *
 * Two ways to get color onto a line, and which one is available is a property
 * of the terminal rather than of the operating system: osr_color_mode answers
 * ANSI (every POSIX terminal, and a Windows console new enough to have virtual
 * terminal processing), CLASSIC (a Windows console that has to be painted
 * through the console API -- XP and 7 interpret no escape sequence at all), or
 * NONE (a pipe, a file, NO_COLOR). ANSI and NONE are the same code path,
 * because the palette is simply empty in the NONE case, which is exactly how
 * lib/ui.sh arranged it; CLASSIC has to interleave a system call with the
 * write, so it is the one branch that cannot compose the line first.
 */
static void log_now(FILE *stream, const char *color_env, const char *tag, const char *msg) {
    Str out;

    if (osr_color_mode(stream == stdout ? 1 : 2) == OSR_COLOR_CLASSIC) {
        osr_paint_tagged(stream, color_env, tag, NULL, msg);
        return;
    }
    str_init(&out);
    log_line(&out, color_env, tag, NULL, msg);
    if (stream == stdout) out_flush(&out);
    else err_flush(&out);
    str_free(&out);
}

/* --- the printf-style five ---------------------------------------------
 *
 * The same five lines again, taking a format instead of a finished string.
 * Both cores print through them (a module says osr_warnf, the Windows package
 * dispatch says osr_warnf), so they live beside the line shape they use rather
 * than in either core's own files -- which is what they did before this file
 * was shared: one copy in lib/module.c, another in lib/winui.c, and nothing
 * making the two agree.
 *
 * The text is rendered into a bounded buffer and then passed as data: a log
 * message is never itself a format string.
 */
static void say(void (*emit)(const char *), const char *fmt, va_list ap) {
    char buf[OSR_LOG_MSG_MAX];
    vsprintf(buf, fmt, ap);
    emit(buf);
}

void osr_infof(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    say(osr_info, fmt, ap);
    va_end(ap);
}

void osr_debugf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    say(osr_debug_line, fmt, ap);
    va_end(ap);
}

void osr_warnf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    say(osr_warn, fmt, ap);
    va_end(ap);
}

void osr_successf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    say(osr_success_line, fmt, ap);
    va_end(ap);
}

/* osr_die -- lib/log.sh's error(): the one fatal path. Prints, then exits. */
void osr_die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    say(osr_error_line, fmt, ap);
    va_end(ap);
    exit(1);
}

/* osr_stepf -- osr_infof carrying the "[03/12] " counter install.sh prints in
 * front of every module. total == 0 omits the prefix entirely, which is what
 * makes a standalone `osr module foo` print no counter at all. */
void osr_stepf(unsigned long n, unsigned long total, const char *fmt, ...) {
    char msg[OSR_LOG_MSG_MAX];
    char prefix[32];
    va_list ap;
    Str out;

    va_start(ap, fmt);
    vsprintf(msg, fmt, ap);
    va_end(ap);

    if (total > 0) sprintf(prefix, "[%02lu/%02lu] ", n, total);
    else           prefix[0] = '\0';

    if (osr_color_mode(1) == OSR_COLOR_CLASSIC) {
        osr_paint_tagged(stdout, "OSR_CYAN", "[INFO]", prefix, msg);
        return;
    }
    str_init(&out);
    log_line(&out, "OSR_CYAN", "[INFO]", prefix, msg);
    out_flush(&out);
    str_free(&out);
}

void osr_info(const char *msg) { log_now(stdout, "OSR_CYAN", "[INFO]", msg); }
void osr_success_line(const char *msg) { log_now(stdout, "OSR_GREEN", "[DONE]", msg); }
void osr_warn(const char *msg) { log_now(stderr, "OSR_YELLOW", "[WARN]", msg); }
/* osr_debug_line -- off unless OSR_DEBUG is set: a theme apply skips dozens of
 * steps by design, and printing each one would bury the handful of lines that
 * say what actually changed. */
void osr_debug_line(const char *msg) {
    if (env_is_set("OSR_DEBUG")) log_now(stderr, "OSR_DIM", "[DEBUG]", msg);
}

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

/* ---------------------------------------------------------------------
 * the platform section
 *
 * Everything above is the same C on both systems. Below are the questions
 * only a kernel can answer, each with one POSIX body and one Win32 body,
 * kept together so that "what actually differs between the two cores" is a
 * single block a reader can hold in their head rather than a search.
 * ------------------------------------------------------------------ */

#ifndef _WIN32

int fd_is_open(int fd) { return fcntl(fd, F_GETFD) != -1; }

/* query_fd -- fd 3 when a caller handed the real terminal over on it (ui.sh's
 * `exec 3>&1` trick, still used by anything reading us inside a `$(...)`),
 * else plain stdout. */
int query_fd(void) { return fd_is_open(3) ? 3 : 1; }

int osr_color_mode(int fd) {
    if (env_is_set("NO_COLOR")) return OSR_COLOR_NONE;
    return isatty(fd) ? OSR_COLOR_ANSI : OSR_COLOR_NONE;
}

/* osr_paint_tagged -- never reached on POSIX: OSR_COLOR_CLASSIC is a Windows
 * console state and osr_color_mode above cannot return it. Defined anyway, as
 * the ordinary composed line, so the shared callers need no #ifdef. */
void osr_paint_tagged(FILE *stream, const char *color_env, const char *tag,
                      const char *prefix, const char *msg) {
    Str out;
    str_init(&out);
    log_line(&out, color_env, tag, prefix, msg);
    if (stream == stdout) out_flush(&out);
    else err_flush(&out);
    str_free(&out);
}

long osr_term_cols_raw(void) {
    struct winsize ws;
    int fds[3];
    int i;

    fds[0] = query_fd();
    fds[1] = 1;
    fds[2] = 2;
    for (i = 0; i < 3; i++) {
        if (!isatty(fds[i])) continue;
        if (ioctl(fds[i], TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return (long)ws.ws_col;
    }
    return 0;
}

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

#else /* _WIN32 */

int fd_is_open(int fd) {
    return _get_osfhandle(fd) != (intptr_t)INVALID_HANDLE_VALUE;
}

/* query_fd -- there is no `exec 3>&1` here: nothing wraps this core in a
 * shell, so the terminal question is always asked of stdout. */
int query_fd(void) { return 1; }

/* std_handle -- the console handle behind a C descriptor, or
 * INVALID_HANDLE_VALUE when that descriptor is a pipe or a file. */
static HANDLE std_handle(int fd) {
    DWORD which = (fd == 2) ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
    HANDLE h = GetStdHandle(which);
    if (h == NULL || h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    if (GetFileType(h) != FILE_TYPE_CHAR) return INVALID_HANDLE_VALUE;
    return h;
}

/* osr_color_mode -- which of the two ways of coloring a line this stream can
 * take.
 *
 * ANSI is preferred wherever it can be had, because it is the shape lib/ui.sh
 * defined and the one the whole palette is written in: a Windows 10 console
 * accepts it as soon as ENABLE_VIRTUAL_TERMINAL_PROCESSING is set, so this
 * asks for that once and reports ANSI if the console agreed. A console that
 * refuses is XP or 7, which interprets no escape sequence at all -- there the
 * only way to color anything is the classic console API, and printing escapes
 * would put literal `ESC[0;36m` in front of every line. Anything that is not
 * a console (a pipe, a redirect, NO_COLOR) gets no color either way.
 *
 * Cached per stream: SetConsoleMode is a system call, and this is asked once
 * per printed line.
 */
int osr_color_mode(int fd) {
    static int cached[3] = { -1, -1, -1 };
    int slot = (fd == 2) ? 2 : 1;
    HANDLE h;
    DWORD mode;

    if (env_is_set("NO_COLOR")) return OSR_COLOR_NONE;
    if (cached[slot] >= 0) return cached[slot];

    h = std_handle(slot);
    if (h == INVALID_HANDLE_VALUE) {
        cached[slot] = OSR_COLOR_NONE;
    } else if (GetConsoleMode(h, &mode) && SetConsoleMode(h, mode | OSR_ENABLE_VT)) {
        cached[slot] = OSR_COLOR_ANSI;
    } else {
        cached[slot] = OSR_COLOR_CLASSIC;
    }
    return cached[slot];
}

/* classic_attr -- the console attribute standing in for one palette color.
 * Keyed by the palette variable rather than by the tag, so the mapping stays
 * the one lib/ui.sh already published and a new tag needs no entry here. */
static WORD classic_attr(const char *color_env) {
    if (strcmp(color_env, "OSR_RED") == 0)    return FOREGROUND_RED | FOREGROUND_INTENSITY;
    if (strcmp(color_env, "OSR_GREEN") == 0)  return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    if (strcmp(color_env, "OSR_YELLOW") == 0) return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    if (strcmp(color_env, "OSR_CYAN") == 0)   return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    if (strcmp(color_env, "OSR_DIM") == 0)    return FOREGROUND_INTENSITY;
    return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
}

/* osr_paint_tagged -- log_line's shape written through the console API: the
 * tag colored and padded to OSR_TAG_WIDTH, then the step prefix and the
 * message in the console's own colors. The same bytes as the ANSI branch,
 * minus the escapes this terminal could not have read. */
void osr_paint_tagged(FILE *stream, const char *color_env, const char *tag,
                      const char *prefix, const char *msg) {
    HANDLE h = std_handle(stream == stdout ? 1 : 2);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int restore = (h != INVALID_HANDLE_VALUE) && GetConsoleScreenBufferInfo(h, &csbi);

    fflush(stream);
    if (restore) SetConsoleTextAttribute(h, classic_attr(color_env));
    fprintf(stream, "%-*s", OSR_TAG_WIDTH, tag);
    fflush(stream);
    if (restore) SetConsoleTextAttribute(h, csbi.wAttributes);

    if (prefix != NULL) fputs(prefix, stream);
    fputs(msg, stream);
    fputc('\n', stream);
    fflush(stream);
}

long osr_term_cols_raw(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int fds[2];
    int i;

    fds[0] = 1;
    fds[1] = 2;
    for (i = 0; i < 2; i++) {
        HANDLE h = std_handle(fds[i]);
        if (h == INVALID_HANDLE_VALUE) continue;
        if (GetConsoleScreenBufferInfo(h, &csbi)) {
            long cols = (long)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
            if (cols > 0) return cols;
        }
    }
    return 0;
}

/* osr_path_lookup -- `command -v <name>`, with the two differences Windows
 * makes to that question: the search list is separated by ';' rather than
 * ':', and a program is identified by an extension out of %PATHEXT% rather
 * than by a mode bit. SearchPath is the resolver CreateProcess itself uses,
 * so a hit here is a promise that running the name finds the same file; the
 * PATHEXT loop is the fallback for a bare name SearchPath would not complete
 * on its own.
 */
int osr_path_lookup(const char *name, Str *out) {
    static const char *const dflt_exts = ".COM;.EXE;.BAT;.CMD";
    char found[MAX_PATH];
    size_t name_len = strlen(name);
    DWORD n;

    n = SearchPathA(NULL, name, NULL, (DWORD)sizeof(found), found, NULL);
    if (n == 0 || n >= sizeof(found)) {
        const char *exts = env_str("PATHEXT", dflt_exts);
        const char *p;
        int hit = 0;

        for (p = exts; *p != '\0' && !hit; ) {
            const char *semi = strchr(p, ';');
            size_t len = (semi != NULL) ? (size_t)(semi - p) : strlen(p);
            char with_ext[MAX_PATH];

            if (len > 0 && name_len + len < sizeof(with_ext)) {
                memcpy(with_ext, name, name_len);
                memcpy(with_ext + name_len, p, len);
                with_ext[name_len + len] = '\0';
                n = SearchPathA(NULL, with_ext, NULL, (DWORD)sizeof(found), found, NULL);
                if (n > 0 && n < sizeof(found)) hit = 1;
            }
            if (semi == NULL) break;
            p = semi + 1;
        }
        if (!hit) return 0;
    }
    if (out != NULL) str_addz(out, found);
    return 1;
}

/* osr_run_quiet -- the `>/dev/null 2>&1` probe, spawned rather than forked:
 * there is no fork here, and _spawnvp is the one call that takes the same
 * argv vector the POSIX branch does, so a caller composes one command for
 * both systems. NUL is this machine's /dev/null.
 */
int osr_run_quiet(char *const argv[]) {
    int saved_out, saved_err, devnull;
    intptr_t rc;

    fflush(stdout);
    fflush(stderr);

    saved_out = _dup(1);
    saved_err = _dup(2);
    devnull = _open("NUL", _O_WRONLY);
    if (devnull >= 0) {
        _dup2(devnull, 1);
        _dup2(devnull, 2);
        _close(devnull);
    }

    rc = _spawnvp(_P_WAIT, argv[0], (const char *const *)argv);

    if (saved_out >= 0) { _dup2(saved_out, 1); _close(saved_out); }
    if (saved_err >= 0) { _dup2(saved_err, 2); _close(saved_err); }

    return (rc < 0) ? 127 : (int)rc;
}

#endif /* _WIN32 */

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

/* ---------------------------------------------------------------------
 * bounded path helpers
 *
 * The Str type above is what most of this tree composes paths with, but a
 * module or a builder frequently has a fixed buffer and one path to put in
 * it -- and a truncated path is a command that acts on the wrong file. These
 * four say so by returning 0 and leaving the buffer empty rather than by
 * handing back a shortened path that still looks usable.
 *
 * They were lib/config_copy.c and modules/src/common.c, one copy in the
 * Windows core and one in its module helpers, back when the two cores shared
 * no code. Both callers are here now.
 * ------------------------------------------------------------------ */

int osr_copy_bounded(char *dst, unsigned long dst_sz, const char *src) {
    unsigned long len = (unsigned long)strlen(src);
    if (dst_sz == 0) return 0;
    if (len >= dst_sz) { dst[0] = '\0'; return 0; }
    memcpy(dst, src, len);
    dst[len] = '\0';
    return 1;
}

int osr_append_bounded(char *dst, unsigned long dst_sz, const char *suffix) {
    unsigned long cur = (unsigned long)strlen(dst);
    unsigned long add = (unsigned long)strlen(suffix);
    if (cur + add >= dst_sz) { dst[0] = '\0'; return 0; }
    memcpy(dst + cur, suffix, add);
    dst[cur + add] = '\0';
    return 1;
}

/* osr_path_join -- a + separator + b. '/' is the separator written, on both
 * systems: every Windows API this tree calls accepts it, and one separator
 * keeps a path comparable no matter which half of the tree composed it.
 * An `a` that already ends in either separator does not get a second one. */
int osr_path_join(char *out, unsigned long out_sz, const char *a, const char *b) {
    unsigned long len_a = (unsigned long)strlen(a);
    unsigned long len_b = (unsigned long)strlen(b);
    unsigned long need;
    int has_sep = (len_a > 0 && (a[len_a - 1] == '/' || a[len_a - 1] == '\\'));

    if (out_sz == 0) return 0;
    need = len_a + (has_sep ? 0 : 1) + len_b;
    if (need >= out_sz) { out[0] = '\0'; return 0; }

    memcpy(out, a, len_a);
    if (!has_sep) out[len_a] = '/';
    memcpy(out + len_a + (has_sep ? 0 : 1), b, len_b);
    out[need] = '\0';
    return 1;
}

/* osr_dirname -- the directory holding path, "." when it has no separator.
 * Both separators are checked because argv[0] on Windows carries whichever
 * one the caller typed. */
void osr_dirname(const char *path, char *out, unsigned long out_sz) {
    const char *fwd = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *slash = fwd;
    unsigned long len;

    if (back != NULL && (slash == NULL || back > slash)) slash = back;
    if (slash == NULL) { osr_copy_bounded(out, out_sz, "."); return; }
    len = (unsigned long)(slash - path);
    if (out_sz == 0) return;
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

/* osr_basename -- the last component of path, in place: a pointer into it. */
const char *osr_basename(const char *path) {
    const char *fwd = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *slash = fwd;
    if (back != NULL && (slash == NULL || back > slash)) slash = back;
    return (slash != NULL) ? slash + 1 : path;
}

/* osr_home -- the account's home directory. %USERPROFILE% is the Windows
 * spelling of $HOME, and OSR_HOME wins over both because an elevated Windows
 * run and a sudo'd POSIX one are both running as somebody other than the
 * person being riced. */
const char *osr_home(void) {
#ifdef _WIN32
    return env_str("OSR_HOME", env_str("USERPROFILE", ""));
#else
    return env_str("OSR_HOME", env_str("HOME", ""));
#endif
}

void osr_expand_home(const char *path, char *out, unsigned long out_sz) {
    if (path[0] == '~' && (path[1] == '\0' || path[1] == '/' || path[1] == '\\')) {
        const char *home = osr_home();
        if (*home != '\0') {
            osr_copy_bounded(out, out_sz, home);
            osr_append_bounded(out, out_sz, path + 1);
            return;
        }
    }
    osr_copy_bounded(out, out_sz, path);
}

/* make_one_dir -- one component, "already there" counting as success. */
static int make_one_dir(const char *path) {
#ifdef _WIN32
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

/* osr_mkdir_parents -- `mkdir -p`, without the fork. A bare drive letter is
 * skipped: "C:" is not a directory anything can create. */
int osr_mkdir_parents(const char *dir) {
    char buf[OSR_PATH_MAX];
    char *p;

    if (dir[0] == '\0' || strcmp(dir, ".") == 0) return 1;
    if (!osr_copy_bounded(buf, sizeof(buf), dir)) return 0;

    for (p = buf + 1; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            if (!(p - buf == 2 && buf[1] == ':')) make_one_dir(buf);
            *p = saved;
        }
    }
    return make_one_dir(buf);
}

/* osr_copy_file -- copy src over dst, creating dst's parent tree first.
 * A plain read/write loop rather than either system's copy call, so the one
 * genuinely OS-specific part of it is the directory creation above. */
int osr_copy_file(const char *src, const char *dst) {
    char parent[OSR_PATH_MAX];
    FILE *in;
    FILE *out;
    char buf[8192];
    size_t n;
    int ok;

    osr_dirname(dst, parent, sizeof(parent));
    osr_mkdir_parents(parent);

    in = fopen(src, "rb");
    if (in == NULL) return 0;
    out = fopen(dst, "wb");
    if (out == NULL) { fclose(in); return 0; }

    ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    }
    if (ferror(in)) ok = 0;

    fclose(in);
    fclose(out);
    return ok;
}

/* ---------------------------------------------------------------------
 * the last four platform questions
 *
 * Each of these was a bare POSIX call somewhere in this tree, and each has
 * a Windows spelling that is not the same word. Naming them once here is
 * what keeps the callers -- lib/theme.c's picker, lib/install.c's listings,
 * osr.c's startup -- one body rather than two.
 * ------------------------------------------------------------------ */

/* osr_setenv -- put a value in this process's environment, where every child
 * it spawns will inherit it. That inheritance is the whole point: the runner
 * detects the facts once and every module sees them, which is what `export`
 * bought the shell tier.
 *
 * There is no setenv() in the Microsoft C runtime; _putenv takes one
 * "NAME=value" string instead, and (unlike putenv) copies it, so the buffer
 * here may be a local. */
void osr_setenv(const char *name, const char *value) {
#ifdef _WIN32
    Str kv;
    str_init(&kv);
    str_addz(&kv, name);
    str_addc(&kv, '=');
    str_addz(&kv, value);
    _putenv(str_text(&kv));
    str_free(&kv);
#else
    setenv(name, value, 1);
#endif
}

/* osr_unsetenv -- remove it. _putenv with an empty value is how the CRT
 * spells that. */
void osr_unsetenv(const char *name) {
#ifdef _WIN32
    Str kv;
    str_init(&kv);
    str_addz(&kv, name);
    str_addc(&kv, '=');
    _putenv(str_text(&kv));
    str_free(&kv);
#else
    unsetenv(name);
#endif
}

long osr_pid(void) {
#ifdef _WIN32
    return (long)_getpid();
#else
    return (long)getpid();
#endif
}

/* osr_tmpdir -- where a scratch file goes. $TMPDIR is the POSIX spelling and
 * %TEMP% the Windows one; both are honoured on both systems, because a test
 * that sets TMPDIR to a sandbox has to be obeyed wherever it runs. */
const char *osr_tmpdir(void) {
#ifdef _WIN32
    return env_str("TMPDIR", env_str("TEMP", env_str("TMP", ".")));
#else
    return env_str("TMPDIR", "/tmp");
#endif
}

static int name_cmp_qsort(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* osr_list_dir -- the listing behind every "what is available" answer this
 * tree gives: the rices, the themes, the modules.
 *
 * With `marker`, it lists the SUBDIRECTORIES of dir that contain a file of
 * that name -- which is what makes "a theme is a directory carrying a
 * theme.list" a definition rather than a convention, and what stops a stray
 * folder under themes/ from being offered as one.
 *
 * With `strip_suffix`, it lists the FILES in dir whose name ends with it,
 * with that suffix removed.
 *
 * Names are sorted and appended to out one per line, so the caller can walk
 * them with next_line. Sorted rather than in readdir order: this text is read
 * by a person, and a listing that changes order between two machines is one
 * they cannot diff. It replaces a `glob()` that sorted for the same reason
 * and that mingw's C runtime does not have.
 */
void osr_list_dir(Str *out, const char *dir, const char *marker,
                  const char *strip_suffix) {
    DIR *d;
    struct dirent *ent;
    char **names = NULL;
    size_t count = 0, cap = 0, i;
    size_t suffix_len = (strip_suffix != NULL) ? strlen(strip_suffix) : 0;

    d = opendir(dir);
    if (d == NULL) return;

    while ((ent = readdir(d)) != NULL) {
        char path[OSR_PATH_MAX];
        size_t name_len = strlen(ent->d_name);
        size_t keep = name_len;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        /* A leading dot is skipped whichever mode this is in, because glob's
         * `*` never matched one either and every caller here inherited that
         * reading -- a theme's `.DS_Store` is not a wallpaper. */
        if (ent->d_name[0] == '.') continue;

        if (marker != NULL) {
            if (!osr_path_join(path, sizeof(path), dir, ent->d_name)) continue;
            if (!osr_append_bounded(path, sizeof(path), "/")) continue;
            if (!osr_append_bounded(path, sizeof(path), marker)) continue;
            if (!file_exists(path)) continue;
        } else if (strip_suffix != NULL) {
            if (name_len <= suffix_len) continue;
            if (strcmp(ent->d_name + name_len - suffix_len, strip_suffix) != 0) continue;
            keep = name_len - suffix_len;
        }

        if (count == cap) {
            cap = cap ? cap * 2 : 16;
            names = (char **)realloc(names, cap * sizeof *names);
            if (names == NULL) osr_die_oom();
        }
        names[count] = (char *)malloc(keep + 1);
        if (names[count] == NULL) osr_die_oom();
        memcpy(names[count], ent->d_name, keep);
        names[count][keep] = '\0';
        count++;
    }
    closedir(d);

    if (count > 1) qsort(names, count, sizeof *names, name_cmp_qsort);
    for (i = 0; i < count; i++) {
        str_addz(out, names[i]);
        str_addc(out, '\n');
        free(names[i]);
    }
    free(names);
}

/* osr_interactive -- is there a person at the other end? Both directions are
 * asked, because a picker has to print a prompt AND read an answer. */
int osr_interactive(void) {
#ifdef _WIN32
    return osr_color_mode(1) != OSR_COLOR_NONE && _isatty(0);
#else
    return isatty(0) && isatty(1);
#endif
}

/* osr_tty_open -- the controlling terminal itself, opened for reading and
 * writing, or NULL when there is none.
 *
 * A picker must not read its answer from stdin or print its prompt to stdout:
 * either one may be a pipe carrying the value this command is being asked
 * for. /dev/tty is the POSIX name for "the terminal regardless of
 * redirection"; CONIN$/CONOUT$ are the Windows pair, and because they are two
 * names rather than one, the write half is handed back separately.
 */
FILE *osr_tty_open(FILE **out_stream) {
#ifdef _WIN32
    FILE *in = fopen("CONIN$", "r");
    FILE *outf = fopen("CONOUT$", "w");
    if (in == NULL || outf == NULL) {
        if (in != NULL) fclose(in);
        if (outf != NULL) fclose(outf);
        if (out_stream != NULL) *out_stream = NULL;
        return NULL;
    }
    if (out_stream != NULL) *out_stream = outf;
    else fclose(outf);
    return in;
#else
    FILE *tty = fopen("/dev/tty", "r+");
    if (out_stream != NULL) *out_stream = tty;
    return tty;
#endif
}

/* osr_path_taken -- `[ -e path ]`: is there ANYTHING at this path?
 *
 * Not file_exists, and the difference is the whole reason this exists: a
 * dangling symlink is not a file you can open, but it is a path that is
 * already spoken for, and a seed-once layer that overwrote one would be
 * clobbering a deliberate link. lstat answers that on POSIX; on Windows the
 * file attributes do, and a reparse point is reported without following it,
 * which is the same reading.
 */
int osr_path_taken(const char *path) {
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return lstat(path, &st) == 0;
#endif
}

/* osr_absolute_dir -- an existing directory's canonical absolute path, which
 * is the `cd -- "$dir" && pwd` a shell used to turn a relative pick into
 * something storable. Returns 0 when the directory cannot be resolved, and
 * the caller keeps what it had. */
int osr_absolute_dir(const char *dir, char *out, unsigned long out_sz) {
#ifdef _WIN32
    DWORD n = GetFullPathNameA(dir, (DWORD)out_sz, out, NULL);
    return n > 0 && n < out_sz;
#else
    char buf[OSR_PATH_MAX];
    if (realpath(dir, buf) == NULL) return 0;
    return osr_copy_bounded(out, out_sz, buf);
#endif
}
