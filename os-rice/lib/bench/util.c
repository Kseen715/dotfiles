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
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "bench.h"

#include <time.h>

/* bench_is_wsl -- a WSL2 kernel is built as "5.15.153.1-microsoft-standard-WSL2".
 *
 * Asked so that neither the detector nor the diagnostic hands out advice that
 * cannot work: a Hyper-V guest has no RAPL to load a driver for and no hwmon
 * to probe, so "load intel_rapl_msr" -- the right answer on a bare-metal box
 * with the same empty powercap tree -- would cost the reader an afternoon.
 *
 * The path is overridable, like OSR_POWERCAP and OSR_HWMON, because BOTH
 * answers need testing and the machine running the suite only ever gives one.
 * Not cached, so a test can change it between cases; it is a 30-byte read made
 * a handful of times per run.
 */
int bench_is_wsl(void) {
    char *buf;
    size_t len, i;
    int yes = 0;

    buf = slurp(env_str("OSR_OSRELEASE", "/proc/sys/kernel/osrelease"), &len);
    if (buf == NULL) return 0;
    for (i = 0; i + 9 <= len; i++) {
        if (memcmp(buf + i, "microsoft", 9) == 0 || memcmp(buf + i, "Microsoft", 9) == 0) {
            yes = 1;
            break;
        }
    }
    free(buf);
    return yes;
}

/* bench_row -- the aligned two-column shape every report here uses. Shared
 * rather than duplicated because the diagnostic prints rows from power.c and
 * cpu.c alike, and two copies would drift apart by a space. */
void bench_row(Str *out, const char *label, const char *value) {
    size_t n = strlen(label);
    str_addzz(out, "  ", label, (const char *)NULL);
    while (n < BENCH_LABEL) { str_addc(out, ' '); n++; }
    str_addzz(out, value, "\n", (const char *)NULL);
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
