/* lib/winui.c -- see lib/winui.h. C89. */
#include "winui.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef enum {
    OSR_TAG_INFO,
    OSR_TAG_WARN,
    OSR_TAG_ERROR,
    OSR_TAG_SUCCESS
} osr_tag;

static int stream_is_console(HANDLE h) {
    return GetFileType(h) == FILE_TYPE_CHAR;
}

static WORD tag_color(osr_tag tag) {
    switch (tag) {
        case OSR_TAG_INFO:    return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case OSR_TAG_WARN:    return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case OSR_TAG_ERROR:   return FOREGROUND_RED | FOREGROUND_INTENSITY;
        case OSR_TAG_SUCCESS: return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        default:               return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
}

/* emit -- print "<colored label><prefix><message>\n" to stream. prefix may
 * be NULL. Mirrors lib/log.sh's `printf '%b%-8s%b%s\n' ...` line shape.
 */
static void emit(FILE *stream, HANDLE h, osr_tag tag, const char *label, const char *prefix, const char *fmt, va_list ap) {
    int use_color;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    WORD original;

    use_color = stream_is_console(h) && GetConsoleScreenBufferInfo(h, &csbi);
    original = use_color ? csbi.wAttributes : 0;

    if (use_color) SetConsoleTextAttribute(h, tag_color(tag));
    fprintf(stream, "%-*s", OSR_TAG_WIDTH, label);
    if (use_color) SetConsoleTextAttribute(h, original);

    if (prefix != NULL) fprintf(stream, "%s", prefix);
    vfprintf(stream, fmt, ap);
    fprintf(stream, "\n");
}

void osr_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(stdout, GetStdHandle(STD_OUTPUT_HANDLE), OSR_TAG_INFO, "[INFO]", NULL, fmt, ap);
    va_end(ap);
}

void osr_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(stderr, GetStdHandle(STD_ERROR_HANDLE), OSR_TAG_WARN, "[WARN]", NULL, fmt, ap);
    va_end(ap);
}

void osr_success(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(stdout, GetStdHandle(STD_OUTPUT_HANDLE), OSR_TAG_SUCCESS, "[DONE]", NULL, fmt, ap);
    va_end(ap);
}

void osr_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(stderr, GetStdHandle(STD_ERROR_HANDLE), OSR_TAG_ERROR, "[ERROR]", NULL, fmt, ap);
    va_end(ap);
    exit(1);
}

void osr_info_step(unsigned long n, unsigned long total, const char *fmt, ...) {
    char prefix[32];
    va_list ap;

    if (total > 0) {
        sprintf(prefix, "[%02lu/%02lu] ", n, total);
    } else {
        prefix[0] = '\0';
    }

    va_start(ap, fmt);
    emit(stdout, GetStdHandle(STD_OUTPUT_HANDLE), OSR_TAG_INFO, "[INFO]", prefix, fmt, ap);
    va_end(ap);
}

/* -------------------------------------------------------------------------
 * osr_run_step -- C port of lib/ui.sh's run_step/_step_paint/_spin. See
 * ui.h's header comment for the overall contract.
 * ---------------------------------------------------------------------- */

#define OSR_STEP_TAIL_LINES 5
#define OSR_STEP_LINE_LEN   300
#define OSR_STEP_MAX_LINES  64

static int env_var_set(const char *name) {
    char buf[8];
    DWORD n = GetEnvironmentVariableA(name, buf, (DWORD)sizeof(buf));
    return n > 0;
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
    osr_info("%s", desc);
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

int osr_run_step(const char *desc, const char *cmd) {
    HANDLE out_handle = GetStdHandle(STD_OUTPUT_HANDLE);

    if (!stream_is_console(out_handle) || env_var_set("OSR_VERBOSE")) {
        return run_step_plain(desc, cmd);
    }

    {
        char log_path[300];
        step_temp_log_path(log_path, sizeof(log_path));
        if (log_path[0] == '\0') return run_step_plain(desc, cmd);
        return run_step_tty(out_handle, desc, cmd, log_path);
    }
}

#else /* !_WIN32 */

/* Plain, colorless text -- this tier is Windows-only today (see lib/net.c's
 * #else branch for the same "documented stub, not a claim of support"
 * framing). A real terminal would want ANSI escapes here, ported from
 * lib/ui.sh's OSR_RED/GREEN/... vars.
 */

static void emit(FILE *stream, const char *label, const char *prefix, const char *fmt, va_list ap) {
    fprintf(stream, "%-*s", OSR_TAG_WIDTH, label);
    if (prefix != NULL) fprintf(stream, "%s", prefix);
    vfprintf(stream, fmt, ap);
    fprintf(stream, "\n");
}

void osr_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(stdout, "[INFO]", NULL, fmt, ap);
    va_end(ap);
}

void osr_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(stderr, "[WARN]", NULL, fmt, ap);
    va_end(ap);
}

void osr_success(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(stdout, "[DONE]", NULL, fmt, ap);
    va_end(ap);
}

void osr_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(stderr, "[ERROR]", NULL, fmt, ap);
    va_end(ap);
    exit(1);
}

void osr_info_step(unsigned long n, unsigned long total, const char *fmt, ...) {
    char prefix[32];
    va_list ap;

    if (total > 0) {
        sprintf(prefix, "[%02lu/%02lu] ", n, total);
    } else {
        prefix[0] = '\0';
    }

    va_start(ap, fmt);
    emit(stdout, "[INFO]", prefix, fmt, ap);
    va_end(ap);
}

/* osr_run_step -- no spinner/tail window on this branch (that part is
 * genuinely Windows-console-API-specific, see ui.h's header comment); the
 * plain desc-then-run behavior ui.sh itself falls back to off a TTY is a
 * faithful enough default here.
 */
int osr_run_step(const char *desc, const char *cmd) {
    osr_info("%s", desc);
    return system(cmd);
}

#endif /* _WIN32 */
