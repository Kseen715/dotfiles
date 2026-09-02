/* lib/ui.c -- lib/ui.sh's live step window: a dimmed tail of a step's own
 * output with a spinner line under it, repainted in place, collapsing to one
 * [ok]/[!!] line when the step finishes.
 *
 * ONE FILE, TWO BODIES, and this is the subsystem where that split is least
 * negotiable: the two systems share no code here at all. The POSIX body moves
 * the cursor with ANSI escapes around a child it watches with kill/waitpid;
 * the Windows body moves it with the console API around a CreateProcess
 * handle, because a real XP or Windows 7 console interprets no escape
 * sequence -- and unlike color, which degrades to plain text, a block that
 * repaints itself has nothing to degrade to. Everything they DO share -- the
 * tag column, the palette, the five log lines -- is lib/common.c's.
 *
 * What the POSIX window must do is stated in test/unit_c/ui_test.c. Several
 * behaviours below look like quirks and are load-bearing, so they are kept
 * and explained rather than cleaned up:
 *
 *   - the status line is printed with printf's `%b`, so a description
 *     carrying backslash escapes is expanded (expand_b below); the tailed
 *     log lines go through `%s` and are NOT expanded.
 *   - the tail pipeline was `tail -n N | tr -d '\r' | sed 's/ESC\[[0-9;?]*
 *     [A-Za-z]//g' | cut -c 1-COLS` inside a `$(...)`, so: all CR bytes
 *     die, only that exact CSI shape is stripped, lines are cut to COLS
 *     BYTES, and trailing blank lines vanish with the command
 *     substitution's trailing newlines.
 *   - `[ -t 1 ]` gates colors, the cursor hide/show and the whole live
 *     window (section 3 auto-degrade): piped or --verbose output stays plain.
 *
 * The deliberate exceptions are named where they are made -- see
 * filter_line_bytes for CR, tail_window for the cut width, and tag_pad for
 * the tag column, which this file pads out to OSR_TAG_WIDTH so a spinner or
 * [ok] line starts its text where the [INFO] lines around it do. All three
 * are asserted where they are decided, in test/unit_c/ui_test.c.
 *
 * Why the POSIX side is also a helper BINARY (`osr ui ...`) rather than only
 * an in-process API: run_step's arguments were shell FUNCTIONS (pkg_install,
 * as_root, ...), so only a shell could fork them, and lib/ui.sh kept that fork
 * and handed every byte of terminal output here. `spin` therefore watches a
 * process it did not spawn, with kill(pid, 0), the same way the sh `_spin`
 * did. Nothing in this tree still calls it that way, but the surface is what
 * a shell module gets.
 *
 * Subcommands (see usage() at the bottom):
 *   vars step-prefix tty-mode cursor-hide cursor-show
 *   paint done spin result fail-tail
 *
 * C89 + POSIX, and C89 + Win32.
 */
#ifndef _WIN32

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#define _DARWIN_C_SOURCE 1

#include "common.h"
#include "cmds.h"
#include "ui.h"

#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

#define OSR_TAIL_LINES_DEFAULT 5
#define OSR_SPIN_INTERVAL_NS 200000000L /* sh: `sleep 0.2` */

/* ---------------------------------------------------------------------
 * the tail window
 * ------------------------------------------------------------------ */

/* read_tail -- the bytes `tail -n n <path>` would print, as a malloc'd
 * NUL-terminated buffer (*out_len excludes the NUL). NULL when the file is
 * missing or empty.
 *
 * Reads from the end in growing chunks instead of slurping the file: a
 * step's log is whatever the command printed, and a kernel build's log is
 * not something to re-read in full several times a second.
 */
static char *read_tail(const char *path, long n, size_t *out_len) {
    FILE *fp;
    long size;
    long chunk = 8192;
    char *buf = NULL;

    *out_len = 0;
    if (n <= 0) return NULL;
    fp = fopen(path, "rb");
    if (fp == NULL) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    size = ftell(fp);
    if (size <= 0) { fclose(fp); return NULL; }

    for (;;) {
        long start;
        long i;
        long lines;
        size_t got;

        if (chunk > size) chunk = size;
        buf = (char *)realloc(buf, (size_t)chunk + 1);
        if (buf == NULL) { fclose(fp); osr_die_oom(); }
        if (fseek(fp, size - chunk, SEEK_SET) != 0) { free(buf); fclose(fp); return NULL; }
        got = fread(buf, 1, (size_t)chunk, fp);
        buf[got] = '\0';

        /* Walk back over (at most) n line breaks. The newline that ends
         * the file is a terminator, not a separator -- `tail -n 1` on
         * "a\nb\n" is "b\n", so it does not count. */
        i = (long)got;
        if (i > 0 && buf[i - 1] == '\n') i--;
        lines = 0;
        start = -1;
        while (i > 0) {
            if (buf[i - 1] == '\n') {
                lines++;
                if (lines == n) { start = i; break; }
            }
            i--;
        }

        if (start >= 0 || chunk == size) {
            if (start < 0) start = 0; /* whole file is fewer than n lines */
            memmove(buf, buf + start, got - (size_t)start);
            *out_len = got - (size_t)start;
            buf[*out_len] = '\0';
            fclose(fp);
            return buf;
        }
        chunk *= 4; /* not enough lines in this chunk -- reach further back */
    }
}

/* filter_line_bytes -- `sed 's/ESC\[[0-9;?]*[A-Za-z]//g'` over the whole
 * buffer, with CR given the meaning a terminal gives it. The CSI scan cannot
 * run past a newline (neither the parameter class nor the final byte class
 * contains one), so doing it stream-wide is the same as sed's line-at-a-time
 * pass.
 *
 * CR is a carriage return, not a byte to delete. dpkg writes its progress as
 * one line rewritten in place (`(Reading database ... 5%\r(Reading database
 * ... 10%\r...`), so ui.sh's `tr -d '\r'` glued every frame of it into a
 * single enormous line, which then wrapped and desynced the repaint -- the
 * cause of the shredded `apt install` window. Here CR rewinds the write
 * position to the start of the current line and the bytes after it OVERWRITE
 * what was there, exactly as on a terminal: progress collapses to its last
 * state, while a CRLF log still keeps the text before the CR.
 */
static void filter_line_bytes(Str *out, const char *b, size_t len) {
    size_t line_start = out->len;   /* first byte of the line being built */
    size_t w = out->len;            /* write position: <= out->len after a CR */
    size_t i = 0;
    while (i < len) {
        if (b[i] == '\r') { w = line_start; i++; continue; }
        if (b[i] == '\033' && i + 1 < len && b[i + 1] == '[') {
            size_t j = i + 2;
            while (j < len && ((b[j] >= '0' && b[j] <= '9') || b[j] == ';' || b[j] == '?')) j++;
            if (j < len && ((b[j] >= 'A' && b[j] <= 'Z') || (b[j] >= 'a' && b[j] <= 'z'))) {
                i = j + 1; /* whole escape dropped */
                continue;
            }
            /* Unterminated: sed leaves it alone, so we emit the ESC and
             * carry on scanning from the next byte. */
        }
        if (b[i] == '\n') {
            /* The line ends with whatever it holds -- an overwrite shorter
             * than what it replaced leaves the old tail visible, as on a
             * terminal -- so append past it, never at w. */
            str_addc(out, '\n');
            line_start = w = out->len;
        } else if (w < out->len) {
            out->p[w++] = b[i];     /* overwriting after a CR */
        } else {
            str_addc(out, b[i]);
            w = out->len;
        }
        i++;
    }
}

typedef struct {
    Str *items;
    size_t count;
} Lines;

static void lines_free(Lines *l) {
    size_t i;
    for (i = 0; i < l->count; i++) str_free(&l->items[i]);
    free(l->items);
    l->items = NULL;
    l->count = 0;
}

/* tail_window -- the finished `$(tail | sed | cut)` value, split the way
 * ui.sh's `while IFS= read -r` loop saw it: every line cut to fit one
 * terminal row, and the trailing blank lines gone with the command
 * substitution's trailing newlines (so an all-blank tail paints nothing).
 *
 * cols - 1, not cols: paint_block rewinds by the number of lines it printed,
 * so a line that fills the last column -- and wraps onto a second row on a
 * terminal that wraps eagerly -- would make every later rewind one row short
 * and walk the block down the screen, leaving the old frames behind.
 */
static void tail_window(Lines *out, const char *path, long tail_lines, long cols) {
    char *raw;
    size_t raw_len;
    Str filtered;
    size_t i;
    size_t start;
    size_t count;
    Str *items;

    out->items = NULL;
    out->count = 0;

    raw = read_tail(path, tail_lines, &raw_len);
    if (raw == NULL) return;

    str_init(&filtered);
    filter_line_bytes(&filtered, raw, raw_len);
    free(raw);

    /* trailing newlines: eaten by `$(...)` */
    while (filtered.len > 0 && filtered.p[filtered.len - 1] == '\n') filtered.p[--filtered.len] = '\0';
    if (filtered.len == 0) { str_free(&filtered); return; }

    count = 1;
    for (i = 0; i < filtered.len; i++) {
        if (filtered.p[i] == '\n') count++;
    }
    items = (Str *)calloc(count, sizeof(Str));
    if (items == NULL) osr_die_oom();

    start = 0;
    count = 0;
    for (i = 0; i <= filtered.len; i++) {
        if (i == filtered.len || filtered.p[i] == '\n') {
            size_t len = i - start;
            /* cut -c 1-(COLS-1), bytes: one row per line, never a wrap */
            if (cols > 1 && len > (size_t)(cols - 1)) len = (size_t)(cols - 1);
            str_init(&items[count]);
            str_add(&items[count], filtered.p + start, len);
            count++;
            start = i + 1;
        }
    }
    str_free(&filtered);
    out->items = items;
    out->count = count;
}

/* ---------------------------------------------------------------------
 * the block: paint / erase / final line
 * ------------------------------------------------------------------ */

/* emit_tail_line -- printf '\r\033[2K%b%s%b\n' "$OSR_DIM" "$line" "$OSR_NC".
 * The line itself is a `%s`: log output must never have its backslashes
 * re-interpreted. The leading CR is not decoration: ESC[2K erases the row but
 * leaves the cursor where it is, so without it a desynced column would print
 * the whole block staircased to the right. */
static void emit_tail_line(Str *o, const char *line) {
    str_addz(o, "\r\033[2K");
    if (expand_b(o, color("OSR_DIM"))) return;
    str_addz(o, line);
    if (expand_b(o, color("OSR_NC"))) return;
    str_addc(o, '\n');
}

/* emit_status_line -- printf '\r\033[2K%b\n' "$1". `%b` on purpose: that is
 * what the sh original did to its already-colored status string; the CR is
 * emit_tail_line's, for the same reason. */
static void emit_status_line(Str *o, const char *line) {
    str_addz(o, "\r\033[2K");
    if (expand_b(o, line)) return;
    str_addc(o, '\n');
}

static void emit_cursor_up(Str *o, int n) {
    char buf[32];
    sprintf(buf, "\033[%dA", n);
    str_addz(o, buf);
}

/* paint_block -- _step_paint: rewind over the block we painted last time,
 * lay down the dimmed tail again, then status_line. Returns the number of
 * rows the new block occupies, which is what the next call rewinds by
 * (relative, never an absolute row -- a console that scrolled in between
 * must not desync us).
 */
static int paint_block(Str *o, int painted, const char *log, const char *status_line) {
    long tail_lines = env_long("OSR_TAIL_LINES", OSR_TAIL_LINES_DEFAULT);
    int drawn = 0;

    if (painted > 0) emit_cursor_up(o, painted);

    if (tail_lines > 0) {
        Lines lines;
        size_t i;
        tail_window(&lines, log, tail_lines, term_cols());
        for (i = 0; i < lines.count; i++) {
            emit_tail_line(o, str_text(&lines.items[i]));
            drawn++;
        }
        lines_free(&lines);
    }

    emit_status_line(o, status_line);
    drawn++;
    return drawn;
}

/* done_block -- _step_done: blank the live block, leave the cursor at its
 * top row, print one result line there, and give the cursor back. */
static void done_block(Str *o, int painted, const char *line) {
    if (painted > 0) {
        int i;
        emit_cursor_up(o, painted);
        for (i = 0; i < painted; i++) str_addz(o, "\r\033[2K\n");
        emit_cursor_up(o, painted);
    }
    emit_status_line(o, line);
    if (isatty(1)) str_addz(o, "\033[?25h");
}

/* ---------------------------------------------------------------------
 * subcommands
 * ------------------------------------------------------------------ */

static int parse_int(const char *s) {
    char *end;
    long n = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0') return 0;
    return (int)n;
}

/* cmd_vars -- the `if [ -t 1 ] && [ -z "$NO_COLOR" ]` block at the top of
 * ui.sh, as shell assignments for it to eval. The escapes stay LITERAL
 * (backslash-zero-three-three): lib/log.sh prints them through `%b`, and
 * an already-expanded ESC byte would break the ASCII-only rule for the
 * sourced value. */
static void cmd_vars(void) {
    int fancy = isatty(query_fd()) && !env_is_set("NO_COLOR");
    if (fancy) {
        printf("OSR_RED='\\033[0;31m'\n");
        printf("OSR_GREEN='\\033[0;32m'\n");
        printf("OSR_YELLOW='\\033[0;33m'\n");
        printf("OSR_CYAN='\\033[0;36m'\n");
        printf("OSR_DIM='\\033[2m'\n");
        printf("OSR_NC='\\033[0m'\n");
    } else {
        printf("OSR_RED='' OSR_GREEN='' OSR_YELLOW='' OSR_CYAN='' OSR_DIM='' OSR_NC=''\n");
    }
    printf("OSR_COLS=%ld\n", term_cols());
}

/* cmd_step_prefix -- "[03/12] " when a total is known, else nothing. */
static void cmd_step_prefix(void) {
    long total = env_long("OSR_STEP_TOTAL", 0);
    long n = env_long("OSR_STEP_N", 0);
    if (total <= 0) return;
    printf("[%02ld/%02ld] ", n, total);
}

/* cmd_spin -- _spin: repaint until pid exits, then leave the row count in
 * state_path for `result`/`done` to erase (the shell kept it in a variable;
 * we are a different process each call, so it goes to a file).
 *
 * kill(pid, 0) rather than wait(): pid is the shell's child, not ours. The
 * shell reaps it as soon as it exits -- it sits in waitpid() for us the
 * whole time -- so the pid disappearing is the same signal `kill -0` gave
 * the sh version.
 */
/* --- the same loop, for callers inside the core --------------------------
 *
 * A module written in C forks its own command and drives the window itself
 * (lib/module.c's osr_run_step), so it needs the loop as a function rather
 * than as two subcommands with a state file between them. The subcommands
 * below are the shell's entry to exactly this.
 */

int osr_ui_live(void) {
    return isatty(1) && !env_is_set("OSR_VERBOSE");
}

/* paint_one -- one frame of the block, shared by both spin loops. */
static int paint_one(int painted, int frame, const char *desc, const char *log_path) {
    static const char frames[4] = { '|', '/', '-', '\\' };
    Str status;
    Str out;
    int drawn;

    str_init(&status);
    if (!expand_b(&status, color("OSR_CYAN"))) {
        str_addc(&status, frames[frame % 4]);
        if (!expand_b(&status, color("OSR_NC"))) {
            tag_pad(&status, 1);
            str_addz(&status, desc);
        }
    }
    str_init(&out);
    drawn = paint_block(&out, painted, log_path, str_text(&status));
    out_flush(&out);
    str_free(&out);
    str_free(&status);
    return drawn;
}

static void spin_sleep(void) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = OSR_SPIN_INTERVAL_NS;
    nanosleep(&ts, NULL);
}

static void cursor_hide_if_tty(void) {
    if (isatty(1)) {
        fputs("\033[?25l", stdout);
        fflush(stdout);
    }
}

int osr_ui_spin_child(pid_t pid, const char *desc, const char *log_path, int *exit_status) {
    int painted = 0;
    int frame = 0;
    int status = 0;

    cursor_hide_if_tty();
    for (;;) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid || done < 0) break;      /* reaped here, not polled */
        painted = paint_one(painted, frame, desc, log_path);
        spin_sleep();
        frame++;
    }
    if (exit_status != NULL) {
        *exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    return painted;
}

int osr_ui_spin_pid(pid_t pid, const char *desc, const char *log_path) {
    static const char frames[4] = { '|', '/', '-', '\\' };
    int painted = 0;
    int frame = 0;

    if (isatty(1)) {
        fputs("\033[?25l", stdout);
        fflush(stdout);
    }
    while (kill(pid, 0) == 0) {
        Str status;
        Str out;
        struct timespec ts;

        str_init(&status);
        if (!expand_b(&status, color("OSR_CYAN"))) {
            str_addc(&status, frames[frame % 4]);
            if (!expand_b(&status, color("OSR_NC"))) {
                tag_pad(&status, 1);
                str_addz(&status, desc);
            }
        }
        str_init(&out);
        painted = paint_block(&out, painted, log_path, str_text(&status));
        out_flush(&out);
        str_free(&out);
        str_free(&status);

        ts.tv_sec = 0;
        ts.tv_nsec = OSR_SPIN_INTERVAL_NS;
        nanosleep(&ts, NULL);
        frame++;
    }
    return painted;
}

void osr_ui_result(int painted, int ok, const char *desc) {
    Str line;
    Str out;

    str_init(&line);
    if (!expand_b(&line, color(ok ? "OSR_GREEN" : "OSR_RED"))) {
        str_addz(&line, ok ? "[ok]" : "[!!]");
        if (!expand_b(&line, color("OSR_NC"))) {
            tag_pad(&line, 4);
            str_addz(&line, desc);
        }
    }
    str_init(&out);
    done_block(&out, painted, str_text(&line));
    out_flush(&out);
    str_free(&out);
    str_free(&line);
}

void osr_ui_fail_tail(long n, const char *log_path) {
    size_t len = 0;
    char *buf = read_tail(log_path, n, &len);
    if (buf == NULL) return;
    fwrite(buf, 1, len, stderr);
    fflush(stderr);
    free(buf);
}

void osr_ui_append_log(const char *step_log) {
    char *buf;
    size_t len;
    FILE *fp;

    buf = slurp(step_log, &len);
    if (buf == NULL) return;
    fp = fopen(env_str("OSR_LOG", "/dev/null"), "ab");
    if (fp != NULL) {
        if (len > 0) fwrite(buf, 1, len, fp);
        fclose(fp);
    }
    free(buf);
}

static int cmd_spin(const char *pid_s, const char *desc, const char *log, const char *state_path) {
    static const char frames[4] = { '|', '/', '-', '\\' };
    pid_t pid = (pid_t)parse_int(pid_s);
    int painted = 0;
    int frame = 0;
    FILE *fp;

    if (isatty(1)) {
        fputs("\033[?25l", stdout);
        fflush(stdout);
    }

    while (kill(pid, 0) == 0) {
        Str status;
        Str out;
        struct timespec ts;

        /* printf '%b%s%b %s' "$OSR_CYAN" "$frame" "$OSR_NC" "$desc" -- the
         * description stays raw here and is expanded by the `%b` in
         * emit_status_line, exactly one expansion, as in sh. */
        str_init(&status);
        if (!expand_b(&status, color("OSR_CYAN"))) {
            str_addc(&status, frames[frame % 4]);
            if (!expand_b(&status, color("OSR_NC"))) {
                tag_pad(&status, 1);
                str_addz(&status, desc);
            }
        }

        str_init(&out);
        painted = paint_block(&out, painted, log, str_text(&status));
        out_flush(&out);
        str_free(&out);
        str_free(&status);

        ts.tv_sec = 0;
        ts.tv_nsec = OSR_SPIN_INTERVAL_NS;
        nanosleep(&ts, NULL);
        frame++;
    }

    fp = fopen(state_path, "wb");
    if (fp != NULL) {
        fprintf(fp, "%d\n", painted);
        fclose(fp);
    }
    return 0;
}

/* read_painted -- the row count cmd_spin left behind. A missing or
 * unreadable file means "nothing on screen", which is also the state after
 * a step that finished before its first repaint. */
static int read_painted(const char *state_path) {
    FILE *fp = fopen(state_path, "rb");
    char buf[32];
    int n = 0;
    if (fp == NULL) return 0;
    if (fgets(buf, (int)sizeof(buf), fp) != NULL) {
        buf[strcspn(buf, "\r\n")] = '\0';
        n = parse_int(buf);
    }
    fclose(fp);
    if (n < 0) n = 0;
    return n;
}

/* cmd_result -- run_step's ending: compose the `[ok]`/`[!!] <desc>` line
 * the sh version built with printf, then collapse the block onto it. */
static int cmd_result(const char *state_path, const char *status, const char *desc) {
    /* Three outcomes, not two: try_step's step is one whose failure is a
     * degraded result rather than a broken run (an optional cross-check tool
     * that this distro does not package). Painting that `[!!]` in red taught
     * people to ignore red, so it gets its own tag. */
    int warn = strcmp(status, "warn") == 0;
    int ok = strcmp(status, "ok") == 0;
    const char *tag = warn ? "[--]" : ok ? "[ok]" : "[!!]";
    const char *col = warn ? "OSR_YELLOW" : ok ? "OSR_GREEN" : "OSR_RED";
    Str line;
    Str out;
    int painted = read_painted(state_path);

    str_init(&line);
    if (!expand_b(&line, color(col))) {
        str_addz(&line, tag);
        if (!expand_b(&line, color("OSR_NC"))) {
            tag_pad(&line, strlen(tag));
            str_addz(&line, desc);
        }
    }

    str_init(&out);
    done_block(&out, painted, str_text(&line));
    out_flush(&out);
    str_free(&out);
    str_free(&line);

    remove(state_path);
    return 0;
}

/* cmd_fail_tail -- `tail -n <n> <log> >&2`: the raw bytes, no filtering,
 * so a failed step's dump is exactly what the command printed. */
static int cmd_fail_tail(const char *n_s, const char *log) {
    size_t len = 0;
    char *buf = read_tail(log, (long)parse_int(n_s), &len);
    if (buf == NULL) return 0;
    fwrite(buf, 1, len, stderr);
    fflush(stderr);
    free(buf);
    return 0;
}

/* usage -- split across several fputs calls on purpose: C90 only promises
 * 509 bytes for a single string literal. */
static int usage(void) {
    fputs("usage: osr ui <subcommand> [args]\n\n", stderr);
    fputs("  vars                          shell assignments: OSR_RED..OSR_NC, OSR_COLS\n", stderr);
    fputs("  step-prefix                   \"[NN/MM] \" from $OSR_STEP_N/$OSR_STEP_TOTAL\n", stderr);
    fputs("  tty-mode                      exit 0 when the live window applies\n", stderr);
    fputs("  cursor-hide | cursor-show     ANSI cursor visibility (TTY only)\n", stderr);
    fputs("  paint <painted> <log> <status>   repaint the block; exit = rows painted\n", stderr);
    fputs("  done <painted> <line>         erase the block, print one result line\n", stderr);
    fputs("  spin <pid> <desc> <log> <state>  repaint until pid exits; rows -> <state>\n", stderr);
    fputs("  result <state> ok|warn|fail <desc>  compose the result line and collapse\n", stderr);
    fputs("  fail-tail <n> <log>           last n lines of <log> to stderr\n", stderr);
    return 2;
}

int osr_ui_main(int argc, char **argv) {
    const char *cmd;

    if (argc < 2) return usage();
    cmd = argv[1];

    if (strcmp(cmd, "vars") == 0) {
        cmd_vars();
        return 0;
    }
    if (strcmp(cmd, "step-prefix") == 0) {
        cmd_step_prefix();
        return 0;
    }
    if (strcmp(cmd, "tty-mode") == 0) {
        /* ui.sh: [ -t 1 ] && [ -z "${OSR_VERBOSE:-}" ] */
        return (isatty(1) && !env_is_set("OSR_VERBOSE")) ? 0 : 1;
    }
    if (strcmp(cmd, "cursor-hide") == 0) {
        if (isatty(1)) { fputs("\033[?25l", stdout); fflush(stdout); }
        return 0;
    }
    if (strcmp(cmd, "cursor-show") == 0) {
        if (isatty(1)) { fputs("\033[?25h", stdout); fflush(stdout); }
        return 0;
    }
    if (strcmp(cmd, "paint") == 0 && argc == 5) {
        Str out;
        int painted;
        str_init(&out);
        painted = paint_block(&out, parse_int(argv[2]), argv[3], argv[4]);
        out_flush(&out);
        str_free(&out);
        return painted > 255 ? 255 : painted; /* rows painted, for the caller */
    }
    if (strcmp(cmd, "done") == 0 && argc == 4) {
        Str out;
        str_init(&out);
        done_block(&out, parse_int(argv[2]), argv[3]);
        out_flush(&out);
        str_free(&out);
        return 0;
    }
    if (strcmp(cmd, "spin") == 0 && argc == 6) {
        return cmd_spin(argv[2], argv[3], argv[4], argv[5]);
    }
    if (strcmp(cmd, "result") == 0 && argc == 5) {
        return cmd_result(argv[2], argv[3], argv[4]);
    }
    if (strcmp(cmd, "fail-tail") == 0 && argc == 4) {
        return cmd_fail_tail(argv[2], argv[3]);
    }
    return usage();
}

#else /* _WIN32 */

/* --- the Windows body ------------------------------------------------------
 *
 * The same window, painted through the classic console API:
 * SetConsoleCursorPosition to get back to the top of the block,
 * FillConsoleOutputCharacterA to clear a row. Not ANSI, because the reach
 * targets this tree keeps (XP, 7) interpret none of it -- see the file header.
 *
 * The child is spawned with CreateProcess and its output redirected into a
 * temp file that the paint loop tails while it runs, which is this side's
 * answer to the POSIX body's pipe-and-waitpid. `cmd /c` wraps the command line
 * so that built-ins, pipes and quoting behave the way they would if the caller
 * had typed it, which is what its callers (the package dispatch, the vendor
 * install scripts) hand over.
 *
 * There is no `osr ui` command on this side: nothing here is driven from a
 * shell, so the surface is the one in-process entry point below.
 * ------------------------------------------------------------------------- */

#include "common.h"
#include "cmds.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define OSR_STEP_TAIL_LINES 5
#define OSR_STEP_LINE_LEN   300
#define OSR_STEP_MAX_LINES  64

/* stream_is_console -- can this handle be repainted at all? A pipe or a file
 * has no cursor to move, which is ui.sh's `[ -t 1 ]` for this block. */
static int stream_is_console(HANDLE h) {
    return h != NULL && h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_CHAR;
}

/* step_temp_log_path -- a per-run scratch file the child process's stdout
 * and stderr are redirected into, so the paint loop below can tail it
 * while the child is still running. Mirrors ui.sh's $OSR_LOG.step.
 */
static void step_temp_log_path(char *out, unsigned long out_sz) {
    char dir[300];
    DWORD n = GetTempPathA((DWORD)sizeof(dir), dir);
    unsigned long dir_len;
    unsigned long need;

    if (n == 0 || n >= sizeof(dir)) { if (out_sz > 0) out[0] = '\0'; return; }

    dir_len = (unsigned long)strlen(dir);
    need = dir_len + (unsigned long)strlen("osr-step-XXXXXXXX.log");
    if (need >= out_sz) { if (out_sz > 0) out[0] = '\0'; return; }

    sprintf(out, "%sosr-step-%lu.log", dir, (unsigned long)GetCurrentProcessId());
}

/* step_read_tail -- last (up to) OSR_STEP_TAIL_LINES lines currently in
 * path, CR stripped. Reads only the trailing chunk of the file, not the
 * whole thing (the child may still be writing to it) -- a bounded
 * approximation of `tail -n 5`, not byte-exact, which is fine since this
 * is a cosmetic preview: the full log still lands on disk regardless.
 */
static unsigned long step_read_tail(const char *path, char lines[][OSR_STEP_LINE_LEN], unsigned long max_lines) {
    FILE *fp;
    long size;
    static char buf[4096];
    size_t n;
    char *line_starts[OSR_STEP_MAX_LINES];
    unsigned long total;
    char *p, *line_start;
    unsigned long start, i, count;

    fp = fopen(path, "rb");
    if (fp == NULL) return 0;
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    if (size > (long)sizeof(buf) - 1) fseek(fp, -(long)(sizeof(buf) - 1), SEEK_END);
    else fseek(fp, 0, SEEK_SET);
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';

    total = 0;
    line_start = buf;
    for (p = buf; *p != '\0' && total < OSR_STEP_MAX_LINES; p++) {
        if (*p == '\n') {
            *p = '\0';
            line_starts[total++] = line_start;
            line_start = p + 1;
        }
    }
    if (*line_start != '\0' && total < OSR_STEP_MAX_LINES) line_starts[total++] = line_start;

    start = (total > max_lines) ? (total - max_lines) : 0;
    count = 0;
    for (i = start; i < total; i++) {
        char *s = line_starts[i];
        unsigned long len = (unsigned long)strlen(s);
        if (len > 0 && s[len - 1] == '\r') s[--len] = '\0';
        if (len >= OSR_STEP_LINE_LEN) len = OSR_STEP_LINE_LEN - 1;
        memcpy(lines[count], s, len);
        lines[count][len] = '\0';
        count++;
    }
    return count;
}

/* step_write_line -- clear the console row the cursor is currently on and
 * print text (color optional, bright applies FOREGROUND_INTENSITY), then
 * advance to the next row. Always re-reads the cursor position first --
 * see step_paint's comment on why nothing here caches a row number across
 * calls.
 */
static void step_write_line(HANDLE h, const char *text, WORD color) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD pos;
    DWORD written;
    char clipped[OSR_STEP_LINE_LEN];
    unsigned long len;
    unsigned long cols;

    if (!GetConsoleScreenBufferInfo(h, &csbi)) { printf("%s\n", text); return; }
    pos.X = 0;
    pos.Y = csbi.dwCursorPosition.Y;
    cols = (unsigned long)csbi.dwSize.X;
    FillConsoleOutputCharacterA(h, ' ', (DWORD)cols, pos, &written);
    SetConsoleCursorPosition(h, pos);

    len = (unsigned long)strlen(text);
    if (len >= sizeof(clipped)) len = sizeof(clipped) - 1;
    memcpy(clipped, text, len);
    clipped[len] = '\0';
    if (cols > 0 && cols < sizeof(clipped) && len > cols) clipped[cols] = '\0';

    SetConsoleTextAttribute(h, color);
    fputs(clipped, stdout);
    SetConsoleTextAttribute(h, csbi.wAttributes);
    fputc('\n', stdout);
    fflush(stdout);
}

/* step_paint -- repaint the block in place: up to OSR_STEP_TAIL_LINES
 * lines tailed from the step's own log (dim), then status_line last
 * (normal brightness). *painted tracks how many rows the block currently
 * occupies so the next call can find its way back to the top -- always by
 * asking the console where the cursor is NOW and subtracting *painted,
 * never by remembering an absolute row, so a console that scrolled during
 * the last paint can't desync this one (the same reason ui.sh's own
 * `\033[%dA` is relative, not absolute).
 */
static void step_paint(HANDLE h, const char *log_path, int *painted, const char *status_line) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    char tail[OSR_STEP_TAIL_LINES][OSR_STEP_LINE_LEN];
    unsigned long tail_count;
    unsigned long i;
    int drawn;

    if (!GetConsoleScreenBufferInfo(h, &csbi)) return;
    if (*painted > 0) {
        COORD up;
        up.X = 0;
        up.Y = csbi.dwCursorPosition.Y - (SHORT)*painted;
        if (up.Y < 0) up.Y = 0;
        SetConsoleCursorPosition(h, up);
    }

    tail_count = step_read_tail(log_path, tail, OSR_STEP_TAIL_LINES);
    drawn = 0;
    for (i = 0; i < tail_count; i++) {
        step_write_line(h, tail[i], FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        drawn++;
    }
    step_write_line(h, status_line, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    drawn++;

    *painted = drawn;
}

/* step_erase -- clear the *painted rows the block currently occupies and
 * leave the cursor at the block's top row, ready for one final result
 * line. Same "re-read the cursor every time" rule as step_paint.
 */
static void step_erase(HANDLE h, int painted) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD pos;
    int i;

    if (painted <= 0) return;
    if (!GetConsoleScreenBufferInfo(h, &csbi)) return;
    pos.X = 0;
    pos.Y = csbi.dwCursorPosition.Y - (SHORT)painted;
    if (pos.Y < 0) pos.Y = 0;
    SetConsoleCursorPosition(h, pos);

    for (i = 0; i < painted; i++) {
        step_write_line(h, "", FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }

    if (!GetConsoleScreenBufferInfo(h, &csbi)) return;
    pos.X = 0;
    pos.Y = csbi.dwCursorPosition.Y - (SHORT)painted;
    if (pos.Y < 0) pos.Y = 0;
    SetConsoleCursorPosition(h, pos);
}

/* run_step_plain -- the non-TTY / OSR_VERBOSE path: print desc once, run
 * the command with its output streamed straight to the real console (no
 * redirection, no spinner), same as ui.sh's run_step else-branch
 * (`info "$_rs_desc"; "$@"`).
 */
static int run_step_plain(const char *desc, const char *cmd) {
    osr_infof("%s", desc);
    return system(cmd);
}

/* run_step_tty -- the live spinner path: TTY attached, OSR_VERBOSE unset. */
static int run_step_tty(HANDLE h, const char *desc, const char *cmd, char *log_path) {
    static const char frames[4] = { '|', '/', '-', '\\' };
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE log_handle;
    SECURITY_ATTRIBUTES sa;
    int painted;
    unsigned long frame;
    DWORD exit_code;
    CONSOLE_CURSOR_INFO cursor_info;
    BOOL cursor_was_visible;
    char status[OSR_STEP_LINE_LEN];
    char cmdline[1024];

    if (strlen("cmd /c ") + strlen(cmd) >= sizeof(cmdline)) return run_step_plain(desc, cmd);

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    log_handle = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_handle == INVALID_HANDLE_VALUE) return run_step_plain(desc, cmd);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = log_handle;
    si.hStdError = log_handle;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    /* CreateProcess needs "cmd /c <cmd>" to run a shell command line the
     * same way system() does (built-ins, pipes, quoting rules).
     */
    sprintf(cmdline, "cmd /c %s", cmd);

    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(log_handle);
        return run_step_plain(desc, cmd);
    }
    CloseHandle(log_handle);

    cursor_was_visible = TRUE;
    if (GetConsoleCursorInfo(h, &cursor_info)) {
        cursor_was_visible = cursor_info.bVisible;
        cursor_info.bVisible = FALSE;
        SetConsoleCursorInfo(h, &cursor_info);
    }

    painted = 0;
    frame = 0;
    for (;;) {
        DWORD wait_result = WaitForSingleObject(pi.hProcess, 200);
        sprintf(status, "%-*c%s", OSR_TAG_WIDTH, frames[frame % 4], desc);
        step_paint(h, log_path, &painted, status);
        frame++;
        if (wait_result == WAIT_OBJECT_0) break;
    }

    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    step_erase(h, painted);
    {
        char result[OSR_STEP_LINE_LEN];
        if (exit_code == 0) sprintf(result, "%-*s%s", OSR_TAG_WIDTH, "[ok]", desc);
        else sprintf(result, "%-*s%s", OSR_TAG_WIDTH, "[!!]", desc);
        step_write_line(h, result, exit_code == 0
            ? (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
            : (FOREGROUND_RED | FOREGROUND_INTENSITY));
    }

    if (cursor_was_visible) {
        cursor_info.bVisible = TRUE;
        SetConsoleCursorInfo(h, &cursor_info);
    }

    if (exit_code != 0) {
        char tail[OSR_STEP_TAIL_LINES][OSR_STEP_LINE_LEN];
        unsigned long tail_count = step_read_tail(log_path, tail, OSR_STEP_TAIL_LINES);
        unsigned long i;
        for (i = 0; i < tail_count; i++) fprintf(stderr, "%s\n", tail[i]);
    }

    remove(log_path);
    return (int)exit_code;
}

int osr_run_step_cmd(const char *desc, const char *cmd) {
    HANDLE out_handle = GetStdHandle(STD_OUTPUT_HANDLE);

    if (!stream_is_console(out_handle) || env_is_set("OSR_VERBOSE")) {
        return run_step_plain(desc, cmd);
    }

    {
        char log_path[300];
        step_temp_log_path(log_path, sizeof(log_path));
        if (log_path[0] == '\0') return run_step_plain(desc, cmd);
        return run_step_tty(out_handle, desc, cmd, log_path);
    }
}

/* osr_ui_main -- `osr ui` is a shell-facing surface, and no shell drives this
 * core. Saying so beats a subcommand table that answers every word with a
 * window nothing asked for. */
int osr_ui_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    fputs("osr ui: the step window is driven in process here, not from a shell\n", stderr);
    return 2;
}

#endif /* _WIN32 */
