/* lib/bench/power.c -- finding and reading a power sensor. See bench.h for why
 * this is layered rather than a single path.
 *
 * C89 + POSIX.
 */
#define _POSIX_C_SOURCE 200809L

#include "bench.h"

#include <dirent.h>
#include <unistd.h>

static const char *const source_names[PWR_SOURCE_MAX] = {
    "none", "rapl", "hwmon", "ryzen-smu", "battery"
};

const char *pwr_source_name(PwrSource s) {
    if (s < 0 || s >= PWR_SOURCE_MAX) return "?";
    return source_names[s];
}

/* --- pure arithmetic ------------------------------------------------------ */

unsigned long pwr_energy_delta(unsigned long a, unsigned long b, unsigned long max_range) {
    if (b >= a) return b - a;
    /* Wrapped. With a known range the lost span is exact; without one the best
     * guess is that it wrapped once at the type's width, and guessing wrong
     * here is better than reporting a negative energy. */
    if (max_range > 0) return (max_range - a) + b;
    return (0xFFFFFFFFUL - a) + b + 1;
}

/* --- detection ------------------------------------------------------------ */

/* RAPL. The powercap tree names the Intel driver's nodes "intel-rapl:N" even
 * on AMD parts, so the directory name is not the thing to match on -- the
 * `name` attribute is, and "package-0" is the whole-socket domain we want
 * rather than one of its "core"/"uncore" children. */
static int detect_rapl(PwrMeter *m) {
    static const char *base = "/sys/class/powercap/";
    DIR *d;
    struct dirent *e;
    Str path, name;
    int found = 0;

    d = opendir(base);
    if (d == NULL) return 0;
    str_init(&path);
    str_init(&name);

    while (!found && (e = readdir(d)) != NULL) {
        unsigned long probe;
        if (e->d_name[0] == '.') continue;
        /* Only top-level domains: a child looks like "intel-rapl:0:1". */
        if (strchr(e->d_name, ':') == NULL) continue;

        str_reset(&name);
        bench_join3(&path, base, e->d_name, "/name");
        if (!bench_read_trim(&name, str_text(&path))) continue;
        if (strncmp(str_text(&name), "package", 7) != 0) continue;

        bench_join3(&path, base, e->d_name, "/energy_uj");
        if (!bench_read_ulong(str_text(&path), &probe)) {
            /* Present but unreadable is the common case: since the PLATYPUS
             * side channel these are 0400. Worth saying so explicitly, because
             * "run it as root" is an actionable answer. */
            bench_set_str(m->detail, sizeof(m->detail),
                    geteuid() == 0 ? "RAPL present but energy_uj unreadable"
                                   : "RAPL present but root-only - re-run as root for power numbers");
            continue;
        }
        bench_set_str(m->path, sizeof(m->path), str_text(&path));
        m->source = PWR_RAPL;
        m->max_range_uj = 0;
        bench_join3(&path, base, e->d_name, "/max_energy_range_uj");
        bench_read_ulong(str_text(&path), &m->max_range_uj);
        bench_set_str(m->detail, sizeof(m->detail), "RAPL package energy counter");
        found = 1;
    }
    closedir(d);
    str_free(&path);
    str_free(&name);
    return found;
}

/* hwmon power1_input, in microwatts. Instantaneous, so it needs polling. */
static int detect_hwmon(PwrMeter *m) {
    static const char *base = "/sys/class/hwmon/";
    DIR *d;
    struct dirent *e;
    Str path, name;
    int found = 0;

    d = opendir(base);
    if (d == NULL) return 0;
    str_init(&path);
    str_init(&name);

    while (!found && (e = readdir(d)) != NULL) {
        unsigned long probe;
        if (e->d_name[0] == '.') continue;
        bench_join3(&path, base, e->d_name, "/power1_input");
        if (!bench_read_ulong(str_text(&path), &probe)) continue;

        str_reset(&name);
        bench_set_str(m->path, sizeof(m->path), str_text(&path));
        bench_join3(&path, base, e->d_name, "/name");
        bench_read_trim(&name, str_text(&path));

        m->source = PWR_HWMON;
        str_reset(&path);
        str_addz(&path, "hwmon power sensor");
        if (name.len > 0) {
            str_addz(&path, " (");
            str_addz(&path, str_text(&name));
            str_addc(&path, ')');
        }
        bench_set_str(m->detail, sizeof(m->detail), str_text(&path));
        found = 1;
    }
    closedir(d);
    str_free(&path);
    str_free(&name);
    return found;
}

/* Battery discharge. Only meaningful while actually discharging -- on AC the
 * reading is 0 or absent, which would silently report a 0 W CPU, so the
 * status is checked rather than assumed. */
static int detect_battery(PwrMeter *m) {
    static const char *base = "/sys/class/power_supply/";
    DIR *d;
    struct dirent *e;
    Str path, status;
    int found = 0;

    d = opendir(base);
    if (d == NULL) return 0;
    str_init(&path);
    str_init(&status);

    while (!found && (e = readdir(d)) != NULL) {
        unsigned long probe;
        if (strncmp(e->d_name, "BAT", 3) != 0) continue;

        str_reset(&status);
        bench_join3(&path, base, e->d_name, "/status");
        if (!bench_read_trim(&status, str_text(&path))) continue;
        if (strcmp(str_text(&status), "Discharging") != 0) {
            bench_set_str(m->detail, sizeof(m->detail),
                    "battery present but not discharging - no power reading on AC");
            continue;
        }
        bench_join3(&path, base, e->d_name, "/power_now");
        if (!bench_read_ulong(str_text(&path), &probe)) continue;

        bench_set_str(m->path, sizeof(m->path), str_text(&path));
        m->source = PWR_BATTERY;
        bench_set_str(m->detail, sizeof(m->detail),
                "battery discharge rate - whole system, not just the CPU");
        found = 1;
    }
    closedir(d);
    str_free(&path);
    str_free(&status);
    return found;
}

int pwr_detect(PwrMeter *m) {
    memset(m, 0, sizeof(*m));
    m->source = PWR_NONE;
    /* Order is by accuracy, not convenience. */
    if (detect_rapl(m)) return 1;
    if (detect_hwmon(m)) return 1;
    if (detect_battery(m)) return 1;
    if (m->detail[0] == '\0') {
        bench_set_str(m->detail, sizeof(m->detail),
                "no power sensor: no RAPL, no hwmon power input, no battery");
    }
    return 0;
}

/* --- measuring ------------------------------------------------------------ */

void pwr_begin(PwrMeter *m) {
    m->sum_w = 0.0;
    m->samples = 0;
    m->e_start = 0;
    m->t_start = bench_now_sec();
    if (m->source == PWR_RAPL) bench_read_ulong(m->path, &m->e_start);
}

void pwr_sample(PwrMeter *m) {
    unsigned long uw;
    if (m->source != PWR_HWMON && m->source != PWR_BATTERY) return;
    if (!bench_read_ulong(m->path, &uw)) return;
    m->sum_w += (double)uw / 1e6;
    m->samples++;
}

int pwr_end(PwrMeter *m, double *watts) {
    double elapsed = bench_now_sec() - m->t_start;

    if (m->source == PWR_RAPL) {
        unsigned long e_end = 0;
        if (elapsed <= 0.0) return 0;
        if (!bench_read_ulong(m->path, &e_end)) return 0;
        *watts = ((double)pwr_energy_delta(m->e_start, e_end, m->max_range_uj) / 1e6) / elapsed;
        return 1;
    }
    if (m->samples > 0) {
        *watts = m->sum_w / (double)m->samples;
        return 1;
    }
    return 0;
}
