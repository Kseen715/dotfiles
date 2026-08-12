/* lib/ui.c -- see lib/ui.h. C89. */
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

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
    fprintf(stream, "%-8s", label);
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

#else /* !_WIN32 */

/* Plain, colorless text -- this tier is Windows-only today (see lib/net.c's
 * #else branch for the same "documented stub, not a claim of support"
 * framing). A real terminal would want ANSI escapes here, ported from
 * lib/ui.sh's OSR_RED/GREEN/... vars.
 */

static void emit(FILE *stream, const char *label, const char *prefix, const char *fmt, va_list ap) {
    fprintf(stream, "%-8s", label);
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

#endif /* _WIN32 */
