/* lib/bench/util.c -- the sysfs and formatting helpers both bench units need.
 *
 * cpu.c and power.c each want to read a one-line sysfs attribute as a number,
 * compose a path without a fixed buffer, and read a monotonic clock. They had a
 * private copy of each; this is the single one. Small enough that the
 * duplication was tempting, and duplicated helpers are exactly how two files
 * end up disagreeing about what a malformed sysfs value means.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "bench.h"

#include <time.h>

int bench_read_trim(Str *out, const char *path) {
    char *buf;
    size_t len, start, end;
    buf = slurp(path, &len);
    if (buf == NULL) return 0;
    start = 0;
    while (start < len && is_space(buf[start])) start++;
    end = len;
    while (end > start && is_space(buf[end - 1])) end--;
    str_add(out, buf + start, end - start);
    free(buf);
    return 1;
}

/* A sysfs attribute that exists but holds something unparseable reads as
 * absent, in both readers. Acting on half a number is worse than acting on
 * none -- especially when the number is a wattage the report will print. */
int bench_read_long(const char *path, long *out) {
    Str s;
    char *endp;
    long v;
    int ok = 0;
    str_init(&s);
    if (bench_read_trim(&s, path) && s.len > 0) {
        v = strtol(str_text(&s), &endp, 10);
        if (*endp == '\0') { *out = v; ok = 1; }
    }
    str_free(&s);
    return ok;
}

int bench_read_ulong(const char *path, unsigned long *out) {
    Str s;
    char *endp;
    unsigned long v;
    int ok = 0;
    str_init(&s);
    if (bench_read_trim(&s, path) && s.len > 0) {
        v = strtoul(str_text(&s), &endp, 10);
        if (*endp == '\0') { *out = v; ok = 1; }
    }
    str_free(&s);
    return ok;
}

void bench_join3(Str *out, const char *a, const char *b, const char *c) {
    str_reset(out);
    str_addz(out, a);
    str_addz(out, b);
    str_addz(out, c);
}

void bench_set_str(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* CLOCK_MONOTONIC, not wall clock: an NTP step in the middle of a benchmark
 * must not turn into a power reading. */
double bench_now_sec(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
